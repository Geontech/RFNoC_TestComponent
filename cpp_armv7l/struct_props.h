#ifndef STRUCTPROPS_H
#define STRUCTPROPS_H

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

*******************************************************************************************/

#include <ossie/CorbaUtils.h>
#include <CF/cf.h>
#include <ossie/PropertyMap.h>

struct RFNoC_Struct_struct {
    RFNoC_Struct_struct ()
    {
        upstreamBlockID = "";
    };

    static std::string getId() {
        return std::string("RFNoC_Struct");
    };

    std::string upstreamBlockID;
};

inline bool operator>>= (const CORBA::Any& a, RFNoC_Struct_struct& s) {
    CF::Properties* temp;
    if (!(a >>= temp)) return false;
    const redhawk::PropertyMap& props = redhawk::PropertyMap::cast(*temp);
    if (props.contains("RFNoC_Struct::upstreamBlockID")) {
        if (!(props["RFNoC_Struct::upstreamBlockID"] >>= s.upstreamBlockID)) return false;
    }
    return true;
}

inline void operator<<= (CORBA::Any& a, const RFNoC_Struct_struct& s) {
    redhawk::PropertyMap props;
 
    props["RFNoC_Struct::upstreamBlockID"] = s.upstreamBlockID;
    a <<= props;
}

inline bool operator== (const RFNoC_Struct_struct& s1, const RFNoC_Struct_struct& s2) {
    if (s1.upstreamBlockID!=s2.upstreamBlockID)
        return false;
    return true;
}

inline bool operator!= (const RFNoC_Struct_struct& s1, const RFNoC_Struct_struct& s2) {
    return !(s1==s2);
}

#endif // STRUCTPROPS_H
