#ifndef BLOCKID_H
#define BLOCKID_H

#include <boost/bind.hpp>
#include <boost/function.hpp>

typedef boost::function<void(const std::string &componentID, const std::string &blockID)> blockIDCallback;

#endif
