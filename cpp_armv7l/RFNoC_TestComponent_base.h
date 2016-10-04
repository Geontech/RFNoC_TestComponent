#ifndef RFNOC_TESTCOMPONENT_BASE_IMPL_BASE_H
#define RFNOC_TESTCOMPONENT_BASE_IMPL_BASE_H

#include <boost/thread.hpp>
#include <ossie/Component.h>
#include <ossie/ThreadedComponent.h>

#include <bulkio/bulkio.h>

class RFNoC_TestComponent_base : public Component, protected ThreadedComponent
{
    public:
        RFNoC_TestComponent_base(const char *uuid, const char *label);
        ~RFNoC_TestComponent_base();

        void start() throw (CF::Resource::StartError, CORBA::SystemException);

        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        void loadProperties();

    protected:
        // Member variables exposed as properties
        /// Property: blockID
        std::string blockID;

        // Ports
        /// Port: dataShort_in
        bulkio::InShortPort *dataShort_in;
        /// Port: dataShort_out
        bulkio::OutShortPort *dataShort_out;

    private:
};
#endif // RFNOC_TESTCOMPONENT_BASE_IMPL_BASE_H
