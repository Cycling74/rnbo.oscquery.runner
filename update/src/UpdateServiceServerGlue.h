
/*
 * This file was automatically generated by sdbus-c++-xml2cpp; DO NOT EDIT!
 */

#ifndef __sdbuscpp_____src_UpdateServiceServerGlue_h__adaptor__H__
#define __sdbuscpp_____src_UpdateServiceServerGlue_h__adaptor__H__

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <tuple>

namespace com {
namespace cycling74 {

class rnbo_adaptor
{
public:
    static constexpr const char* INTERFACE_NAME = "com.cycling74.rnbo";

protected:
    rnbo_adaptor(sdbus::IObject& object)
        : object_(object)
    {
        object_.registerMethod("QueueRunnerInstall").onInterface(INTERFACE_NAME).withInputParamNames("version").withOutputParamNames("queued").implementedAs([this](const std::string& version){ return this->QueueRunnerInstall(version); });
        object_.registerMethod("UpdateOutdated").onInterface(INTERFACE_NAME).implementedAs([this](){ return this->UpdateOutdated(); });
        object_.registerProperty("State").onInterface(INTERFACE_NAME).withGetter([this](){ return this->State(); });
        object_.registerProperty("Status").onInterface(INTERFACE_NAME).withGetter([this](){ return this->Status(); });
        object_.registerProperty("OutdatedPackages").onInterface(INTERFACE_NAME).withGetter([this](){ return this->OutdatedPackages(); });
    }

    ~rnbo_adaptor() = default;

private:
    virtual bool QueueRunnerInstall(const std::string& version) = 0;
    virtual void UpdateOutdated() = 0;

private:
    virtual uint32_t State() = 0;
    virtual std::string Status() = 0;
    virtual uint32_t OutdatedPackages() = 0;

private:
    sdbus::IObject& object_;
};

}} // namespaces

#endif
