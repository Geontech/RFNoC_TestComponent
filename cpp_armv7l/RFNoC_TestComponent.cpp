/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

**************************************************************************/

#include "RFNoC_TestComponent.h"

/*
 * Initialize the service function callback
 */
GenericThreadedComponent::GenericThreadedComponent(serviceFunction_t sf) :
    serviceFunctionMethod(sf)
{
}

/*
 * Call the service function callback
 */
int GenericThreadedComponent::serviceFunction()
{
    return this->serviceFunctionMethod();
}

/*
 * Start the thread
 */
void GenericThreadedComponent::start()
{
    this->startThread();
}

/*
 * Stop the thread
 */
bool GenericThreadedComponent::stop()
{
    return this->stopThread();
}

PREPARE_LOGGING(RFNoC_TestComponent_i)

/*
 * Initialize non-RH members
 */
RFNoC_TestComponent_i::RFNoC_TestComponent_i(const char *uuid, const char *label) :
    RFNoC_TestComponent_base(uuid, label),
    blockIDChange(NULL),
    rxThread(NULL),
    spp(1024),
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
    if (this->rxStream) {
        uhd::stream_cmd_t streamCmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(streamCmd);
    }

    // Reset the RF-NoC block
    if (this->rfnocBlock) {
        this->rfnocBlock->clear();
    }

    // Release the threads if necessary
    if (this->rxThread) {
        delete this->rxThread;
    }

    if (this->txThread) {
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
    if (not this->rfnocBlock) {
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
    if (this->blockIDChange) {
        std::vector<uhd::rfnoc::block_id_t> blocks(1, this->blockID);
        this->blockIDChange(this->_identifier, blocks);
    }
}

/*
 * The service function for receiving from the RF-NoC block
 */
int RFNoC_TestComponent_i::rxServiceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Perform RX, if necessary
    if (this->rxStream) {
        // Recv from the block
        uhd::rx_metadata_t md;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Calling recv on the rx_stream");

        size_t num_rx_samps = this->rxStream->recv(&output.front(), output.size(), md, 1.0);

        // Check the meta data for error codes
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Timeout while streaming");
            return NOOP;
        } else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Overflow while streaming");
        } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << md.strerror());
            return NOOP;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Received " << num_rx_samps << " samples");

        // Create the output stream
        if (not this->outShortStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Created an output stream");
            this->outShortStream = this->dataShort_out->createStream("my_stream_yo");
            this->outShortStream.complex(true);
        }

        // Get the time stamps from the meta data
        BULKIO::PrecisionUTCTime rxTime;

        rxTime.twsec = md.time_spec.get_full_secs();
        rxTime.tfsec = md.time_spec.get_frac_secs();

        // Write the data to the output stream
        this->outShortStream.write(output.data(), num_rx_samps, rxTime);

        // Respond to an end of burst
        /*if (md.end_of_burst) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOB");

            this->outShortStream.close();
        }*/
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
    if (this->txStream) {
        // Wait on input data
        bulkio::InShortStream inputStream = this->dataShort_in->getCurrentStream(bulkio::Const::BLOCKING);

        if (not inputStream) {
            return NOOP;
        }

        // Get the block from the input stream
        bulkio::ShortDataBlock block = inputStream.read();
        uhd::tx_metadata_t md;

        // The stream was valid but no data was received
        if (not block) {
            // On EOS, forward to the RF-NoC block
            if (inputStream.eos()) {
                LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");

                // Propagate the EOS to the RF-NoC Block
                md.end_of_burst = true;

                std::vector<std::complex<short> > empty;
                this->txStream->send(&empty.front(), empty.size(), md);
            }

            return NOOP;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Received " << block.size() << " samples");

        // Get the timestamps to send to the RF-NoC block
        std::list<bulkio::SampleTimestamp> timestamps = block.getTimestamps();

        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(timestamps.front().time.twsec, timestamps.front().time.tfsec);

        // Assign the data to the output vector
        output.assign(block.cxdata(), block.cxdata() + block.cxsize());

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Copied data to vector");
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Output vector is of size: " << output.size());

        // Send the data to the RF-NoC block
        this->txStream->send(&output.front(), output.size(), md);

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Sent data");

        // On EOS, forward to the RF-NoC block
        if (inputStream.eos()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");

            // Propagate the EOS to the RF-NoC Block
            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStream->send(&empty.front(), empty.size(), md);
        }
    }

    return NORMAL;
}

/*
 * Override start to call start on the RX and TX threads
 */
void RFNoC_TestComponent_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    RFNoC_TestComponent_base::start();

    if (this->rxThread) {
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

    if (this->rxThread) {
        if (this->rxThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }
    }

    if (this->txThread) {
        if (this->txThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }
    }

    RFNoC_TestComponent_base::stop();
}

/*
 * A method which allows a callback to be set for the block ID changing. This
 * callback should point back to the persona to alert it of the component's
 * block IDs
 */
void RFNoC_TestComponent_i::setBlockIDCallback(blockIDCallback cb)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    this->blockIDChange = cb;
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
        if (this->rxStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set RX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set RX streamer");

        // Set the stream arguments
        // Only support short complex for now
        uhd::stream_args_t stream_args("sc16", "sc16");
        uhd::device_addr_t streamer_args;

        streamer_args["block_id"] = this->blockID;

        // Get the spp from the block
        this->spp = this->rfnocBlock->get_args().cast<size_t>("spp", 1024);

        streamer_args["spp"] = boost::lexical_cast<std::string>(this->spp);

        this->output.resize(100*spp);

        stream_args.args = streamer_args;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

        // Retrieve the RX stream as specified from the device 3
        this->rxStream = this->usrp->get_rx_stream(stream_args);

        // Start continuous streaming immediately
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStream->issue_stream_cmd(stream_cmd);

        // Create the RX receive thread
        this->rxThread = new GenericThreadedComponent(boost::bind(&RFNoC_TestComponent_i::rxServiceFunction, this));

        // If the component is already started, then start the RX receive thread
        if (this->_started) {
            this->rxThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->rxStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset RX streamer, but not streaming");
            return;
        }

        // Stop continuous streaming
        uhd::stream_cmd_t streamCmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(streamCmd);

        // Release the RX stream pointer
        this->rxStream.reset();

        // Stop and delete the RX stream thread
        if (not this->rxThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "RX Thread had to be killed");
        }

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
        if (this->txStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set TX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set TX streamer");

        // Set the stream arguments
        // Only support short complex for now
        uhd::stream_args_t stream_args("sc16", "sc16");
        uhd::device_addr_t streamer_args;

        streamer_args["block_id"] = this->blockID;

        // Get the spp from the block
        this->spp = this->rfnocBlock->get_args().cast<size_t>("spp", 1024);

        streamer_args["spp"] = boost::lexical_cast<std::string>(this->spp);

        stream_args.args = streamer_args;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

        // Retrieve the TX stream as specified from the device 3
        this->txStream = this->usrp->get_tx_stream(stream_args);

        // Create the TX transmit thread
        this->txThread = new GenericThreadedComponent(boost::bind(&RFNoC_TestComponent_i::txServiceFunction, this));

        // If the component is already started, then start the TX transmit thread
        if (this->_started) {
            this->txThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->txStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset TX streamer, but not streaming");
            return;
        }

        // Release the TX stream pointer
        this->txStream.reset();

        // Stop and delete the TX stream thread
        if (not this->txThread->stop()) {
            LOG_WARN(RFNoC_TestComponent_i, "TX Thread had to be killed");
        }

        delete this->txThread;
        this->txThread = NULL;
    }
}

/*
 * A method which allows the persona to set the address of the USRP it is
 * using.
 */
void RFNoC_TestComponent_i::setUsrpAddress(uhd::device_addr_t usrpAddress)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Retrieve a pointer to the device
    this->usrp = uhd::device3::make(usrpAddress);

    // Save the address for later, if needed
    this->usrpAddress = usrpAddress;

    // Without a valid USRP, this component can't do anything
    if (not usrp) {
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

/*
 * A helper method for setting arguments on the RF-NoC block.
 */
bool RFNoC_TestComponent_i::setArgs(std::vector<arg_struct> &newArgs)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // This should never be necessary, but just in case...
    if (not this->rfnocBlock) {
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
