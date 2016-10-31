#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

#include "RFNoC_TestComponent_base.h"

#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>
#include <uhd/device3.hpp>

#include "RFNoC_Component.h"

class RFNoC_TestComponent_i : public RFNoC_TestComponent_base
{
    ENABLE_LOGGING
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

        void constructor();

        int serviceFunction();

        void setBlockIDCallback(blockIDCallback cb);
        void setRxStreamer(bool enable);
        void setTxStreamer(bool enable);
        void setUsrpAddress(uhd::device_addr_t usrpAddress);

    private:
        void argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue);

    private:
        bool setArgs(std::vector<arg_struct> &newArgs);

    private:
        blockIDCallback blockIDChange;
        std::vector<std::complex<short> > output;
        bulkio::OutShortStream outShortStream;
        uhd::rfnoc::block_ctrl_base::sptr rfnocBlock;
        uhd::rx_streamer::sptr rxStream;
        size_t spp;
        BULKIO::StreamSRI sri;
        uhd::tx_streamer::sptr txStream;
        uhd::device3::sptr usrp;
        uhd::device_addr_t usrpAddress;
};

#endif // RFNOC_TESTCOMPONENT_I_IMPL_H
