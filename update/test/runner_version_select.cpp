//Tests for runner_version::selectLatest and the validation::version regex, std only so they run
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

	expect("beta beats older final", lines({"1.4.6-10", "1.4.6-11~beta.1"}), "1.4.6", S("1.4.6-11~beta.1"));
	expect("beta beats older final (reversed)", lines({"1.4.6-11~beta.1", "1.4.6-10"}), "1.4.6", S("1.4.6-11~beta.1"));
	expect("final beats its betas", lines({"1.4.6-10", "1.4.6-11~beta.1", "1.4.6-11"}), "1.4.6", S("1.4.6-11"));
	expect("final beats its betas (final first)", lines({"1.4.6-11", "1.4.6-11~beta.1", "1.4.6-10"}), "1.4.6", S("1.4.6-11"));
	expect("beta numbers compare numerically", lines({"1.4.6-11~beta.1", "1.4.6-11~beta.2", "1.4.6-11~beta.10"}), "1.4.6", S("1.4.6-11~beta.10"));
	expect("beta numbers compare numerically (reversed)", lines({"1.4.6-11~beta.10", "1.4.6-11~beta.2", "1.4.6-11~beta.1"}), "1.4.6", S("1.4.6-11~beta.10"));
	expect("rc beats beta", lines({"1.4.6-11~beta.3", "1.4.6-11~rc.1"}), "1.4.6", S("1.4.6-11~rc.1"));
	expect("higher beta beats lower final", lines({"1.4.6-12~beta.1", "1.4.6-11"}), "1.4.6", S("1.4.6-12~beta.1"));
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
	expect("huge app number wins", lines({"1.4.6-10", "1.4.6-" + huge}), "1.4.6", S(("1.4.6-" + huge).c_str()));
	expect("huge beats huge+1 digit loses", lines({"1.4.6-" + huge + "9", "1.4.6-" + huge}), "1.4.6", S(("1.4.6-" + huge + "9").c_str()));
	expect("huge beta", lines({"1.4.6-" + huge + "~beta.1", "1.4.6-10"}), "1.4.6", S(("1.4.6-" + huge + "~beta.1").c_str()));
	expect("huge beta number", lines({"1.4.6-11~beta." + huge, "1.4.6-11~beta." + huge + "9", "1.4.6-11~beta.2"}), "1.4.6", S(("1.4.6-11~beta." + huge + "9").c_str()));
	expect("leading zeros compare numerically", lines({"1.4.6-009", "1.4.6-10"}), "1.4.6", S("1.4.6-10"));
	expect("zero app number", lines({"1.4.6-0"}), "1.4.6", S("1.4.6-0"));
	expect("zero beta beats nothing", lines({"1.4.6-0~beta.1"}), "1.4.6", S("1.4.6-0~beta.1"));

	//duplicates (several archs/components)
	expect("duplicates", lines({"1.4.6-11~beta.1", "1.4.6-11~beta.1", "1.4.6-10", "1.4.6-10", "1.4.6-11~beta.1"}), "1.4.6", S("1.4.6-11~beta.1"));

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
