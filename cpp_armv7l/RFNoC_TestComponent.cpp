/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

**************************************************************************/

#include "RFNoC_TestComponent.h"

PREPARE_LOGGING(RFNoC_TestComponent_i)

RFNoC_TestComponent_i::RFNoC_TestComponent_i(const char *uuid, const char *label) :
    RFNoC_TestComponent_base(uuid, label),
    blockIDChange(NULL)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);
}

RFNoC_TestComponent_i::~RFNoC_TestComponent_i()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Reset the RF-NoC block
    if (this->rfnocBlock) {
        this->rfnocBlock->clear();
    }

    // Stop streaming
    if (this->rxStream) {
        uhd::stream_cmd_t streamCmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(streamCmd);
    }
}

void RFNoC_TestComponent_i::constructor()
{
    /***********************************************************************************
     This is the RH constructor. All properties are properly initialized before this function is called 
    ***********************************************************************************/
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

    // Alert the persona of the block ID
    if (this->blockIDChange) {
        this->blockIDChange(this->_identifier, this->blockID);
    }
}

int RFNoC_TestComponent_i::serviceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    bool dataTransceived = false;

    // Perform TX, if necessary
    if (this->txStream) {
        bulkio::InShortStream inputStream = this->dataShort_in->getCurrentStream(0.0);

        if (not inputStream) {
            return NOOP;
        }

        bulkio::ShortDataBlock block = inputStream.read();
        uhd::tx_metadata_t md;

        if (not block) {
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

        std::vector<std::complex<short> > out;
        out.assign(block.cxdata(), block.cxdata() + block.cxsize());

        std::list<bulkio::SampleTimestamp> timestamps = block.getTimestamps();

        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(timestamps.front().time.twsec, timestamps.front().time.tfsec);

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Copied data to vector");
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Output vector is of size: " << out.size());

        this->txStream->send(&out.front(), out.size(), md);

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Sent data");

        if (inputStream.eos()) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");

            // Propagate the EOS to the RF-NoC Block
            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStream->send(&empty.front(), empty.size(), md);
        }

        dataTransceived = true;
    }

    // Perform RX, if necessary
    if (this->rxStream) {
        uhd::rx_metadata_t md;
        std::vector<std::complex<short> > output;

        output.resize(5000);

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Calling recv on the rx_stream");

        size_t num_rx_samps = this->rxStream->recv(&output.front(), output.size(), md, 1.0);

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

        if (not this->outShortStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Created an output stream");
            this->outShortStream = this->dataShort_out->createStream("my_stream_yo");
            this->outShortStream.complex(true);
        }

        BULKIO::PrecisionUTCTime rxTime;

        rxTime.twsec = md.time_spec.get_full_secs();
        rxTime.tfsec = md.time_spec.get_frac_secs();

        this->outShortStream.write(output.data(), num_rx_samps, rxTime);

        if (md.end_of_burst) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "EOB");

            this->outShortStream.close();
        }

        dataTransceived = true;
    }

    if (dataTransceived) {
        return NORMAL;
    } else {
        return NOOP;
    }
}

void RFNoC_TestComponent_i::setBlockIDCallback(blockIDCallback cb)
{
    this->blockIDChange = cb;
}

void RFNoC_TestComponent_i::setRxStreamer(bool enable)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    if (enable) {
        if (this->rxStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set RX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set RX streamer");

        uhd::stream_args_t stream_args("sc16", "sc16");
        uhd::device_addr_t streamer_args;

        streamer_args["block_id"] = this->blockID;
        streamer_args["spp"] = "1024";

        stream_args.args = streamer_args;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

        this->rxStream = this->usrp->get_rx_stream(stream_args);

        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStream->issue_stream_cmd(stream_cmd);
    } else {
        if (not this->rxStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset RX streamer, but not streaming");
            return;
        }

        uhd::stream_cmd_t streamCmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(streamCmd);

        this->rxStream.reset();
    }
}

void RFNoC_TestComponent_i::setTxStreamer(bool enable)
{
    LOG_TRACE(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    if (enable) {
        if (this->txStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to set TX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempting to set TX streamer");

        uhd::stream_args_t stream_args("sc16", "sc16");
        uhd::device_addr_t streamer_args;

        streamer_args["block_id"] = this->blockID;
        streamer_args["spp"] = "1024";

        stream_args.args = streamer_args;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Using streamer arguments: " << stream_args.args.to_string());

        this->txStream = this->usrp->get_tx_stream(stream_args);
    } else {
        if (not this->txStream) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Attempted to unset TX streamer, but not streaming");
            return;
        }

        this->txStream.reset();
    }
}

void RFNoC_TestComponent_i::setUsrp(uhd::device3::sptr usrp)
{
    LOG_DEBUG(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->usrp = usrp;

    if (not usrp) {
        LOG_FATAL(RFNoC_TestComponent_i, "Received a USRP which is not RF-NoC compatible.");
        throw std::exception();
    }
}

void RFNoC_TestComponent_i::argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue)
{
    if (not setArgs(this->args)) {
        LOG_WARN(RFNoC_TestComponent_i, "Unable to set new arguments, reverting");
        this->args = oldValue;
    }
}

bool RFNoC_TestComponent_i::setArgs(std::vector<arg_struct> &newArgs)
{
    if (not this->rfnocBlock) {
        LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Unable to set new arguments, RF-NoC block is not set");
        return false;
    }

    std::vector<size_t> invalidIndices;

    for (size_t i = 0; i < newArgs.size(); ++i) {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << newArgs[i].id << ": " << newArgs[i].value);

        this->rfnocBlock->set_arg(newArgs[i].id, newArgs[i].value);

        if (this->rfnocBlock->get_arg(newArgs[i].id) != newArgs[i].value) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << newArgs[i].id << " to " << newArgs[i].value);
            invalidIndices.push_back(i);
        }
    }

    for (std::vector<size_t>::reverse_iterator i = invalidIndices.rbegin(); i != invalidIndices.rend(); ++i) {
        this->args.erase(this->args.begin() + *i);
    }

    return true;
}
