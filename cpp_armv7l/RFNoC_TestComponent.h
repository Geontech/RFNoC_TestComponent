#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

#include "RFNoC_TestComponent_base.h"

#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>
#include <uhd/device3.hpp>

#include "BlockID.h"

class RFNoC_TestComponent_i : public RFNoC_TestComponent_base
{
    ENABLE_LOGGING
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

        void constructor();

        void start() throw (CF::Resource::StartError, CORBA::SystemException);
        int serviceFunction();

        void setBlockIDCallback(blockIDCallback cb);
        void setUsrp(uhd::device3::sptr usrp);

    private:
        void argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue);

    private:
        bool setArgs(std::vector<arg_struct> &newArgs);

    private:
        blockIDCallback blockIDChange;
        bulkio::OutShortStream outShortStream;
        uhd::rfnoc::block_ctrl_base::sptr rfnocBlock;
        uhd::rx_streamer::sptr rxStream;
        BULKIO::StreamSRI sri;
        uhd::tx_streamer::sptr txStream;
        uhd::device3::sptr usrp;
};

#endif // RFNOC_TESTCOMPONENT_I_IMPL_H
