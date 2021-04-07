// DLL loading and Patcher instantiation
// Based off DynamicFactory by Jeremy Bernstein
#pragma once

#include <memory>
#include "RNBO.h"

class PatcherFactory {
	public:
		/// Create a factory with the DLL at the given path.
		/// Throws on error, returns on success.
		static std::shared_ptr<PatcherFactory> CreateFactory(const std::string& dllPath) noexcept(false);

#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
		static std::shared_ptr<PatcherFactory> CreateBuiltInFactory() noexcept(false);
#endif

		/// Create an instance.
		RNBO::UniquePtr<RNBO::PatcherInterface> createInstance();
		~PatcherFactory();
	protected:
		PatcherFactory(void * handle, RNBO::PatcherFactoryFunctionPtr factory);
	private:
		PatcherFactory(const PatcherFactory&) = delete;
		PatcherFactory& operator=(const PatcherFactory&) = delete;
		PatcherFactory(PatcherFactory&&) = delete;
		PatcherFactory& operator=(PatcherFactory&&) = delete;

		RNBO::PatcherFactoryFunctionPtr mFactory;
		void * mHandle;
};
