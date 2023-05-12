#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <boost/filesystem.hpp>

class DB {
	public:

		DB();
		~DB();

		//NOTE paths are all just file names, use conf to find the actual locations
		void patcherStore(
				const std::string& name,
				const boost::filesystem::path& so_name,
				const boost::filesystem::path& config_name,
				const std::string& max_rnbo_version
		);
		bool patcherGetLatest(
				const std::string& name,
				boost::filesystem::path& so_name,
				boost::filesystem::path& config_name,
				std::string& created_at
		);
	private:
		SQLite::Database mDB;
};
