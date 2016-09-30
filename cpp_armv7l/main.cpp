#include <iostream>
#include "ossie/ossieSupport.h"
#include <ossie/Device_impl.h>

#include "RFNoC_TestComponent.h"

#include <uhd/usrp/multi_usrp.hpp>

RFNoC_TestComponent_i *resourcePtr;

void signal_catcher(int sig)
{
    // IMPORTANT Don't call exit(...) in this function
    // issue all CORBA calls that you need for cleanup here before calling ORB shutdown
    if (resourcePtr) {
        resourcePtr->halt();
    }
}
int main(int argc, char* argv[])
{
    struct sigaction sa;
    sa.sa_handler = signal_catcher;
    sa.sa_flags = 0;
    resourcePtr = 0;

    //Component::start_component(&resourcePtr, argc, argv);
    Resource_impl::start_component(resourcePtr, argc, argv);
    return 0;
}

extern "C" {
    Resource_impl* construct(int argc, char* argv[], Device_impl* parentDevice, uhd::usrp::multi_usrp::sptr usrp) {

        struct sigaction sa;
        sa.sa_handler = signal_catcher;
        sa.sa_flags = 0;
        resourcePtr = 0;

        Resource_impl::start_component(resourcePtr, argc, argv);

        // Any addition parameters passed into construct can now be
        // set directly onto resourcePtr since it is the instantiated
        // Redhawk device
        //      Example:
        //         resourcePtr->setSharedAPI(sharedAPI);
        //resourcePtr->setParentDevice(parentDevice);
        std::cout << "A" << std::endl;

        if (resourcePtr) {
            std::cout << "Resource Ptr is valid, supposedly" << std::endl;
        } else {
            std::cout << "Resource Ptr is invalid" << std::endl;
            return NULL;
        }

        std::cout << resourcePtr->_identifier << std::endl;

        //resourcePtr->setUsrp(usrp);
        std::cout << usrp->get_mboard_name() << std::endl;

        std::cout << "B" << std::endl;

        return resourcePtr;
    }
}

