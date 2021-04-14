#include "PatcherFactory.h"
#include <dlfcn.h>
#include <iostream>
#include <exception>

using std::endl;
using std::cerr;
using std::runtime_error;

#ifndef RNBO_OSCQUERY_BUILTIN_PATCHER
RNBO::PatcherFactoryFunctionPtr GetPatcherFactoryFunction(RNBO::PlatformInterface*) {
	throw new std::runtime_error("global factory allocation not supported");
}
#else
std::shared_ptr<PatcherFactory> PatcherFactory::CreateBuiltInFactory() {
	RNBO::PatcherFactoryFunctionPtr factory = GetPatcherFactoryFunction(RNBO::Platform::get());
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
	void* handle = dlopen(dllPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
	if (!handle) {
		throw new runtime_error("failed to open dynamic library at path: " + dllPath);
	}
	RNBO::GetPatcherFactoryFunctionPtr getfactory = (RNBO::GetPatcherFactoryFunctionPtr)dlsym(handle, "GetPatcherFactoryFunction");
	if (!getfactory) {
		throw new runtime_error("failed to get GetPatcherFactoryFunction from dynamic library at path: " + dllPath);
	}
	RNBO::PatcherFactoryFunctionPtr factory = getfactory(RNBO::Platform::get());
	if (!factory) {
		throw new runtime_error("failed to get factory from dynamic library at path: " + dllPath);
	}
	return std::shared_ptr<PatcherFactory>(new PatcherFactory(handle, factory));
}
