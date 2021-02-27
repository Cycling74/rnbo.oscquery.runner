
/*
 * This file was automatically generated by sdbus-c++-xml2cpp; DO NOT EDIT!
 */

#ifndef __sdbuscpp__UpdateServiceServerGlue_h__adaptor__H__
#define __sdbuscpp__UpdateServiceServerGlue_h__adaptor__H__

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
        object_.registerProperty("Active").onInterface(INTERFACE_NAME).withGetter([this](){ return this->Active(); });
        object_.registerProperty("Status").onInterface(INTERFACE_NAME).withGetter([this](){ return this->Status(); });
    }

    ~rnbo_adaptor() = default;

private:
    virtual bool QueueRunnerInstall(const std::string& version) = 0;

private:
    virtual bool Active() = 0;
    virtual std::string Status() = 0;

private:
    sdbus::IObject& object_;
};

}} // namespaces

#endif
