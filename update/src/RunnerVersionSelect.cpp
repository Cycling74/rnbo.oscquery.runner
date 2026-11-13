#include "RunnerVersionSelect.h"

namespace {
	bool isDigit(char c) { return c >= '0' && c <= '9'; }
	bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

	std::string trim(const std::string& s) {
		const char *ws = " \t\r\n";
		auto b = s.find_first_not_of(ws);
		if (b == std::string::npos) {
			return "";
		}
		auto e = s.find_last_not_of(ws);
		return s.substr(b, e - b + 1);
	}

	//dpkg's order()
	int order(char c) {
		if (isDigit(c)) {
			return 0;
		} else if (isAlpha(c)) {
			return c;
		} else if (c == '~') {
			return -1;
		} else if (c) {
			return static_cast<unsigned char>(c) + 256;
		}
		return 0;
	}

	//the characters that are legal in a debian version (minus '-', which we split on)
	bool validPre(const std::string& pre) {
		if (pre.empty()) {
			return false;
		}
		for (char c: pre) {
			if (!isDigit(c) && !isAlpha(c) && c != '.' && c != '+' && c != '~') {
				return false;
			}
		}
		return true;
	}

	struct Candidate {
		std::string digits; //app number, without leading zeros, may be empty (zero)
		bool final;
		std::string pre; //"~pre" if !final, otherwise empty
		std::string version; //original apt version string
	};

	//numeric compare of digit strings with no leading zeros, no overflow possible
	int cmpNumber(const std::string& a, const std::string& b) {
		if (a.size() != b.size()) {
			return a.size() < b.size() ? -1 : 1;
		}
		return a.compare(b);
	}

	//<0 if a is older than b
	int cmpCandidate(const Candidate& a, const Candidate& b) {
		if (int c = cmpNumber(a.digits, b.digits)) {
			return c;
		}
		if (a.final != b.final) {
			return a.final ? 1 : -1;
		}
		return a.final ? 0 : runner_version::verrevcmp(a.pre, b.pre);
	}
}

namespace runner_version {
	int verrevcmp(const std::string& a, const std::string& b) {
		auto at = [](const std::string& s, size_t i) { return i < s.size() ? s[i] : '\0'; };
		size_t i = 0, j = 0;
		while (at(a, i) || at(b, j)) {
			int firstDiff = 0;
			while ((at(a, i) && !isDigit(at(a, i))) || (at(b, j) && !isDigit(at(b, j)))) {
				int ac = order(at(a, i));
				int bc = order(at(b, j));
				if (ac != bc) {
					return ac - bc;
				}
				i++;
				j++;
			}
			while (at(a, i) == '0') {
				i++;
			}
			while (at(b, j) == '0') {
				j++;
			}
			while (isDigit(at(a, i)) && isDigit(at(b, j))) {
				if (!firstDiff) {
					firstDiff = at(a, i) - at(b, j);
				}
				i++;
				j++;
			}
			if (isDigit(at(a, i))) {
				return 1;
			}
			if (isDigit(at(b, j))) {
				return -1;
			}
			if (firstDiff) {
				return firstDiff;
			}
		}
		return 0;
	}

	std::optional<std::string> selectLatest(
			const std::vector<std::string>& madisonLines,
			const std::string& packageName,
			const std::string& libVersion,
			bool* exact
	) {
		std::optional<Candidate> best;
		if (exact) {
			*exact = false;
		}

		for (const auto& line: madisonLines) {
			//example line
			//rnbooscquery | 1.4.0-dev.0-2 | https://c74-apt.nyc3.digitaloceanspaces.com/raspbian bookworm/beta armhf Packages
			auto p1 = line.find('|');
			if (p1 == std::string::npos) {
				continue;
			}
			//Require the second separator: callers may hand us lines read through a fixed size buffer,
			//and a version cut short by that must not be mistaken for a real one.
			auto p2 = line.find('|', p1 + 1);
			if (p2 == std::string::npos) {
				continue;
			}

			if (trim(line.substr(0, p1)) != packageName) {
				continue;
			}
			const std::string version = trim(line.substr(p1 + 1, p2 - p1 - 1));

			if (version == libVersion) {
				if (exact) {
					*exact = true;
				}
				continue;
			}

			//split at the LAST '-', library versions can contain dashes
			auto dash = version.rfind('-');
			if (dash == std::string::npos || version.compare(0, dash, libVersion) != 0) {
				continue;
			}

			//suffix is strictly `N` or `N~pre`
			const std::string suffix = version.substr(dash + 1);
			size_t n = 0;
			while (n < suffix.size() && isDigit(suffix[n])) {
				n++;
			}
			if (n == 0) {
				continue;
			}

			Candidate c;
			c.version = version;
			c.final = n == suffix.size();
			if (!c.final) {
				if (suffix[n] != '~' || !validPre(suffix.substr(n + 1))) {
					continue;
				}
				c.pre = suffix.substr(n);
			}
			auto nz = suffix.find_first_not_of('0');
			c.digits = (nz == std::string::npos || nz >= n) ? "" : suffix.substr(nz, n - nz);

			if (!best || cmpCandidate(*best, c) < 0) {
				best = std::move(c);
			}
		}

		if (best) {
			return best->version;
		}
		return std::nullopt;
	}
}
