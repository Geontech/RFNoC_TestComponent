/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

**************************************************************************/

#include "RFNoC_TestComponent.h"

PREPARE_LOGGING(RFNoC_TestComponent_i)

/*
 * Initialize non-RH members
 */
RFNoC_TestComponent_i::RFNoC_TestComponent_i(const char *uuid, const char *label) :
    RFNoC_TestComponent_base(uuid, label),
    blockInfoChange(NULL),
    receivedSRI(false),
    rxStreamStarted(false),
    rxThread(NULL),
    spp(512),
    txThread(NULL)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);
}

/*
 * Clean up the RF-NoC stream and block
 */
RFNoC_TestComponent_i::~RFNoC_TestComponent_i()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Stop streaming
    stopRxStream();

    // Reset the RF-NoC block
    if (this->rfnocBlock.get()) {
        this->rfnocBlock->clear();
    }

    // Release the threads if necessary
    if (this->rxThread) {
        this->rxThread->stop();
        delete this->rxThread;
    }

    if (this->txThread) {
        this->txThread->stop();
        delete this->txThread;
    }
}

/*
 * Initialize members dependent on RH properties
 */
void RFNoC_TestComponent_i::constructor()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Grab the pointer to the specified block ID
    this->rfnocBlock = this->usrp->get_block_ctrl<uhd::rfnoc::block_ctrl_base>(this->blockID);

    // Without this, there is no need to continue
    if (not this->rfnocBlock.get()) {
        LOG_FATAL(RFNoC_TestComponent_i, "Unable to retrieve RF-NoC block with ID: " << this->blockID);
        throw std::exception();
    } else {
        LOG_DEBUG(RFNoC_TestComponent_i, "Got the block: " << this->blockID);
    }

    // Set the args initially
    setArgs(this->args);

    // Register the property change listener
    this->addPropertyListener(this->args, this, &RFNoC_TestComponent_i::argsChanged);

    // Alert the persona of the block ID(s)
    if (this->blockInfoChange) {
        std::vector<BlockInfo> blocks;

        BlockInfo tmp;
        tmp.blockID = this->rfnocBlock->get_block_id();
        tmp.port = 0;

        blocks.push_back(tmp);

        this->blockInfoChange(this->_identifier, blocks);
    }

    // Add an SRI change listener
    this->dataShort_in->addStreamListener(this, &RFNoC_TestComponent_i::streamChanged);

    // Add a stream listener
    this->dataShort_out->setNewConnectListener(this, &RFNoC_TestComponent_i::newConnection);
    this->dataShort_out->setNewDisconnectListener(this, &RFNoC_TestComponent_i::newDisconnection);

    // Preallocate the vector
    this->output.resize(10000);
}

/*
 * The service function for receiving from the RF-NoC block
 */
int RFNoC_TestComponent_i::rxServiceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Perform RX, if necessary
    if (this->rxStream.get()) {
        // Don't bother doing anything until the SRI has been received
        if (not this->receivedSRI) {
            LOG_DEBUG(RFNoC_TestComponent_i, "RX Thread active but no SRI has been received");
            return NOOP;
        }

        // Recv from the block
        uhd::rx_metadata_t md;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Calling recv on the rx_stream");

        size_t num_rx_samps = this->rxStream->recv(&output.front(), output.size(), md, 3.0);

        // Check the meta data for error codes
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Timeout while streaming");
            return NOOP;
        } else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Overflow while streaming");
        } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << md.strerror());
            this->rxStreamStarted = false;
            startRxStream();
            return NOOP;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "RX Thread Requested " << output.size() << " samples");
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "RX Thread Received " << num_rx_samps << " samples");

        // Get the time stamps from the meta data
        BULKIO::PrecisionUTCTime rxTime;

        rxTime.twsec = md.time_spec.get_full_secs();
        rxTime.tfsec = md.time_spec.get_frac_secs();

        // Write the data to the output stream
        short *outputBuffer = (short *) this->output.data();

        this->dataShort_out->pushPacket(outputBuffer, this->output.size() * 2, rxTime, md.end_of_burst, this->sri.streamID._ptr);
    }

    return NORMAL;
}

/*
 * The service function for transmitting to the RF-NoC block
 */
int RFNoC_TestComponent_i::txServiceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Perform TX, if necessary
    if (this->txStream.get()) {
        // Wait on input data
        bulkio::InShortPort::DataTransferType *packet = this->dataShort_in->getPacket(bulkio::Const::BLOCKING);

        if (not packet) {
            return NOOP;
        }

        uhd::tx_metadata_t md;
        std::complex<short> *block = (std::complex<short> *) packet->dataBuffer.data();
        size_t blockSize = packet->dataBuffer.size() / 2;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "TX Thread Received " << blockSize << " samples");

        if (blockSize == 0) {
            LOG_DEBUG(RFNoC_TestComponent_i, "Skipping empty packet");
            delete packet;
            return NOOP;
        }

        // Get the timestamp to send to the RF-NoC block
        BULKIO::PrecisionUTCTime time = packet->T;

        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(time.twsec, time.tfsec);

        // Send the data
        size_t num_tx_samps = this->txStream->send(block, blockSize, md);

        if (blockSize != 0 and num_tx_samps == 0) {
            LOG_DEBUG(RFNoC_TestComponent_i, "The TX stream is no longer valid, obtaining a new one");

            retrieveTxStream();
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "TX Thread Sent " << num_tx_samps << " samples");

        // On EOS, forward to the RF-NoC block
        if (packet->EOS) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");

            // Propagate the EOS to the RF-NoC Block
            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStream->send(&empty.front(), empty.size(), md);
        }

        delete packet;
        return NORMAL;
    }

    return NOOP;
}

/*
 * Override start to call start on the RX and TX threads
 */
void RFNoC_TestComponent_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    RFNoC_TestComponent_base::start();

    if (this->rxThread) {
        startRxStream();

        this->rxThread->start();
    }

    if (this->txThread) {
        this->txThread->start();
    }
}

/*
 * Override stop to call stop on the RX and TX threads
 */
void RFNoC_TestComponent_i::stop() throw (CF::Resource::StopError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    RFNoC_TestComponent_base::stop();

    if (this->rxThread) {
        if (not this->rxThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }

        stopRxStream();
    }

    if (this->txThread) {
        if (not this->txThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }
    }
}

void RFNoC_TestComponent_i::releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    releasePorts();
    stopPropertyChangeMonitor();
    // This is a singleton shared by all code running in this process
    //redhawk::events::Manager::Terminate();
    PortableServer::POA_ptr root_poa = ossie::corba::RootPOA();
    PortableServer::ObjectId_var oid = root_poa->servant_to_id(this);
    root_poa->deactivate_object(oid);

    component_running.signal();
}

/*
 * A method which allows a callback to be set for the block ID changing. This
 * callback should point back to the persona to alert it of the component's
 * block IDs
 */
void RFNoC_TestComponent_i::setBlockInfoCallback(blockInfoCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    this->blockInfoChange = cb;
}

void RFNoC_TestComponent_i::setNewIncomingConnectionCallback(connectionCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->newIncomingConnectionCallback = cb;
}

void RFNoC_TestComponent_i::setNewOutgoingConnectionCallback(connectionCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->newOutgoingConnectionCallback = cb;
}

void RFNoC_TestComponent_i::setRemovedIncomingConnectionCallback(connectionCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->removedIncomingConnectionCallback = cb;
}

void RFNoC_TestComponent_i::setRemovedOutgoingConnectionCallback(connectionCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->removedOutgoingConnectionCallback = cb;
}

/*
 * A method which allows the persona to set this component as an RX streamer.
 * This means the component should retrieve the data from block and then send
 * it out as bulkio data.
 */
void RFNoC_TestComponent_i::setRxStreamer(bool enable)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (enable) {
        // Don't create an RX stream if it already exists
        if (this->rxStream.get()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set RX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set RX streamer");

        // Get the RX stream
        retrieveRxStream();

        // Create the receive buffer
        this->output.resize(10*spp);

        // Create the RX receive thread
        this->rxThread = new GenericThreadedComponent(boost::bind(&RFNoC_TestComponent_i::rxServiceFunction, this));

        // If the component is already started, then start the RX receive thread
        if (this->_started) {
            startRxStream();

            this->rxThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->rxStream.get()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset RX streamer, but not streaming");
            return;
        }

        // Stop and delete the RX stream thread
        if (not this->rxThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }

        // Stop continuous streaming
        stopRxStream();

        // Release the RX stream pointer
        this->rxStream.reset();

        delete this->rxThread;
        this->rxThread = NULL;
    }
}

/*
 * A method which allows the persona to set this component as a TX streamer.
 * This means the component should retrieve the data from the bulkio port and
 *  then send it to the block.
 */
void RFNoC_TestComponent_i::setTxStreamer(bool enable)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (enable) {
        // Don't create a TX stream if it already exists
        if (this->txStream.get()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set TX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set TX streamer");

        // Get the TX stream
        retrieveTxStream();

        // Create the TX transmit thread
        this->txThread = new GenericThreadedComponent(boost::bind(&RFNoC_TestComponent_i::txServiceFunction, this));

        // If the component is already started, then start the TX transmit thread
        if (this->_started) {
            this->txThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->txStream.get()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset TX streamer, but not streaming");
            return;
        }

        // Stop and delete the TX stream thread
        if (not this->txThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }

        // Release the TX stream pointer
        this->txStream.reset();

        delete this->txThread;
        this->txThread = NULL;
    }
}

/*
 * A method which allows the persona to set the USRP it is using.
 */
void RFNoC_TestComponent_i::setUsrp(uhd::device3::sptr usrp)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Save the USRP for later
    this->usrp = usrp;

    // Without a valid USRP, this component can't do anything
    if (not usrp.get()) {
        LOG_FATAL(RFNoC_TestComponent_i, "Received a USRP which is not RF-NoC compatible.");
        throw std::exception();
    }
}

/*
 * The property change listener for the args property.
 */
void RFNoC_TestComponent_i::argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (not setArgs(this->args)) {
        LOG_WARN(RFNoC_TestComponent_i, "Unable to set new arguments, reverting");
        this->args = oldValue;
    }
}

void RFNoC_TestComponent_i::streamChanged(bulkio::InShortPort::StreamType stream)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    std::map<std::string, bool>::iterator it = this->streamMap.find(stream.streamID());

    bool newIncomingConnection = (it == this->streamMap.end());
    bool removedIncomingConnection =(it != this->streamMap.end() and stream.eos());

    if (newIncomingConnection) {
        boost::this_thread::sleep(1);

        if (this->newIncomingConnectionCallback) {
            this->newIncomingConnectionCallback(stream.streamID());
        }
    } else if (removedIncomingConnection) {
        if (this->removedOutgoingConnectionCallback) {
            this->removedOutgoingConnectionCallback(stream.streamID());
        }
    }

    this->sri = stream.sri();

    // Default to complex
    this->sri.mode = 1;

    this->dataShort_out->pushSRI(this->sri);

    this->receivedSRI = true;
}

void RFNoC_TestComponent_i::newConnection(const char *connectionID)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (this->newOutgoingConnectionCallback) {
        this->newOutgoingConnectionCallback(connectionID);
    }
}

void RFNoC_TestComponent_i::newDisconnection(const char *connectionID)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (this->removedOutgoingConnectionCallback) {
        this->removedOutgoingConnectionCallback(connectionID);
    }
}

void RFNoC_TestComponent_i::retrieveRxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Release the old stream if necessary
    if (this->rxStream.get()) {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Releasing old RX stream");
        this->rxStream.reset();
    }

    // Set the stream arguments
    // Only support short complex for now
    uhd::stream_args_t stream_args("sc16", "sc16");
    uhd::device_addr_t streamer_args;

    streamer_args["block_id"] = this->blockID;

    // Get the spp from the block
    this->spp = this->rfnocBlock->get_args().cast<size_t>("spp", 512);

    streamer_args["spp"] = boost::lexical_cast<std::string>(this->spp);

    stream_args.args = streamer_args;

    LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

    // Retrieve the RX stream as specified from the device 3
    try {
        this->rxStream = this->usrp->get_rx_stream(stream_args);
    } catch(uhd::runtime_error &e) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to retrieve RX stream: " << e.what());
    } catch(...) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Unexpected error occurred while retrieving RX stream");
    }
}

void RFNoC_TestComponent_i::retrieveTxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Release the old stream if necessary
    if (this->txStream.get()) {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Releasing old TX stream");
        this->txStream.reset();
    }

    // Set the stream arguments
    // Only support short complex for now
    uhd::stream_args_t stream_args("sc16", "sc16");
    uhd::device_addr_t streamer_args;

    streamer_args["block_id"] = this->blockID;

    // Get the spp from the block
    this->spp = this->rfnocBlock->get_args().cast<size_t>("spp", 512);

    streamer_args["spp"] = boost::lexical_cast<std::string>(this->spp);

    stream_args.args = streamer_args;

    LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

    // Retrieve the TX stream as specified from the device 3
    try {
        this->txStream = this->usrp->get_tx_stream(stream_args);
    } catch(uhd::runtime_error &e) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to retrieve TX stream: " << e.what());
    } catch(...) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Unexpected error occurred while retrieving TX stream");
    }
}

void RFNoC_TestComponent_i::startRxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (not this->rxStreamStarted) {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStream->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = true;
    }
}

void RFNoC_TestComponent_i::stopRxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (this->rxStreamStarted) {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = false;
    }
}

/*
 * A helper method for setting arguments on the RF-NoC block.
 */
bool RFNoC_TestComponent_i::setArgs(std::vector<arg_struct> &newArgs)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // This should never be necessary, but just in case...
    if (not this->rfnocBlock.get()) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Unable to set new arguments, RF-NoC block is not set");
        return false;
    }

    // Iterate over each argument, keeping track of the indices whose values
    // were rejected by the block
    std::vector<size_t> invalidIndices;

    for (size_t i = 0; i < newArgs.size(); ++i) {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << newArgs[i].id << ": " << newArgs[i].value);

        this->rfnocBlock->set_arg(newArgs[i].id, newArgs[i].value);

        if (this->rfnocBlock->get_arg(newArgs[i].id) != newArgs[i].value) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << newArgs[i].id << " to " << newArgs[i].value);
            invalidIndices.push_back(i);
        }
    }

    // Iterate over the invalid indices in reverse order to remove the invalid
    // arguments
    for (std::vector<size_t>::reverse_iterator i = invalidIndices.rbegin(); i != invalidIndices.rend(); ++i) {
        this->args.erase(this->args.begin() + *i);
    }

    return true;
}
