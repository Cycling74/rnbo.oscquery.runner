#pragma once

#include <optional>
#include <string>
#include <vector>

//Pure (std only) selection of the newest runner version from `apt-cache madison` output.
//
//Runner versions look like `<libVersion>-<N>` or, for betas, `<libVersion>-<N>~<pre>`
//(dpkg order: 1.4.6-10 < 1.4.6-11~beta.1 < 1.4.6-11~beta.2 < 1.4.6-11).
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

	//dpkg's verrevcmp: <0, 0, >0
	int verrevcmp(const std::string& a, const std::string& b);
}
