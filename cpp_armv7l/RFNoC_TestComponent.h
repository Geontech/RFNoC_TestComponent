#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

#include "RFNoC_TestComponent_base.h"

#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>
#include <uhd/device3.hpp>

#include "GenericThreadedComponent.h"
#include "RFNoC_Component.h"

/*
 * The class for the component
 */
class RFNoC_TestComponent_i : public RFNoC_TestComponent_base, public RFNoC_ComponentInterface
{
    ENABLE_LOGGING
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

        void constructor();

        // Service functions for RX and TX
        int rxServiceFunction();
        int txServiceFunction();

        // Don't use the default serviceFunction for clarity
        int serviceFunction() { return FINISH; }

        // Override start and stop
        void start() throw (CF::Resource::StartError, CORBA::SystemException);
        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

        // Override releaseObject
        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        // Methods to be called by the persona, inherited from RFNoC_ComponentInterface
        void setBlockIDCallback(blockIDCallback cb);
        void setRxStreamer(bool enable);
        void setTxStreamer(bool enable);
        void setUsrp(uhd::device3::sptr usrp);

    private:
        // Property change listeners
        void argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue);

        // Stream listeners
        void streamChanged(bulkio::InShortPort::StreamType stream);

    private:
        void retrieveRxStream();
        void retrieveTxStream();

        // Internal method for setting the arguments on the block
        bool setArgs(std::vector<arg_struct> &newArgs);

    private:
        blockIDCallback blockIDChange;
        std::vector<std::complex<short> > output;
        bool receivedSRI;
        uhd::rfnoc::block_ctrl_base::sptr rfnocBlock;
        uhd::rx_streamer::sptr rxStream;
        GenericThreadedComponent *rxThread;
        size_t spp;
        BULKIO::StreamSRI sri;
        uhd::tx_streamer::sptr txStream;
        GenericThreadedComponent *txThread;
        uhd::device3::sptr usrp;
};

#endif // RFNOC_TESTCOMPONENT_I_IMPL_H
