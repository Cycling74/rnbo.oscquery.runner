#pragma once

#include <boost/filesystem.hpp>

namespace runner {

// Move a closed regular file, replacing an existing destination. Only EXDEV
// falls back to copy-and-remove. On copy failure the source is retained, but
// the destination may be partial; the fallback is not atomic. A remove failure
// is reported too, in which case both complete files remain.
void move_file(const boost::filesystem::path& source,
		const boost::filesystem::path& destination, boost::system::error_code& ec);

// Copy a closed file to a unique sibling temporary file, then rename it over
// destination. The source is retained, and existing destination handles keep
// their old inode. Copy/rename failure leaves the destination unchanged and
// removes staging data. Publication is atomic, but not power-loss durable.
void copy_file_atomic(const boost::filesystem::path& source,
		const boost::filesystem::path& destination, boost::system::error_code& ec);

// Prepare temporary storage. Only the default directory is cleaned, and only
// when no other runner holds it. Keep this guard alive while using TempDir.
class TempDirectory {
public:
	TempDirectory(const boost::filesystem::path& directory,
			const boost::filesystem::path& defaultDirectory);
	~TempDirectory();
	TempDirectory(const TempDirectory&) = delete;
	TempDirectory& operator=(const TempDirectory&) = delete;

private:
	int mLock = -1;
};

}
