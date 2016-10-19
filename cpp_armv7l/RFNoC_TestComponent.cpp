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
    firstPass(true),
    secondPass(false)
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

    // Reset the channel definitions
    /*if (not this->originalRxChannel.empty()) {
        try {
            this->usrp->set_rx_channel(this->originalRxChannel);
        } catch(...) {
            LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "An error occurred while trying to reset the RX channel");
        }
    }

    if (not this->originalTxChannel.empty()) {
        try {
            this->usrp->set_tx_channel(this->originalTxChannel);
        } catch(...) {
            LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "An error occurred while trying to reset the TX channel");
        }
    }*/
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
}

void RFNoC_TestComponent_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    bool wasStarted = this->_started;

    RFNoC_TestComponent_base::start();

    // Push an initial SRI with this block ID
    if (not wasStarted) {
        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Pushing SRI");

        redhawk::PropertyMap &tmp = redhawk::PropertyMap::cast(this->sri.keywords);
        tmp["RF-NoC_Block_ID"] = this->blockID;

        this->dataShort_out->pushSRI(this->sri);

        this->firstPass = true;
        this->secondPass = false;
    }
}

int RFNoC_TestComponent_i::serviceFunction()
{
    LOG_TRACE(RFNoC_TestComponent_i, this->blockID << ": " << __PRETTY_FUNCTION__);

    // Determine if the upstream component is also an RF-NoC Component
    if (this->firstPass) {
        // Clear the firstPass flag and set the secondPass flag
        this->firstPass = false;
        this->secondPass = true;

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Getting active SRIs");

        BULKIO::StreamSRISequence *SRIs = this->dataShort_in->activeSRIs();

        if (SRIs->length() == 0) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "No SRIs available");
            return NOOP;
        }

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Got the active SRIs, grabbing the first");

        BULKIO::StreamSRI upstreamSri = SRIs->operator [](0);

        redhawk::PropertyMap &tmp = redhawk::PropertyMap::cast(upstreamSri.keywords);

        LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Got the SRI, checking for keyword");

        if (tmp.contains("RF-NoC_Block_ID")) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Found RF-NoC_Block_ID keyword");

            this->upstreamBlockID = tmp["RF-NoC_Block_ID"].toString();
        } else {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Did not find RF-NoC_Block_ID keyword");
        }

        // Connect only if an upstream block ID was found
        if (this->upstreamBlockID != "") {
            try {
                this->rxGraph->connect(this->upstreamBlockID, this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_WARN(RFNoC_TestComponent_i, this->blockID << ":" << " failed to connect: " << this->upstreamBlockID << " -> " << this->blockID)
            }
        }

        // Clean up the SRIs
        delete SRIs;
    } else if (this->secondPass) {
        // Clear the secondPass flag
        this->secondPass = false;

        // This is the first block in the chain, initialize the TX stream
        /*if (this->upstreamBlockID == "") {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << "Host -> " << this->blockID);

            this->originalTxChannel = this->usrp->get_tx_channel_id(0).get();

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Original TX Channel: " << this->originalTxChannel);

            try {
                this->usrp->set_tx_channel(this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Error Code: " << e.code());
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Error Msg: " << e.what());
            } catch(...) {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "An unexpected exception occurred");
                return FINISH;
            }

            uhd::stream_args_t stream_args("sc16", "sc16");

            this->txStream = this->usrp->get_tx_stream(stream_args);

            if (not this->txStream) {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to create TX Streamer");
                return FINISH;
            }
        }*/

        // This is the last block in the stream, initialize the RX stream
        /*if (this->rfnocBlock->list_downstream_nodes().size() == 0) {
            LOG_DEBUG(RFNoC_TestComponent_i, this->blockID << ": " << this->blockID << " -> Host");

            uhd::property_tree::sptr tree = this->usrp->get_device3()->get_tree();

            this->originalRxChannel = this->usrp->get_rx_channel_id(0).get();

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Original RX Channel: " << this->originalRxChannel);

            try {
                this->usrp->set_rx_channel(this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Error Code: " << e.code());
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "Error Msg: " << e.what());
            } catch(...) {
                LOG_ERROR(RFNoC_TestComponent_i, this->blockID << ": " << "An unexpected exception occurred");
                return FINISH;
            }

            uhd::stream_args_t stream_args("sc16", "sc16");

            this->rxStream = this->usrp->get_rx_stream(stream_args);

            uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            stream_cmd.num_samps = 0;
            stream_cmd.stream_now = true;
            stream_cmd.time_spec = uhd::time_spec_t();

            this->rxStream->issue_stream_cmd(stream_cmd);
        }*/
    } else {
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
        }
    }

    return NORMAL;
}

void RFNoC_TestComponent_i::setUsrp(uhd::device3::sptr usrp)
{
    LOG_DEBUG(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);

    this->usrp = usrp;

    if (not usrp) {
        LOG_FATAL(RFNoC_TestComponent_i, "Received a USRP which is not RF-NoC compatible.");
        throw std::exception();
    }

    this->rxGraph = this->usrp->create_graph("default");
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
