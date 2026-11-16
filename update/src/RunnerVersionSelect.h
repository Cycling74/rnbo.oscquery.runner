#pragma once

#include <optional>
#include <string>
#include <vector>

//Pure (std only) selection of the newest runner version from `apt-cache madison` output.
//
//Runner versions look like `<libVersion>-<N>` or, for betas, `<libVersion>-<N>~<pre>`
//(dpkg order: 1.4.6-10 < 1.4.6-11~beta.1 < 1.4.6-11~beta.2 < 1.4.6-11).
//
//Legacy libraries: Max clients shipped with RNBO 1.4.6 and earlier only accept `<lib>-<digits>`, compare N as an
//integer and consider an update finished when the runner's reported runner_version string equals the string
//offered here. So for a library whose leading major.minor.patch is <= 1.4.6:
// - the only permitted prerelease is exactly `~beta.0` (selectLatest ignores other prereleases),
// - an N is never used twice (`1.4.6-12~beta.0` and `1.4.6-12` never both exist),
// - the apt package keeps the real version, but the client is shown `1.4.6-12` (reportedVersion),
// - when the client asks for `rnbooscquery=1.4.6-12`, resolveInstallVersion picks the newest package of app number 12:
//   `1.4.6-12` if apt has it, otherwise the highest `1.4.6-12~<pre>`. This tolerates prereleases the rules above forbid.
//Newer libraries are untouched.
//
//madisonLines: lines like `rnbooscquery | 1.4.0-dev.0-2 | https://... bookworm/main armhf Packages`
//packageName:  only lines for this package are considered
//libVersion:   only versions of the form `<libVersion>-<suffix>` are considered
//exact:        if non-null, set to true when a line lists exactly `libVersion` (no app number)
//
//returns the original apt version string of the winner, or nullopt if nothing with an app number matched.
namespace runner_version {
	std::optional<std::string> selectLatest(
			const std::vector<std::string>& madisonLines,
			const std::string& packageName,
			const std::string& libVersion,
			bool* exact = nullptr
	);

	//true if the leading major.minor.patch of libVersion is <= 1.4.6 (anything after is ignored, unparsable is false)
	bool isLegacyLibrary(const std::string& libVersion);

	//the version a client is shown: for a legacy lib, `<lib>-<digits>~beta.0` is reported as `<lib>-<digits>`,
	//everything else is returned unchanged
	std::string reportedVersion(const std::string& aptVersion, const std::string& libVersion);

	//the version to hand to apt-get install. For a legacy `<lib>-<digits>` this is the newest listed version of that
	//app number: `requested` itself if listed, otherwise the highest `requested~<pre>` in dpkg order. Anything else,
	//or nothing listed, returns `requested` unchanged.
	std::string resolveInstallVersion(
			const std::vector<std::string>& madisonLines,
			const std::string& packageName,
			const std::string& requested
	);

	//dpkg's verrevcmp: <0, 0, >0
	int verrevcmp(const std::string& a, const std::string& b);
}
