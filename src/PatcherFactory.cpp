#include "PatcherFactory.h"
#include <dlfcn.h>
#include <iostream>
#include <exception>
#include <boost/filesystem.hpp>

using std::endl;
using std::cerr;
using std::runtime_error;

namespace fs = boost::filesystem;

#ifndef RNBO_OSCQUERY_BUILTIN_PATCHER
RNBO::PatcherFactoryFunctionPtr GetPatcherFactoryFunction() {
	throw new std::runtime_error("global factory allocation not supported");
}

void SetLogger(RNBO::Logger* logger) {
	throw new std::runtime_error("global logger setting not supported");
}

#else
std::shared_ptr<PatcherFactory> PatcherFactory::CreateBuiltInFactory() {
	RNBO::PatcherFactoryFunctionPtr factory = GetPatcherFactoryFunction();
	if (!factory) {
		throw new runtime_error("failed to get factory from built in GetPatcherFactoryFunction");
	}
	return std::shared_ptr<PatcherFactory>(new PatcherFactory(nullptr, factory));
}
#endif

PatcherFactory::PatcherFactory(void * handle, RNBO::PatcherFactoryFunctionPtr factory) : mHandle(handle), mFactory(factory) { }

PatcherFactory::~PatcherFactory() {
	mFactory = nullptr;
	if (mHandle) {
		dlclose(mHandle);
	}
}

RNBO::UniquePtr<RNBO::PatcherInterface> PatcherFactory::createInstance() {
	return RNBO::UniquePtr<RNBO::PatcherInterface>(mFactory());
}

std::shared_ptr<PatcherFactory> PatcherFactory::CreateFactory(const std::string& dllPath) {
	if (!fs::exists(dllPath)) {
		throw new runtime_error("dynamic library file does not exist: " + dllPath);
	}
	boost::system::error_code ec;
	boost::uintmax_t filesize = fs::file_size(dllPath, ec);
	if (ec || !filesize) {
		throw new runtime_error("dynamic library file size problem: " + dllPath);
	}

	void* handle = dlopen(dllPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
	if (!handle) {
		throw new runtime_error("failed to open dynamic library at path: " + dllPath);
	}
	RNBO::GetPatcherFactoryFunctionPtr getfactory = (RNBO::GetPatcherFactoryFunctionPtr)dlsym(handle, "GetPatcherFactoryFunction");
	if (!getfactory) {
		throw new runtime_error("failed to get GetPatcherFactoryFunction from dynamic library at path: " + dllPath);
	}
	RNBO::PatcherFactoryFunctionPtr factory = getfactory();
	if (!factory) {
		throw new runtime_error("failed to get factory from dynamic library at path: " + dllPath);
	}
	RNBO::SetLoggerFunctionPtr setLoggerFunc = (RNBO::SetLoggerFunctionPtr)dlsym(handle, "SetLogger");
	if (setLoggerFunc) {
		setLoggerFunc(&RNBO::Logger::getInstance());
	}

	return std::shared_ptr<PatcherFactory>(new PatcherFactory(handle, factory));
}
