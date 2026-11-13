#pragma once

// RNBO_VERSION_MAJOR/MINOR/PATCH come from CMakeLists.txt, which reads them from
// version.cmake in the RNBO source tree (or conan package) being built against.
//
// Use this to guard code that needs an RNBO API newer than the last public release, so the
// same source builds against both the released RNBO and the development sources.
// Delete a guard once the release it tests for is the oldest one we support.
#define RNBO_VERSION_AT_LEAST(major, minor, patch) \
	((RNBO_VERSION_MAJOR > (major)) || \
	 (RNBO_VERSION_MAJOR == (major) && RNBO_VERSION_MINOR > (minor)) || \
	 (RNBO_VERSION_MAJOR == (major) && RNBO_VERSION_MINOR == (minor) && RNBO_VERSION_PATCH >= (patch)))

// The logger API changed during the 1.5.0 development cycle, so 1.5.0 prerelease packages exist with
// either API and the version number can't tell them apart. Test for the old header instead.
#if __has_include("RNBO_LoggerImpl.h")
#define RNBO_HAS_LEGACY_LOGGER 1
#else
#define RNBO_HAS_LEGACY_LOGGER 0
#endif
