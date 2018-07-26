#ifndef RFNOC_TESTCOMPONENT_I_IMPL_H
#define RFNOC_TESTCOMPONENT_I_IMPL_H

// Base Include(s)
#include "RFNoC_TestComponent_base.h"

// RF-NoC RH Include(s)
#include <GenericThreadedComponent.h>
#include <RFNoC_Component.h>

// UHD Include(s)
#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>
#include <uhd/device3.hpp>

/*
 * The class for the component
 */
class RFNoC_TestComponent_i : public RFNoC_TestComponent_base, public RFNoC_RH::RFNoC_Component
{
    ENABLE_LOGGING

	// Constructor(s) and/or Destructor
    public:
        RFNoC_TestComponent_i(const char *uuid, const char *label);
        ~RFNoC_TestComponent_i();

    // Public Method(s)
    public:
        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        void start() throw (CF::Resource::StartError, CORBA::SystemException);

        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

	// Public RFNoC_Component Method(s)
	public:
		// Methods to be called by the persona, inherited from RFNoC_Component
		void setRxStreamer(uhd::rx_streamer::sptr rxStreamer);

		void setTxStreamer(uhd::tx_streamer::sptr txStreamer);

    // Protected Method(s)
    protected:
        void constructor();

        // Don't use the default serviceFunction for clarity
        int serviceFunction() { return FINISH; }

        int rxServiceFunction();

        int txServiceFunction();

    // Private Method(s)
    private:
        void argsChanged(const std::vector<arg_struct> &oldValue, const std::vector<arg_struct> &newValue);

        void newConnection(const char *connectionID);

        void newDisconnection(const char *connectionID);

        bool setArgs(std::vector<arg_struct> &newArgs);

        void startRxStream();

        void stopRxStream();

        void streamChanged(bulkio::InShortPort::StreamType stream);

    // Private Member(s)
    private:
        std::vector<std::complex<short> > output;
        bool receivedSRI;
        uhd::rfnoc::block_ctrl_base::sptr rfnocBlock;
        uhd::rx_streamer::sptr rxStreamer;
        bool rxStreamStarted;
        boost::shared_ptr<RFNoC_RH::GenericThreadedComponent> rxThread;
        size_t spp;
        BULKIO::StreamSRI sri;
        std::map<std::string, bool> streamMap;
        uhd::tx_streamer::sptr txStreamer;
        boost::shared_ptr<RFNoC_RH::GenericThreadedComponent> txThread;
};

#endif
