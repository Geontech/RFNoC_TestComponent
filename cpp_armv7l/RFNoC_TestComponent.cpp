// Class Include
#include "RFNoC_TestComponent.h"

// RF-NoC RH Utils
#include <RFNoC_Utils.h>

PREPARE_LOGGING(RFNoC_TestComponent_i)

/*
 * Constructor(s) and/or Destructor
 */

// Initialize non-RH members
RFNoC_TestComponent_i::RFNoC_TestComponent_i(const char *uuid, const char *label) :
    RFNoC_TestComponent_base(uuid, label),
    receivedSRI(false),
    rxStreamStarted(false),
    spp(512)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);
}

// Clean up the RF-NoC stream and threads
RFNoC_TestComponent_i::~RFNoC_TestComponent_i()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Stop streaming
    stopRxStream();

    // Release the threads if necessary
    if (this->rxThread)
    {
        this->rxThread->stop();
    }

    if (this->txThread)
    {
        this->txThread->stop();
    }
}

/*
 * Public Method(s)
 */

// Make sure the singleton isn't terminated
void RFNoC_TestComponent_i::releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    // This function clears the component running condition so main shuts down everything
    try
    {
        stop();
    }
    catch (CF::Resource::StopError& ex)
    {
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

// Override start to call start on the RX and TX threads
void RFNoC_TestComponent_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    RFNoC_TestComponent_base::start();

    if (this->rxThread)
    {
        startRxStream();

        this->rxThread->start();
    }

    if (this->txThread)
    {
        this->txThread->start();
    }
}

// Override stop to call stop on the RX and TX threads
void RFNoC_TestComponent_i::stop() throw (CF::Resource::StopError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    RFNoC_TestComponent_base::stop();

    if (this->rxThread)
    {
        if (not this->rxThread->stop())
        {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }

        stopRxStream();
    }

    if (this->txThread)
    {
        if (not this->txThread->stop())
        {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }
    }
}

/*
 * Public RFNoC_Component Method(s)
 */

// A method which allows the persona to set this component as an RX streamer.
// This means the component should retrieve the data from block and then send
// it out as bulkio data.
void RFNoC_TestComponent_i::setRxStreamer(uhd::rx_streamer::sptr rxStreamer)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (rxStreamer)
    {
        // This shouldn't happen
        if (this->rxStreamer)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Replacing existing RX streamer");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set RX streamer");

        // Set the RX stream
        this->rxStreamer = rxStreamer;

        // Create the RX receive thread
        this->rxThread = boost::make_shared<RFNoC_RH::GenericThreadedComponent>(boost::bind(&RFNoC_TestComponent_i::rxServiceFunction, this));

        // If the component is already started, then start the RX receive thread
        if (this->_started)
        {
            startRxStream();

            this->rxThread->start();
        }
    }
    else
    {
        // Don't clean up the stream if it's not already running
        if (not this->rxStreamer)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset RX streamer, but not streaming");
            return;
        }

        // Stop and delete the RX stream thread
        if (not this->rxThread->stop())
        {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }

        // Stop continuous streaming
        stopRxStream();

        // Release the RX stream pointer
        LOG_DEBUG(RFNoC_TestComponent_i, "Resetting RX stream");
        this->rxStreamer.reset();

        this->rxThread.reset();
    }
}

// A method which allows the persona to set this component as a TX streamer.
// This means the component should retrieve the data from the bulkio port and
// then send it to the block.
void RFNoC_TestComponent_i::setTxStreamer(uhd::tx_streamer::sptr txStreamer)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (txStreamer)
    {
        // This shouldn't happen
        if (this->txStreamer)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Replacing TX streamer");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set TX streamer");

        // Set the TX stream
        this->txStreamer = txStreamer;

        // Create the TX transmit thread
        this->txThread = boost::make_shared<RFNoC_RH::GenericThreadedComponent>(boost::bind(&RFNoC_TestComponent_i::txServiceFunction, this));

        // If the component is already started, then start the TX transmit thread
        if (this->_started)
        {
            this->txThread->start();
        }
    }
    else
    {
        // Don't clean up the stream if it's not already running
        if (not this->txStreamer)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset TX streamer, but not streaming");
            return;
        }

        // Stop and delete the TX stream thread
        if (not this->txThread->stop())
        {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }

        // Release the TX stream pointer
        this->txStreamer.reset();

        this->txThread.reset();
    }
}

/*
 * Protected Method(s)
 */

// Initialize members dependent on RH properties
void RFNoC_TestComponent_i::constructor()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Construct a BlockDescriptor
    RFNoC_RH::BlockDescriptor blockDescriptor;

    blockDescriptor.blockId = this->blockID;

    // Grab the pointer to the specified block ID
    this->rfnocBlock = this->persona->getBlock(blockDescriptor);

    // Without this, there is no need to continue
    if (not this->rfnocBlock)
    {
        LOG_FATAL(RFNoC_TestComponent_i, "Unable to retrieve RF-NoC block with ID: " << this->blockID);
        throw std::exception();
    }
    else
    {
        LOG_DEBUG(RFNoC_TestComponent_i, "Got the block: " << this->blockID);
    }

    // Set the args initially
    setArgs(this->args);

    // Alert the persona of stream descriptors for this component
    RFNoC_RH::StreamDescriptor streamDescriptor;

    streamDescriptor.cpuFormat = "sc16";
    streamDescriptor.otwFormat = "sc16";
    streamDescriptor.streamArgs["block_id"] = this->blockID;
    streamDescriptor.streamArgs["block_port"] = blockDescriptor.port;

    // Get the spp from the block
    this->spp = this->rfnocBlock->get_args().cast<size_t>("spp", 512);

    streamDescriptor.streamArgs["spp"] = boost::lexical_cast<std::string>(this->spp);

    // Register the property change listener
    this->addPropertyListener(this->args, this, &RFNoC_TestComponent_i::argsChanged);

    // Set the logger for the ports
    this->dataShort_in->setLogger(this->getLogger());
    this->dataShort_out->setLogger(this->getLogger());

    // Add an SRI change listener
    this->dataShort_in->addStreamListener(this, &RFNoC_TestComponent_i::streamChanged);

    // Add a stream listener
    this->dataShort_out->setNewConnectListener(this, &RFNoC_TestComponent_i::newConnection);
    this->dataShort_out->setNewDisconnectListener(this, &RFNoC_TestComponent_i::newDisconnection);

    // Create the receive buffer
    this->output.resize((0.8 * bulkio::Const::MAX_TRANSFER_BYTES / sizeof(std::complex<short>)));
}

// The service function for receiving from the RF-NoC block
int RFNoC_TestComponent_i::rxServiceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Perform RX, if necessary
    if (this->rxStreamer)
    {
        // Don't bother doing anything until the SRI has been received
        if (not this->receivedSRI)
        {
            LOG_TRACE(RFNoC_TestComponent_i, "RX Thread active but no SRI has been received");
            return NOOP;
        }

        // Recv from the block
        uhd::rx_metadata_t md;

        size_t samplesRead = 0;
        size_t samplesToRead = this->output.size();

        while (samplesRead < this->output.size())
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Calling recv on the rx_stream");

            size_t num_rx_samps = this->rxStreamer->recv(&output.front() + samplesRead, samplesToRead, md, 1.0);

            // Check the meta data for error codes
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
            {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Timeout while streaming");
                return NOOP;
            }
            else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
            {
                LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Overflow while streaming");
            }
            else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << md.strerror());
                this->rxStreamStarted = false;
                startRxStream();
                return NOOP;
            }

            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "RX Thread Requested " << samplesToRead << " samples");
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "RX Thread Received " << num_rx_samps << " samples");

            samplesRead += num_rx_samps;
            samplesToRead -= num_rx_samps;
        }

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

// The service function for transmitting to the RF-NoC block
int RFNoC_TestComponent_i::txServiceFunction()
{
    // Perform TX, if necessary
    if (this->txStreamer)
    {
        // Wait on input data
        bulkio::InShortPort::DataTransferType *packet = this->dataShort_in->getPacket(bulkio::Const::BLOCKING);

        if (not packet)
        {
            return NOOP;
        }

        uhd::tx_metadata_t md;
        std::complex<short> *block = (std::complex<short> *) packet->dataBuffer.data();
        size_t blockSize = packet->dataBuffer.size() / 2;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "TX Thread Received " << blockSize << " samples");

        if (blockSize == 0)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, "Skipping empty packet");
            delete packet;
            return NOOP;
        }

        // Get the timestamp to send to the RF-NoC block
        BULKIO::PrecisionUTCTime time = packet->T;

        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(time.twsec, time.tfsec);

        size_t samplesSent = 0;
        size_t samplesToSend = blockSize;

        while (samplesToSend != 0)
        {
            // Send the data
            size_t num_tx_samps = this->txStreamer->send(block + samplesSent, samplesToSend, md, 1);

            samplesSent += num_tx_samps;
            samplesToSend -= num_tx_samps;

            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "TX Thread Sent " << num_tx_samps << " samples");
        }

        // On EOS, forward to the RF-NoC block
        if (packet->EOS)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");

            // Propagate the EOS to the RF-NoC Block
            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStreamer->send(&empty.front(), empty.size(), md);
        }

        delete packet;
        return NORMAL;
    }

    return NOOP;
}

/*
 * Private Method(s)
 */

// The property change listener for the args property.
void RFNoC_TestComponent_i::argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (not setArgs(this->args))
    {
        LOG_WARN(RFNoC_TestComponent_i, "Unable to set new arguments, reverting");
        this->args = oldValue;
    }
}

void RFNoC_TestComponent_i::streamChanged(bulkio::InShortPort::StreamType stream)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    std::map<std::string, bool>::iterator it = this->streamMap.find(stream.streamID());

    bool newIncomingConnection = (it == this->streamMap.end());
    bool removedIncomingConnection = (it != this->streamMap.end() and stream.eos());

    if (newIncomingConnection)
    {
        LOG_DEBUG(RFNoC_TestComponent_i, "New incoming connection");

        this->persona->incomingConnectionAdded(this->identifier(),
        									   stream.streamID(),
											   this->dataShort_in->_this()->_hash(RFNoC_RH::HASH_SIZE));

        this->streamMap[stream.streamID()] = true;
    }
    else if (removedIncomingConnection)
    {
        LOG_DEBUG(RFNoC_TestComponent_i, "Removed incoming connection");

        this->persona->incomingConnectionRemoved(this->identifier(),
        										 stream.streamID(),
												 this->dataShort_in->_this()->_hash(RFNoC_RH::HASH_SIZE));

        this->streamMap.erase(it);
    }
    else
    {
        LOG_DEBUG(RFNoC_TestComponent_i, "Existing connection changed");
    }

    LOG_DEBUG(RFNoC_TestComponent_i, "Got SRI for stream ID: " << stream.streamID());

    this->sri = stream.sri();

    // Default to complex
    this->sri.mode = 1;

    this->dataShort_out->pushSRI(this->sri);

    LOG_DEBUG(RFNoC_TestComponent_i, "Pushed stream ID to port");

    this->receivedSRI = true;
}

void RFNoC_TestComponent_i::newConnection(const char *connectionID)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

	BULKIO::UsesPortStatisticsProvider_ptr port = BULKIO::UsesPortStatisticsProvider::_narrow(this->getPort(this->dataShort_out->getName().c_str()));

	this->persona->outgoingConnectionAdded(this->identifier(), connectionID, port->_hash(RFNoC_RH::HASH_SIZE));
}

void RFNoC_TestComponent_i::newDisconnection(const char *connectionID)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

	BULKIO::UsesPortStatisticsProvider_ptr port = BULKIO::UsesPortStatisticsProvider::_narrow(this->getPort(this->dataShort_out->getName().c_str()));

	this->persona->outgoingConnectionRemoved(this->identifier(), connectionID, port->_hash(RFNoC_RH::HASH_SIZE));
}

void RFNoC_TestComponent_i::startRxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (not this->rxStreamStarted and this->rxStreamer)
    {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStreamer->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = true;
    }
}

void RFNoC_TestComponent_i::stopRxStream()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    if (this->rxStreamStarted and this->rxStreamer)
    {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStreamer->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = false;

        // Run recv until nothing is left
        uhd::rx_metadata_t md;
        int num_post_samps = 0;

        LOG_DEBUG(RFNoC_TestComponent_i, "Emptying receive queue...");

        do
        {
            num_post_samps = this->rxStreamer->recv(&this->output.front(), this->output.size(), md, 1.0);
        } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

        LOG_DEBUG(RFNoC_TestComponent_i, "Emptied receive queue");
    }
}

// A helper method for setting arguments on the RF-NoC block.
bool RFNoC_TestComponent_i::setArgs(std::vector<arg_struct> &newArgs)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // This should never be necessary, but just in case...
    if (not this->rfnocBlock)
    {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Unable to set new arguments, RF-NoC block is not set");
        return false;
    }

    // Iterate over each argument, keeping track of the indices whose values
    // were rejected by the block
    std::vector<size_t> invalidIndices;

    for (size_t i = 0; i < newArgs.size(); ++i)
    {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << newArgs[i].id << ": " << newArgs[i].value);

        this->rfnocBlock->set_arg(newArgs[i].id, newArgs[i].value);

        if (this->rfnocBlock->get_arg(newArgs[i].id) != newArgs[i].value)
        {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << newArgs[i].id << " to " << newArgs[i].value);
            invalidIndices.push_back(i);
        }
    }

    // Iterate over the invalid indices in reverse order to remove the invalid
    // arguments
    for (std::vector<size_t>::reverse_iterator i = invalidIndices.rbegin(); i != invalidIndices.rend(); ++i)
    {
        this->args.erase(this->args.begin() + *i);
    }

    return true;
}
