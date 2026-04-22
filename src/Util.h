#pragma once

#include <string>
#include <string>

namespace runner {
inline const std::string runner_version(RUNNER_VERSION);
inline const std::string runner_git_hash(RUNNER_GIT_HASH);
inline const std::string rnbo_version(RNBO_VERSION);
inline const std::string rnbo_compat_version(RNBO_COMPAT_VERSION);

inline const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);

#if defined(RNBOOSCQUERY_CXX_COMPILER_ID)
inline const std::string rnbo_compiler_id(RNBOOSCQUERY_CXX_COMPILER_ID);
#else
inline const std::string rnbo_compiler_id("unknown");
#endif
#if defined(RNBOOSCQUERY_CXX_COMPILER_VERSION)
inline const std::string rnbo_compiler_version(RNBOOSCQUERY_CXX_COMPILER_VERSION);
#else
inline const std::string rnbo_compiler_version("unknown");
#endif

	std::string render_time_template(const std::string& templ);
	std::string targetid();
	std::string sanitizeName(std::string n);
}
