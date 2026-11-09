#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch.hpp"
#include "FileUtil.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = boost::filesystem;

namespace {
struct Scratch {
	fs::path path;
	explicit Scratch(fs::path parent = fs::temp_directory_path())
		: path(parent / fs::unique_path("rnbo-test-%%%%-%%%%-%%%%")) {
		fs::create_directories(path);
	}
	~Scratch() {
		boost::system::error_code ec;
		fs::remove_all(path, ec);
	}
};

void write(const fs::path& path, const std::string& content) {
	std::ofstream stream(path.string(), std::ios::binary);
	stream << content;
	stream.close();
	REQUIRE(stream.good());
}

std::string read(const fs::path& path) {
	std::ifstream stream(path.string(), std::ios::binary);
	return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

TEST_CASE("Same-filesystem moves rename the file and replace existing destinations") {
	Scratch scratch;
	auto source = scratch.path / "source";
	auto destination = scratch.path / "destination";
	write(source, "new audio");
	write(destination, "old audio");
	struct stat before{}, after{};
	REQUIRE(::stat(source.c_str(), &before) == 0);
	boost::system::error_code ec = make_error_code(boost::system::errc::io_error);
	runner::move_file(source, destination, ec);
	REQUIRE_FALSE(ec);
	REQUIRE_FALSE(fs::exists(source));
	REQUIRE(read(destination) == "new audio");
	REQUIRE(::stat(destination.c_str(), &after) == 0);
	REQUIRE(before.st_ino == after.st_ino);
}

TEST_CASE("Other rename failures retain the source and existing destination") {
	Scratch scratch;
	auto source = scratch.path / "source";
	auto destination = scratch.path / "directory";
	write(source, "audio");
	fs::create_directory(destination);
	write(destination / "keep", "existing");
	boost::system::error_code ec;
	runner::move_file(source, destination, ec);
	REQUIRE(ec);
	REQUIRE(ec != boost::system::errc::cross_device_link);
	REQUIRE(read(source) == "audio");
	REQUIRE(read(destination / "keep") == "existing");
	runner::move_file(scratch.path / "missing", source, ec);
	REQUIRE(ec == boost::system::errc::no_such_file_or_directory);
	REQUIRE(read(source) == "audio");
}

TEST_CASE("Atomic replacement isolates existing open handles from restored data") {
	Scratch scratch;
	auto source = scratch.path / "restore.sqlite";
	auto destination = scratch.path / "live.sqlite";
	write(source, "restored database");
	write(destination, "old database");
	std::fstream old(destination.string(), std::ios::in | std::ios::out | std::ios::binary);
	REQUIRE(old.good());
	struct stat before{}, after{};
	REQUIRE(::stat(destination.c_str(), &before) == 0);
	boost::system::error_code ec;
	runner::copy_file_atomic(source, destination, ec);
	REQUIRE_FALSE(ec);
	REQUIRE(::stat(destination.c_str(), &after) == 0);
	REQUIRE(before.st_ino != after.st_ino);
	REQUIRE(read(destination) == "restored database");
	REQUIRE(read(source) == "restored database");
	// Model a late write through a handle retained by a stopping instance.
	old << "late write to the old database";
	old.flush();
	REQUIRE(old.good());
	REQUIRE(read(destination) == "restored database");
	REQUIRE(std::distance(fs::directory_iterator(scratch.path), fs::directory_iterator()) == 2);
}

TEST_CASE("Failed atomic replacement preserves the destination and removes staging data") {
	Scratch scratch;
	auto source = scratch.path / "restore.sqlite";
	auto destination = scratch.path / "live.sqlite";
	boost::system::error_code ec;
	SECTION("copy failure") {
		write(destination, "current database");
		runner::copy_file_atomic(source, destination, ec);
		REQUIRE(ec);
		REQUIRE(read(destination) == "current database");
		REQUIRE(std::distance(fs::directory_iterator(scratch.path), fs::directory_iterator()) == 1);
	}
	SECTION("rename failure") {
		write(source, "restored database");
		fs::create_directory(destination);
		write(destination / "keep", "existing");
		runner::copy_file_atomic(source, destination, ec);
		REQUIRE(ec);
		REQUIRE(read(destination / "keep") == "existing");
		REQUIRE(read(source) == "restored database");
		REQUIRE(std::distance(fs::directory_iterator(scratch.path), fs::directory_iterator()) == 2);
	}
}

TEST_CASE("Cross-filesystem moves copy complete data and retain sources on failure", "[cross-filesystem]") {
	const auto other = std::getenv("RNBO_TEST_OTHER_FS");
	if (!other) {
		WARN("Set RNBO_TEST_OTHER_FS to a directory on another filesystem to exercise EXDEV");
		return;
	}
	Scratch scratch;
	Scratch target(other);
	auto source = scratch.path / "source";
	auto destination = target.path / "destination";
	const std::string content(128 * 1024, 'a');
	write(source, content);
	boost::system::error_code ec;
	fs::rename(source, destination, ec);
	REQUIRE(ec == boost::system::errc::cross_device_link);

	SECTION("new destination") {
		runner::move_file(source, destination, ec);
		REQUIRE_FALSE(ec);
		REQUIRE_FALSE(fs::exists(source));
		REQUIRE(read(destination) == content);
	}
	SECTION("replace destination") {
		write(destination, "old");
		runner::move_file(source, destination, ec);
		REQUIRE_FALSE(ec);
		REQUIRE_FALSE(fs::exists(source));
		REQUIRE(read(destination) == content);
	}
	SECTION("atomic copy stages on the destination filesystem") {
		write(destination, "old database");
		runner::copy_file_atomic(source, destination, ec);
		REQUIRE_FALSE(ec);
		REQUIRE(read(source) == content);
		REQUIRE(read(destination) == content);
		REQUIRE(std::distance(fs::directory_iterator(target.path), fs::directory_iterator()) == 1);
	}
	SECTION("copy fails") {
		fs::create_directory(destination);
		runner::move_file(source, destination, ec);
		REQUIRE(ec);
		REQUIRE(read(source) == content);
		REQUIRE(fs::is_directory(destination));
	}
	SECTION("destination symlink does not overwrite its target") {
		write(target.path / "keep", "existing");
		fs::create_symlink(target.path / "keep", destination);
		runner::move_file(source, destination, ec);
		REQUIRE(ec == boost::system::errc::operation_not_supported);
		REQUIRE(read(source) == content);
		REQUIRE(read(target.path / "keep") == "existing");
	}
}

TEST_CASE("Full destination filesystem retains the source", "[cross-filesystem]") {
	const auto small = std::getenv("RNBO_TEST_SMALL_FS");
	if (!small) {
		WARN("Set RNBO_TEST_SMALL_FS to a small separate tmpfs to exercise disk-full errors");
		return;
	}
	Scratch scratch;
	Scratch target(small);
	const std::string content(2 * 1024 * 1024, 'b');
	write(scratch.path / "source", content);
	REQUIRE(fs::space(target.path).available < content.size());
	boost::system::error_code ec;
	SECTION("move retains the source") {
		runner::move_file(scratch.path / "source", target.path / "destination", ec);
	}
	SECTION("atomic copy also preserves the original destination") {
		write(target.path / "destination", "current database");
		runner::copy_file_atomic(scratch.path / "source", target.path / "destination", ec);
		REQUIRE(read(target.path / "destination") == "current database");
		REQUIRE(std::distance(fs::directory_iterator(target.path), fs::directory_iterator()) == 1);
	}
	REQUIRE(ec == boost::system::errc::no_space_on_device);
	REQUIRE(read(scratch.path / "source") == content);
}

TEST_CASE("Startup cleans only an unused default temporary directory") {
	Scratch scratch;
	auto directory = scratch.path / "default";
	auto custom = scratch.path / "custom";
	fs::create_directories(directory / "stale-package");
	fs::create_directories(custom);
	write(directory / "stale-package" / "file", "stale");
	write(directory / "stale.wav", "stale");
	write(custom / "keep", "custom data");
	fs::create_directory_symlink(custom, directory / "link");
	{
		runner::TempDirectory guard(directory, directory);
		REQUIRE_FALSE(fs::exists(directory / "stale-package"));
		REQUIRE_FALSE(fs::exists(directory / "stale.wav"));
		REQUIRE_FALSE(fs::exists(directory / "link"));
		REQUIRE(read(custom / "keep") == "custom data");
		write(directory / "active.wav", "in progress");
		const auto child = ::fork();
		REQUIRE(child >= 0);
		if (child == 0) {
			try {
				runner::TempDirectory second(directory, directory);
				::_exit(fs::exists(directory / "active.wav") ? 0 : 1);
			} catch (...) {
				::_exit(2);
			}
		}
		int status = 0;
		REQUIRE(::waitpid(child, &status, 0) == child);
		REQUIRE(WIFEXITED(status));
		REQUIRE(WEXITSTATUS(status) == 0);
		REQUIRE(read(directory / "active.wav") == "in progress");
	}
	{
		runner::TempDirectory restarted(directory, directory);
		REQUIRE_FALSE(fs::exists(directory / "active.wav"));
		REQUIRE(fs::exists(directory / ".runner.lock"));
	}
	runner::TempDirectory customGuard(custom, directory);
	REQUIRE(read(custom / "keep") == "custom data");
}

TEST_CASE("A symlinked default directory is not cleaned") {
	Scratch scratch;
	auto directory = scratch.path / "default";
	auto target = scratch.path / "target";
	fs::create_directory(target);
	write(target / "keep", "existing");
	fs::create_directory_symlink(target, directory);
	runner::TempDirectory guard(directory, directory);
	REQUIRE(read(target / "keep") == "existing");
}
