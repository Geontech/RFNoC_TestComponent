#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

#include "RFNoC_TestComponent_base.h"

#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/usrp/multi_usrp.hpp>

class RFNoC_TestComponent_i : public RFNoC_TestComponent_base
{
    ENABLE_LOGGING
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

        void constructor();

        int serviceFunction();

        void setUsrp(uhd::usrp::multi_usrp::sptr usrp);

    private:
        uhd::usrp::multi_usrp::sptr usrp;
        uhd::rfnoc::block_ctrl_base::sptr rfnocBlock;
};

#endif // RFNOC_TESTCOMPONENT_I_IMPL_H
