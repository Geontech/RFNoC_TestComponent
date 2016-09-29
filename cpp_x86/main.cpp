#include <iostream>
#include "ossie/ossieSupport.h"

#include "RFNoC_TestComponent.h"
int main(int argc, char* argv[])
{
    RFNoC_TestComponent_i* RFNoC_TestComponent_servant;
    Component::start_component(RFNoC_TestComponent_servant, argc, argv);
    return 0;
}

