#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

#include "RFNoC_TestComponent_base.h"

#include <uhd/usrp/multi_usrp.hpp>

class RFNoC_TestComponent_i : public RFNoC_TestComponent_base
{
    ENABLE_LOGGING
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

        void constructor();

        int serviceFunction();

        void setUsrp(uhd::usrp::multi_usrp::sptr usrp) { this->usrp = usrp; LOG_INFO(RFNoC_TestComponent_i, "HERE"); /*LOG_INFO(RFNoC_TestComponent_i, this->usrp->get_pp_string());*/ }

    private:
        uhd::usrp::multi_usrp::sptr usrp;
};

#endif // RFNOC_TESTCOMPONENT_I_IMPL_H
