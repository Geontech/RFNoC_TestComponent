#ifndef STRUCTPROPS_H
#define STRUCTPROPS_H

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

*******************************************************************************************/

#include <ossie/CorbaUtils.h>
#include <CF/cf.h>
#include <ossie/PropertyMap.h>

struct arg_struct {
    arg_struct ()
    {
        id = "";
        value = "";
        type = "INT";
    };

    static std::string getId() {
        return std::string("arg");
    };

    std::string id;
    std::string value;
    std::string type;
};

inline bool operator>>= (const CORBA::Any& a, arg_struct& s) {
    CF::Properties* temp;
    if (!(a >>= temp)) return false;
    const redhawk::PropertyMap& props = redhawk::PropertyMap::cast(*temp);
    if (props.contains("args::id")) {
        if (!(props["args::id"] >>= s.id)) return false;
    }
    if (props.contains("args::value")) {
        if (!(props["args::value"] >>= s.value)) return false;
    }
    if (props.contains("args::type")) {
        if (!(props["args::type"] >>= s.type)) return false;
    }
    return true;
}

inline void operator<<= (CORBA::Any& a, const arg_struct& s) {
    redhawk::PropertyMap props;
 
    props["args::id"] = s.id;
 
    props["args::value"] = s.value;
 
    props["args::type"] = s.type;
    a <<= props;
}

inline bool operator== (const arg_struct& s1, const arg_struct& s2) {
    if (s1.id!=s2.id)
        return false;
    if (s1.value!=s2.value)
        return false;
    if (s1.type!=s2.type)
        return false;
    return true;
}

inline bool operator!= (const arg_struct& s1, const arg_struct& s2) {
    return !(s1==s2);
}

#endif // STRUCTPROPS_H
