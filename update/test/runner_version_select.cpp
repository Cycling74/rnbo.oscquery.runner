//Tests for runner_version::selectLatest, isLegacyLibrary, reportedVersion, resolveInstallVersion and the validation::version regex, std only so they run
//on any host. Build and run them with the CMake project in this directory, from the repo root:
//
//  cmake -S update/test -B build-update-test && cmake --build build-update-test --parallel 6 && ctest --test-dir build-update-test

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "RunnerVersionSelect.h"
#include "Validation.h"

namespace {
	int failures = 0;
	int checks = 0;

	const std::string PKG = "rnbooscquery";
	const std::string URL = "https://c74-apt.nyc3.digitaloceanspaces.com/raspbian bookworm/main armhf Packages";

	std::string madison(const std::string& version, const std::string& pkg = PKG) {
		return " " + pkg + " | " + version + " | " + URL + "\n";
	}

	std::vector<std::string> lines(const std::vector<std::string>& versions) {
		std::vector<std::string> r;
		for (auto& v: versions) {
			r.push_back(madison(v));
		}
		return r;
	}

	std::string str(const std::optional<std::string>& o) { return o ? "\"" + *o + "\"" : "<none>"; }

	void expect(const std::string& name, const std::vector<std::string>& in, const std::string& lib, std::optional<std::string> want, bool wantExact = false) {
		bool exact = true; //make sure it gets reset
		auto got = runner_version::selectLatest(in, PKG, lib, &exact);
		checks++;
		if (got != want || exact != wantExact) {
			failures++;
			std::cerr << "FAIL " << name << ": got " << str(got) << " exact=" << exact
				<< ", want " << str(want) << " exact=" << wantExact << std::endl;
		}
	}

	void expectCmp(const std::string& a, const std::string& b, int sign) {
		int c = runner_version::verrevcmp(a, b);
		int s = c < 0 ? -1 : (c > 0 ? 1 : 0);
		checks++;
		if (s != sign) {
			failures++;
			std::cerr << "FAIL verrevcmp(" << a << ", " << b << ") = " << c << ", want sign " << sign << std::endl;
		}
	}

	void expectLegacy(const std::string& lib, bool want) {
		checks++;
		if (runner_version::isLegacyLibrary(lib) != want) {
			failures++;
			std::cerr << "FAIL isLegacyLibrary(" << lib << ") want " << want << std::endl;
		}
	}

	void expectReported(const std::string& apt, const std::string& lib, const std::string& want) {
		auto got = runner_version::reportedVersion(apt, lib);
		checks++;
		if (got != want) {
			failures++;
			std::cerr << "FAIL reportedVersion(" << apt << ", " << lib << ") = " << got << ", want " << want << std::endl;
		}
	}

	void expectResolve(const std::string& name, const std::vector<std::string>& in, const std::string& requested, const std::string& want) {
		auto got = runner_version::resolveInstallVersion(in, PKG, requested);
		checks++;
		if (got != want) {
			failures++;
			std::cerr << "FAIL resolveInstallVersion " << name << ": got " << got << ", want " << want << std::endl;
		}
	}

	void expectValid(const std::string& v, bool want) {
		checks++;
		if (validation::version(v) != want) {
			failures++;
			std::cerr << "FAIL validation::version(" << v << ") want " << want << std::endl;
		}
	}
}

int main() {
	auto N = std::optional<std::string>();
	auto S = [](const char *s) { return std::optional<std::string>(s); };

	expect("beta beats older final", lines({"1.4.7-10", "1.4.7-11~beta.1"}), "1.4.7", S("1.4.7-11~beta.1"));
	expect("beta beats older final (reversed)", lines({"1.4.7-11~beta.1", "1.4.7-10"}), "1.4.7", S("1.4.7-11~beta.1"));
	expect("final beats its betas", lines({"1.4.7-10", "1.4.7-11~beta.1", "1.4.7-11"}), "1.4.7", S("1.4.7-11"));
	expect("final beats its betas (final first)", lines({"1.4.7-11", "1.4.7-11~beta.1", "1.4.7-10"}), "1.4.7", S("1.4.7-11"));
	expect("beta numbers compare numerically", lines({"1.4.7-11~beta.1", "1.4.7-11~beta.2", "1.4.7-11~beta.10"}), "1.4.7", S("1.4.7-11~beta.10"));
	expect("beta numbers compare numerically (reversed)", lines({"1.4.7-11~beta.10", "1.4.7-11~beta.2", "1.4.7-11~beta.1"}), "1.4.7", S("1.4.7-11~beta.10"));
	expect("rc beats beta", lines({"1.4.7-11~beta.3", "1.4.7-11~rc.1"}), "1.4.7", S("1.4.7-11~rc.1"));
	expect("higher beta beats lower final", lines({"1.4.7-12~beta.1", "1.4.7-11"}), "1.4.7", S("1.4.7-12~beta.1"));
	expect("10 > 9 numerically", lines({"1.4.6-9", "1.4.6-10", "1.4.6-2"}), "1.4.6", S("1.4.6-10"));

	//library version with dashes
	expect("dashed lib", lines({"1.5.0-dev.150-11"}), "1.5.0-dev.150", S("1.5.0-dev.150-11"));
	expect("lib prefix must not match", lines({"1.5.0-dev.150-11"}), "1.5.0-dev.15", N);
	expect("lib prefix must not match (shorter)", lines({"1.5.0-dev.15-11"}), "1.5.0-dev.150", N);
	expect("lib 1.5.0 must not match dashed lib", lines({"1.5.0-dev.150-11"}), "1.5.0", N);
	expect("dashed lib with beta", lines({"1.5.0-dev.150-10", "1.5.0-dev.150-11~beta.1"}), "1.5.0-dev.150", S("1.5.0-dev.150-11~beta.1"));
	expect("real example line", {"rnbooscquery | 1.4.0-dev.0-2 | https://c74-apt.nyc3.digitaloceanspaces.com/raspbian bookworm/beta armhf Packages"}, "1.4.0-dev.0", S("1.4.0-dev.0-2"));

	//exact only
	expect("bare lib only", lines({"1.4.6"}), "1.4.6", N, true);
	expect("bare lib plus app", lines({"1.4.6", "1.4.6-3"}), "1.4.6", S("1.4.6-3"), true);

	//ignored things
	expect("other lib versions", lines({"1.4.5-20", "1.4.60-20", "1.4-20", "2.4.6-20"}), "1.4.6", N);
	expect("other package", {madison("1.4.6-11", "rnbooscquery-dev"), madison("1.4.6-11", "other"), madison("1.4.6-11", "xrnbooscquery")}, "1.4.6", N);
	expect("package name is trimmed and exact", {"rnbooscquery|1.4.6-5|x\n", "\trnbooscquery\t|\t1.4.6-6\t|\tx\r\n"}, "1.4.6", S("1.4.6-6"));
	expect("malformed suffixes", lines({"1.4.6-11abc", "1.4.6-~beta", "1.4.6-11~", "1.4.6-", "1.4.6-abc", "1.4.6-11~beta-1",
			"1.4.6-11 ~beta", "1.4.6--11", "1.4.6-11~be ta", "1.4.6-11+b1", "1.4.6-11.1", "1.4.6-1_1", "1.4.6-+11", "1.4.6--"}), "1.4.6", N);
	expect("malformed ignored, valid kept", lines({"1.4.6-11abc", "1.4.6-3", "1.4.6-99~"}), "1.4.6", S("1.4.6-3"));
	expect("epoch is not ours", lines({"1:1.4.6-11"}), "1.4.6", N);
	expect("no dash", lines({"1.4.6"}), "1.4.6", N, true);
	expect("empty input", {}, "1.4.6", N);
	expect("garbage lines", {"", "\n", "|", "||", "W: some warning from apt\n", "rnbooscquery\n", "rnbooscquery |", "rnbooscquery | \n", " | 1.4.6-3 | x"}, "1.4.6", N);
	//lines come through a fixed size read buffer, a line cut before the second separator is not a version
	expect("missing third field", {"rnbooscquery | 1.4.6-4"}, "1.4.6", N);
	expect("line cut inside the version", {"rnbooscquery | 1.4.6-11~beta.12", "345 | https://example.com bookworm/main armhf Packages\n"}, "1.4.6", N);

	//absurd digit strings: no overflow, no exception
	const std::string huge(400, '9');
	expect("huge app number wins", lines({"1.4.7-10", "1.4.7-" + huge}), "1.4.7", S(("1.4.7-" + huge).c_str()));
	expect("huge beats huge+1 digit loses", lines({"1.4.7-" + huge + "9", "1.4.7-" + huge}), "1.4.7", S(("1.4.7-" + huge + "9").c_str()));
	expect("huge beta", lines({"1.4.7-" + huge + "~beta.1", "1.4.7-10"}), "1.4.7", S(("1.4.7-" + huge + "~beta.1").c_str()));
	expect("huge beta number", lines({"1.4.7-11~beta." + huge, "1.4.7-11~beta." + huge + "9", "1.4.7-11~beta.2"}), "1.4.7", S(("1.4.7-11~beta." + huge + "9").c_str()));
	expect("leading zeros compare numerically", lines({"1.4.6-009", "1.4.6-10"}), "1.4.6", S("1.4.6-10"));
	expect("zero app number", lines({"1.4.6-0"}), "1.4.6", S("1.4.6-0"));
	expect("zero beta beats nothing", lines({"1.4.7-0~beta.1"}), "1.4.7", S("1.4.7-0~beta.1"));

	//duplicates (several archs/components)
	expect("duplicates", lines({"1.4.7-11~beta.1", "1.4.7-11~beta.1", "1.4.7-10", "1.4.7-10", "1.4.7-11~beta.1"}), "1.4.7", S("1.4.7-11~beta.1"));

	//exact pointer is optional
	checks++;
	if (runner_version::selectLatest(lines({"1.4.6-3"}), PKG, "1.4.6") != S("1.4.6-3")) {
		failures++;
		std::cerr << "FAIL null exact pointer" << std::endl;
	}

	//dpkg comparison of the prerelease part (dpkg --compare-versions semantics)
	expectCmp("~beta.1", "~beta.2", -1);
	expectCmp("~beta.2", "~beta.10", -1);
	expectCmp("~beta.10", "~beta.2", 1);
	expectCmp("~beta.1", "~beta.1", 0);
	expectCmp("~beta.01", "~beta.1", 0);
	expectCmp("~beta", "~beta.1", -1);
	expectCmp("~beta.1", "~rc.1", -1);
	expectCmp("~alpha", "~beta", -1);
	expectCmp("~~", "~", -1);
	expectCmp("~", "", -1);
	expectCmp("~rc1", "~rc1+b1", -1);
	expectCmp("~a", "~a.", -1);
	expectCmp("~a1", "~a.1", -1); //'.' sorts after end/digit boundary: "a" "1" vs "a" ".1"
	expectCmp("~beta.1", "~beta1", 1);

	//legacy libraries (<= 1.4.6)
	expectLegacy("1.4.6", true);
	expectLegacy("1.4.6-alpha.1", true);
	expectLegacy("1.4.0-dev.0", true);
	expectLegacy("1.3.9", true);
	expectLegacy("0.9.12", true);
	expectLegacy("1.4.06", true);
	expectLegacy("1.4.7", false);
	expectLegacy("1.4.60", false);
	expectLegacy("1.5.0-dev.150", false);
	expectLegacy("1.5.0", false);
	expectLegacy("2.0.0", false);
	expectLegacy("10.0.0", false);
	expectLegacy("1.10.0", false);
	expectLegacy("1.4", false);
	expectLegacy("1.4.", false);
	expectLegacy("1..6", false);
	expectLegacy("garbage", false);
	expectLegacy("", false);
	expectLegacy("v1.4.6", false);

	expectReported("1.4.6-12~beta.0", "1.4.6", "1.4.6-12");
	expectReported("1.4.0-dev.0-3~beta.0", "1.4.0-dev.0", "1.4.0-dev.0-3");
	expectReported("1.4.6-12", "1.4.6", "1.4.6-12");
	expectReported("1.4.6-12~beta.1", "1.4.6", "1.4.6-12~beta.1");
	expectReported("1.4.6-12~beta.10", "1.4.6", "1.4.6-12~beta.10");
	expectReported("1.4.6-12~beta.", "1.4.6", "1.4.6-12~beta.");
	expectReported("1.4.6-12~beta.1~x", "1.4.6", "1.4.6-12~beta.1~x");
	expectReported("1.4.6-12~rc.0", "1.4.6", "1.4.6-12~rc.0");
	expectReported("1.4.6-12~beta.0", "1.4.7", "1.4.6-12~beta.0"); //lib mismatch
	expectReported("1.4.7-12~beta.0", "1.4.6", "1.4.7-12~beta.0"); //lib mismatch
	expectReported("1.4.7-12~beta.0", "1.4.7", "1.4.7-12~beta.0"); //not legacy
	expectReported("1.5.0-dev.150-12~beta.0", "1.5.0-dev.150", "1.5.0-dev.150-12~beta.0");
	expectReported("1.4.6-~beta.0", "1.4.6", "1.4.6-~beta.0");
	expectReported("1.4.6-x~beta.0", "1.4.6", "1.4.6-x~beta.0");
	expectReported("1.4.6~beta.0", "1.4.6", "1.4.6~beta.0");
	expectReported("~beta.0", "1.4.6", "~beta.0");
	expectReported("", "1.4.6", "");

	//selectLatest for legacy libraries
	expect("legacy beta.0 beats older final", lines({"1.4.6-10", "1.4.6-11~beta.0"}), "1.4.6", S("1.4.6-11~beta.0"));
	expect("legacy beta.1 ignored", lines({"1.4.6-10", "1.4.6-11~beta.1"}), "1.4.6", S("1.4.6-10"));
	expect("legacy beta.1 alone ignored", lines({"1.4.6-11~beta.1"}), "1.4.6", N);
	expect("legacy final beats beta.0 of the same number", lines({"1.4.6-11~beta.0", "1.4.6-11"}), "1.4.6", S("1.4.6-11"));
	expect("legacy rc.1 ignored", lines({"1.4.6-10", "1.4.6-11~rc.1"}), "1.4.6", S("1.4.6-10"));
	expect("legacy beta.0 wins over ignored beta.1", lines({"1.4.6-10", "1.4.6-11~beta.1", "1.4.6-11~beta.0"}), "1.4.6", S("1.4.6-11~beta.0"));
	expect("legacy final beats older beta.0", lines({"1.4.6-11~beta.0", "1.4.6-12"}), "1.4.6", S("1.4.6-12"));
	expect("legacy final beats older beta.0 (reversed)", lines({"1.4.6-12", "1.4.6-11~beta.0"}), "1.4.6", S("1.4.6-12"));
	expect("legacy beta.0 beats older beta.0", lines({"1.4.6-11~beta.0", "1.4.6-12~beta.0"}), "1.4.6", S("1.4.6-12~beta.0"));
	expect("legacy dashed lib", lines({"1.4.0-dev.0-10", "1.4.0-dev.0-11~beta.0", "1.4.0-dev.0-12~rc.1"}), "1.4.0-dev.0", S("1.4.0-dev.0-11~beta.0"));
	expect("non-legacy still picks beta.1", lines({"1.4.7-10", "1.4.7-11~beta.1"}), "1.4.7", S("1.4.7-11~beta.1"));
	expect("non-legacy still picks beta.0", lines({"1.4.7-10", "1.4.7-11~beta.0"}), "1.4.7", S("1.4.7-11~beta.0"));
	expect("non-legacy dashed lib still picks beta.2", lines({"1.5.0-dev.150-10", "1.5.0-dev.150-11~beta.2"}), "1.5.0-dev.150", S("1.5.0-dev.150-11~beta.2"));

	//resolveInstallVersion
	expectResolve("exact wins", lines({"1.4.6-12", "1.4.6-12~beta.0"}), "1.4.6-12", "1.4.6-12");
	expectResolve("exact beta is exact", lines({"1.4.6-12~beta.0"}), "1.4.6-12~beta.0", "1.4.6-12~beta.0");
	expectResolve("falls to beta.0", lines({"1.4.6-11", "1.4.6-12~beta.0"}), "1.4.6-12", "1.4.6-12~beta.0");
	expectResolve("falls to beta.0, dashed lib", lines({"1.4.0-dev.0-3~beta.0"}), "1.4.0-dev.0-3", "1.4.0-dev.0-3~beta.0");
	expectResolve("falls to the only prerelease", lines({"1.4.6-12~beta.1"}), "1.4.6-12", "1.4.6-12~beta.1");
	expectResolve("highest prerelease", lines({"1.4.6-12~beta.0", "1.4.6-12~beta.2", "1.4.6-12~beta.1"}), "1.4.6-12", "1.4.6-12~beta.2");
	expectResolve("prereleases order numerically", lines({"1.4.6-12~beta.10", "1.4.6-12~beta.9"}), "1.4.6-12", "1.4.6-12~beta.10");
	expectResolve("final wins over any prerelease", lines({"1.4.6-12~beta.2", "1.4.6-12", "1.4.6-12~beta.0"}), "1.4.6-12", "1.4.6-12");
	expectResolve("other app numbers ignored", lines({"1.4.6-120~beta.3", "1.4.6-13~beta.5", "1.4.6-12~beta.1"}), "1.4.6-12", "1.4.6-12~beta.1");
	expectResolve("invalid prerelease ignored", lines({"1.4.6-12~be;ta", "1.4.6-12~"}), "1.4.6-12", "1.4.6-12");
	expectResolve("non-legacy not rewritten", lines({"1.4.7-12~beta.0"}), "1.4.7-12", "1.4.7-12");
	expectResolve("non-legacy dashed not rewritten", lines({"1.5.0-dev.150-12~beta.0"}), "1.5.0-dev.150-12", "1.5.0-dev.150-12");
	expectResolve("nothing listed", {}, "1.4.6-12", "1.4.6-12");
	expectResolve("other versions listed", lines({"1.4.6-11", "1.4.6-13~beta.0"}), "1.4.6-12", "1.4.6-12");
	expectResolve("other package ignored", {madison("1.4.6-12~beta.0", "rnbooscquery-dev"), madison("1.4.6-12", "other")}, "1.4.6-12", "1.4.6-12");
	expectResolve("other package exact does not win", {madison("1.4.6-12", "other"), madison("1.4.6-12~beta.0")}, "1.4.6-12", "1.4.6-12~beta.0");
	expectResolve("cut line ignored", {"rnbooscquery | 1.4.6-12~beta.0"}, "1.4.6-12", "1.4.6-12");
	expectResolve("not <lib>-<digits>", lines({"1.4.6~beta.0"}), "1.4.6", "1.4.6");
	expectResolve("non digit suffix", lines({"1.4.6-x~beta.0"}), "1.4.6-x", "1.4.6-x");
	expectResolve("garbage request", lines({"1.4.6-12~beta.0"}), "", "");

	//validation accepts our version forms
	expectValid("1.4.6-11~beta.1", true);
	expectValid("1.4.6-11~beta.10", true);
	expectValid("1.4.6-11", true);
	expectValid("1.5.0-dev.150-11", true);
	expectValid("1.5.0-dev.150-11~beta.1", true);
	expectValid("1.4.6-11~beta.1 & rm -rf /", false);
	expectValid("1.4.6-11~beta.1;ls", false);

	std::cout << (checks - failures) << "/" << checks << " checks passed" << std::endl;
	return failures ? 1 : 0;
}
