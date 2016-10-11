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
}

RFNoC_TestComponent_i::~RFNoC_TestComponent_i()
{
    if (this->rfnocBlock) {
        this->rfnocBlock->clear();
    }
}

void RFNoC_TestComponent_i::constructor()
{
    /***********************************************************************************
     This is the RH constructor. All properties are properly initialized before this function is called 
    ***********************************************************************************/
    this->rfnocBlock = this->usrp->get_device3()->find_block_ctrl(this->blockID);

    if (not this->rfnocBlock) {
        LOG_FATAL(RFNoC_TestComponent_i, "Unable to retrieve RF-NoC block with ID: " << this->blockID);
        throw std::exception();
    } else {
        LOG_INFO(RFNoC_TestComponent_i, "Got the block: " << this->blockID);
    }

    setArgs(this->args);

    this->addPropertyChangeListener("args", this, &RFNoC_TestComponent_i::argsChanged);
}

void RFNoC_TestComponent_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Started");

    bool wasStarted = this->_started;

    RFNoC_TestComponent_base::start();

    if (not wasStarted) {
        LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Pushing SRI");

        BULKIO::StreamSRI sri;

        redhawk::PropertyMap &tmp = redhawk::PropertyMap::cast(sri.keywords);
        tmp["RF-NoC_Block_ID"] = this->blockID;

        this->dataShort_out->pushSRI(sri);

        this->firstPass = true;
        this->secondPass = false;
    }
}

/***********************************************************************************************

    Basic functionality:

        The service function is called by the serviceThread object (of type ProcessThread).
        This call happens immediately after the previous call if the return value for
        the previous call was NORMAL.
        If the return value for the previous call was NOOP, then the serviceThread waits
        an amount of time defined in the serviceThread's constructor.
        
    SRI:
        To create a StreamSRI object, use the following code:
                std::string stream_id = "testStream";
                BULKIO::StreamSRI sri = bulkio::sri::create(stream_id);

    Time:
        To create a PrecisionUTCTime object, use the following code:
                BULKIO::PrecisionUTCTime tstamp = bulkio::time::utils::now();

        
    Ports:

        Data is passed to the serviceFunction through by reading from input streams
        (BulkIO only). The input stream class is a port-specific class, so each port
        implementing the BulkIO interface will have its own type-specific input stream.
        UDP multicast (dataSDDS and dataVITA49) and string-based (dataString, dataXML and
        dataFile) do not support streams.

        The input stream from which to read can be requested with the getCurrentStream()
        method. The optional argument to getCurrentStream() is a floating point number that
        specifies the time to wait in seconds. A zero value is non-blocking. A negative value
        is blocking.  Constants have been defined for these values, bulkio::Const::BLOCKING and
        bulkio::Const::NON_BLOCKING.

        More advanced uses of input streams are possible; refer to the REDHAWK documentation
        for more details.

        Input streams return data blocks that automatically manage the memory for the data
        and include the SRI that was in effect at the time the data was received. It is not
        necessary to delete the block; it will be cleaned up when it goes out of scope.

        To send data using a BulkIO interface, create an output stream and write the
        data to it. When done with the output stream, the close() method sends and end-of-
        stream flag and cleans up.

        NOTE: If you have a BULKIO dataSDDS or dataVITA49  port, you must manually call 
              "port->updateStats()" to update the port statistics when appropriate.

        Example:
            // This example assumes that the component has two ports:
            //  An input (provides) port of type bulkio::InShortPort called dataShort_in
            //  An output (uses) port of type bulkio::OutFloatPort called dataFloat_out
            // The mapping between the port and the class is found
            // in the component base class header file
            // The component class must have an output stream member; add to
            // RFNoC_TestComponent.h:
            //   bulkio::OutFloatStream outputStream;

            bulkio::InShortStream inputStream = dataShort_in->getCurrentStream();
            if (!inputStream) { // No streams are available
                return NOOP;
            }

            bulkio::ShortDataBlock block = inputStream.read();
            if (!block) { // No data available
                // Propagate end-of-stream
                if (inputStream.eos()) {
                   outputStream.close();
                }
                return NOOP;
            }

            short* inputData = block.data();
            std::vector<float> outputData;
            outputData.resize(block.size());
            for (size_t index = 0; index < block.size(); ++index) {
                outputData[index] = (float) inputData[index];
            }

            // If there is no output stream open, create one
            if (!outputStream) {
                outputStream = dataFloat_out->createStream(block.sri());
            } else if (block.sriChanged()) {
                // Update output SRI
                outputStream.sri(block.sri());
            }

            // Write to the output stream
            outputStream.write(outputData, block.getTimestamps());

            // Propagate end-of-stream
            if (inputStream.eos()) {
              outputStream.close();
            }

            return NORMAL;

        If working with complex data (i.e., the "mode" on the SRI is set to
        true), the data block's complex() method will return true. Data blocks
        provide functions that return the correct interpretation of the data
        buffer and number of complex elements:

            if (block.complex()) {
                std::complex<short>* data = block.cxdata();
                for (size_t index = 0; index < block.cxsize(); ++index) {
                    data[index] = std::abs(data[index]);
                }
                outputStream.write(data, block.cxsize(), bulkio::time::utils::now());
            }

        Interactions with non-BULKIO ports are left up to the component developer's discretion
        
    Messages:
    
        To receive a message, you need (1) an input port of type MessageEvent, (2) a message prototype described
        as a structure property of kind message, (3) a callback to service the message, and (4) to register the callback
        with the input port.
        
        Assuming a property of type message is declared called "my_msg", an input port called "msg_input" is declared of
        type MessageEvent, create the following code:
        
        void RFNoC_TestComponent_i::my_message_callback(const std::string& id, const my_msg_struct &msg){
        }
        
        Register the message callback onto the input port with the following form:
        this->msg_input->registerMessage("my_msg", this, &RFNoC_TestComponent_i::my_message_callback);
        
        To send a message, you need to (1) create a message structure, (2) a message prototype described
        as a structure property of kind message, and (3) send the message over the port.
        
        Assuming a property of type message is declared called "my_msg", an output port called "msg_output" is declared of
        type MessageEvent, create the following code:
        
        ::my_msg_struct msg_out;
        this->msg_output->sendMessage(msg_out);

    Accessing the Application and Domain Manager:
    
        Both the Application hosting this Component and the Domain Manager hosting
        the Application are available to the Component.
        
        To access the Domain Manager:
            CF::DomainManager_ptr dommgr = this->getDomainManager()->getRef();
        To access the Application:
            CF::Application_ptr app = this->getApplication()->getRef();
    
    Properties:
        
        Properties are accessed directly as member variables. For example, if the
        property name is "baudRate", it may be accessed within member functions as
        "baudRate". Unnamed properties are given the property id as its name.
        Property types are mapped to the nearest C++ type, (e.g. "string" becomes
        "std::string"). All generated properties are declared in the base class
        (RFNoC_TestComponent_base).
    
        Simple sequence properties are mapped to "std::vector" of the simple type.
        Struct properties, if used, are mapped to C++ structs defined in the
        generated file "struct_props.h". Field names are taken from the name in
        the properties file; if no name is given, a generated name of the form
        "field_n" is used, where "n" is the ordinal number of the field.
        
        Example:
            // This example makes use of the following Properties:
            //  - A float value called scaleValue
            //  - A boolean called scaleInput
              
            if (scaleInput) {
                dataOut[i] = dataIn[i] * scaleValue;
            } else {
                dataOut[i] = dataIn[i];
            }
            
        Callback methods can be associated with a property so that the methods are
        called each time the property value changes.  This is done by calling 
        addPropertyListener(<property>, this, &RFNoC_TestComponent_i::<callback method>)
        in the constructor.

        The callback method receives two arguments, the old and new values, and
        should return nothing (void). The arguments can be passed by value,
        receiving a copy (preferred for primitive types), or by const reference
        (preferred for strings, structs and vectors).

        Example:
            // This example makes use of the following Properties:
            //  - A float value called scaleValue
            //  - A struct property called status
            
        //Add to RFNoC_TestComponent.cpp
        RFNoC_TestComponent_i::RFNoC_TestComponent_i(const char *uuid, const char *label) :
            RFNoC_TestComponent_base(uuid, label)
        {
            addPropertyListener(scaleValue, this, &RFNoC_TestComponent_i::scaleChanged);
            addPropertyListener(status, this, &RFNoC_TestComponent_i::statusChanged);
        }

        void RFNoC_TestComponent_i::scaleChanged(float oldValue, float newValue)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, "scaleValue changed from" << oldValue << " to " << newValue);
        }
            
        void RFNoC_TestComponent_i::statusChanged(const status_struct& oldValue, const status_struct& newValue)
        {
            LOG_DEBUG(RFNoC_TestComponent_i, "status changed");
        }
            
        //Add to RFNoC_TestComponent.h
        void scaleChanged(float oldValue, float newValue);
        void statusChanged(const status_struct& oldValue, const status_struct& newValue);
        

************************************************************************************************/
int RFNoC_TestComponent_i::serviceFunction()
{
    LOG_DEBUG(RFNoC_TestComponent_i, "serviceFunction() example log message");

    // Determine if the upstream component is also an RF-NoC Component
    if (this->firstPass) {
        // Clear the firstPass flag and set the secondPass flag
        this->firstPass = false;
        this->secondPass = true;

        LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Getting active SRIs");

        BULKIO::StreamSRISequence *SRIs = this->dataShort_in->activeSRIs();

        if (SRIs->length() == 0) {
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "No SRIs available");
            return NOOP;
        }

        LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Got the active SRIs, grabbing the first");

        BULKIO::StreamSRI sri = SRIs->operator [](0);

        redhawk::PropertyMap &tmp = redhawk::PropertyMap::cast(sri.keywords);

        LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Got the SRI, checking for keyword");

        if (tmp.contains("RF-NoC_Block_ID")) {
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Found RF-NoC_Block_ID keyword");

            this->upstreamBlockID = tmp["RF-NoC_Block_ID"].toString();
        } else {
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Did not find RF-NoC_Block_ID keyword");
        }

        if (this->upstreamBlockID != "") {
            try {
                this->usrp->connect(this->upstreamBlockID, this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_WARN(RFNoC_TestComponent_i, this->blockID << " failed to connect: " << this->upstreamBlockID << " -> " << this->blockID)
            }
        }

        delete SRIs;
    } else if (this->secondPass) {
        // Clear the secondPass flag
        this->secondPass = false;

        // This is the first block in the chain, initialize the TX stream
        if (this->upstreamBlockID == "") {
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Host -> " << this->blockID);

            try {
                this->usrp->set_tx_channel(this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Error Code: " << e.code());
                LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Error Msg: " << e.what());
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
        }

        // This is the last block in the stream, initialize the RX stream
        if (this->rfnocBlock->list_downstream_nodes().size() == 0) {
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << this->blockID << " -> Host");

            try {
                this->usrp->set_rx_channel(this->blockID);
            } catch(uhd::runtime_error &e) {
                LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Error Code: " << e.code());
                LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Error Msg: " << e.what());
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
        }
    } else {
        // Perform TX, if necessary
        if (this->txStream) {
            bulkio::InShortStream inputStream = this->dataShort_in->getCurrentStream(0.0);

            if (not inputStream) {
                return NOOP;
            }

            bulkio::ShortDataBlock block = inputStream.read();

            if (not block) {
                if (inputStream.eos()) {
                    LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "EOS");
                }

                return NOOP;
            }

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Received " << block.size() << " samples");

            uhd::tx_metadata_t md;
            std::vector<std::complex<short> > out;
            out.assign(block.cxdata(), block.cxdata() + block.cxsize());

            std::list<bulkio::SampleTimestamp> timestamps = block.getTimestamps();

            md.has_time_spec = true;
            md.time_spec = uhd::time_spec_t(timestamps.front().time.twsec, timestamps.front().time.tfsec);

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Copied data to vector");
            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Output vector is of size: " << out.size());

            this->txStream->send(&out.front(), out.size(), md);

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Sent data");
        }

        // Perform RX, if necessary
        if (this->rxStream) {
            uhd::rx_metadata_t md;
            std::vector<std::complex<short> > output;

            output.resize(1000);

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Calling recv on the rx_stream");

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

            LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Received " << num_rx_samps << " samples");

            if (not this->outShortStream) {
                LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << "Created an output stream");
                this->outShortStream = this->dataShort_out->createStream("my_stream_yo");
                this->outShortStream.complex(true);
            }

            BULKIO::PrecisionUTCTime rxTime;

            rxTime.twsec = md.time_spec.get_full_secs();
            rxTime.tfsec = md.time_spec.get_frac_secs();

            this->outShortStream.write(output.data(), num_rx_samps, rxTime);
        }
    }

    return NORMAL;
}

void RFNoC_TestComponent_i::setUsrp(uhd::usrp::multi_usrp::sptr usrp)
{
    LOG_INFO(RFNoC_TestComponent_i, __PRETTY_FUNCTION__);
    this->usrp = usrp;

    if (not usrp or not usrp->is_device3()) {
        LOG_FATAL(RFNoC_TestComponent_i, "Received a USRP which is not RF-NoC compatible.");
        throw std::exception();
    }
}

void RFNoC_TestComponent_i::argsChanged(const std::vector<arg_struct> *oldValue, const std::vector<arg_struct> *newValue)
{
    if (not setArgs(this->args)) {
        LOG_WARN(RFNoC_TestComponent_i, "Unable to set new arguments, reverting");
        this->args = *oldValue;
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
        LOG_INFO(RFNoC_TestComponent_i, this->blockID << ": " << newArgs[i].id << "(" << newArgs[i].type << "): " << newArgs[i].value);

        this->rfnocBlock->set_arg(newArgs[i].id, newArgs[i].value);

        if (this->rfnocBlock->get_arg(newArgs[i].id) != newArgs[i].value) {
            LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << newArgs[i].id << "(" << newArgs[i].type << ") to " << newArgs[i].value);
            invalidIndices.push_back(i);
        }

        /*std::stringstream ss;

        ss << this->args[i].value;

        switch (this->args[i].type) {
            case "INT": {
                int value;

                ss >> value;

                this->rfnocBlock->set_arg<int>(this->args[i].id, int(value));

                if (this->rfnocBlock->get_arg<int>(this->args[i].id) != int(value)) {
                    LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << this->args[i].id << "(" << this->args[i].type << ") to " << this->args[i].value);
                    invalidIndices.push_back(i);
                }
                break;
            }

            case "DOUBLE": {
                double value;

                ss >> value;

                this->rfnocBlock->set_arg<double>(this->args[i].id, double(value));

                if (this->rfnocBlock->get_arg<double>(this->args[i].id) != double(value)) {
                    LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << this->args[i].id << "(" << this->args[i].type << ") to " << this->args[i].value);
                    invalidIndices.push_back(i);
                }

                break;
            }

            case "STRING": {
                std::string value = this->args[i].value;

                this->rfnocBlock->set_arg<>(this->args[i].id, int(value));

                if (this->rfnocBlock->get_arg<int>(this->args[i].id) != int(value)) {
                    LOG_WARN(RFNoC_TestComponent_i, this->blockID << ": " << "Failed to set " << this->args[i].id << "(" << this->args[i].type << ") to " << this->args[i].value);
                    invalidIndices.push_back(i);
                }

                break;
            }

            default: {
                LOG_WARN(RFNoC_TestComponent_i, "argument type must be: INT, DOUBLE, STRING");
                invalidIndices.push_back(i);
            }
        }*/
    }

    for (std::vector<size_t>::reverse_iterator i = invalidIndices.rbegin(); i != invalidIndices.rend(); ++i) {
        this->args.erase(this->args.begin() + *i);
    }

    return true;
}
