
/*
 * This file was automatically generated by sdbus-c++-xml2cpp; DO NOT EDIT!
 */

#ifndef __sdbuscpp________src_UpdateServiceProxyGlue_h__proxy__H__
#define __sdbuscpp________src_UpdateServiceProxyGlue_h__proxy__H__

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <tuple>

namespace com {
namespace cycling74 {

class rnbo_proxy
{
public:
    static constexpr const char* INTERFACE_NAME = "com.cycling74.rnbo";

protected:
    rnbo_proxy(sdbus::IProxy& proxy)
        : proxy_(proxy)
    {
    }

    ~rnbo_proxy() = default;

public:
    bool QueueRunnerInstall(const std::string& version)
    {
        bool result;
        proxy_.callMethod("QueueRunnerInstall").onInterface(INTERFACE_NAME).withArguments(version).storeResultsTo(result);
        return result;
    }

    void UpdateOutdated()
    {
        proxy_.callMethod("UpdateOutdated").onInterface(INTERFACE_NAME);
    }

public:
    uint32_t State()
    {
        return proxy_.getProperty("State").onInterface(INTERFACE_NAME);
    }

    std::string Status()
    {
        return proxy_.getProperty("Status").onInterface(INTERFACE_NAME);
    }

    uint32_t OutdatedPackages()
    {
        return proxy_.getProperty("OutdatedPackages").onInterface(INTERFACE_NAME);
    }

private:
    sdbus::IProxy& proxy_;
};

}} // namespaces

#endif
