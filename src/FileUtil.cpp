#include "FileUtil.h"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace fs = boost::filesystem;

namespace runner {

void move_file(const fs::path& source, const fs::path& destination, boost::system::error_code& ec) {
	fs::rename(source, destination, ec);
	if (ec != boost::system::errc::cross_device_link)
		return;

	// copy_file follows symlinks, unlike rename. Do not accidentally overwrite
	// a symlink's target or turn a source symlink into a regular file.
	auto sourceStatus = fs::symlink_status(source, ec);
	if (ec)
		return;
	if (!fs::is_regular_file(sourceStatus)) {
		ec = make_error_code(boost::system::errc::operation_not_supported);
		return;
	}
	auto destinationStatus = fs::symlink_status(destination, ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		return;
	if (fs::is_symlink(destinationStatus)) {
		ec = make_error_code(boost::system::errc::operation_not_supported);
		return;
	}
	ec.clear();
	if (!fs::copy_file(source, destination, fs::copy_option::overwrite_if_exists, ec))
		return;
	fs::remove(source, ec);
}

void copy_file_atomic(const fs::path& source, const fs::path& destination, boost::system::error_code& ec) {
	std::string temporary = (destination.parent_path() / ".rnbo-restore-XXXXXX").string();
	const int fd = ::mkstemp(temporary.data());
	if (fd < 0) {
		ec.assign(errno, boost::system::generic_category());
		return;
	}
	struct Cleanup {
		const std::string& path;
		~Cleanup() {
			boost::system::error_code ignored;
			fs::remove(path, ignored);
		}
	} cleanup{temporary};
	if (::close(fd) != 0) {
		ec.assign(errno, boost::system::generic_category());
		return;
	}
	// Boost checks writing and closing the copy before returning success.
	if (!fs::copy_file(source, temporary, fs::copy_option::overwrite_if_exists, ec))
		return;
	// Deliberately do not use move_file: publication must never copy over the
	// destination's inode, even if the directory's mount changes unexpectedly.
	fs::rename(temporary, destination, ec);
}

TempDirectory::TempDirectory(const fs::path& directory, const fs::path& defaultDirectory) {
	fs::create_directories(directory);
	if (directory.lexically_normal() != defaultDirectory.lexically_normal())
		return;

	// Never recursively clean a redirected cache directory.
	if (fs::is_symlink(fs::symlink_status(directory)))
		return;

	const auto lockPath = directory / ".runner.lock";
	mLock = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (mLock < 0)
		throw fs::filesystem_error("open temporary directory lock", lockPath,
				boost::system::error_code(errno, boost::system::generic_category()));

	try {
		if (::flock(mLock, LOCK_EX | LOCK_NB) == 0) {
			for (const auto& entry : fs::directory_iterator(directory)) {
				if (entry.path() != lockPath)
					fs::remove_all(entry.path());
			}
		} else if (errno != EWOULDBLOCK && errno != EAGAIN) {
			throw fs::filesystem_error("lock temporary directory", lockPath,
					boost::system::error_code(errno, boost::system::generic_category()));
		}

		// Wait for any startup cleaner, then retain a shared lock for our lifetime.
		// Never unlink the lock file: all runners must lock the same inode.
		if (::flock(mLock, LOCK_SH) != 0)
			throw fs::filesystem_error("share temporary directory lock", lockPath,
					boost::system::error_code(errno, boost::system::generic_category()));
	} catch (...) {
		::close(mLock);
		mLock = -1;
		throw;
	}
}

TempDirectory::~TempDirectory() {
	if (mLock >= 0)
		::close(mLock);
}

}
