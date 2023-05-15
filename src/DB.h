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
				const std::string& max_rnbo_version,
				int audio_inputs,
				int audio_outputs,
				int midi_inputs,
				int midi_outputs
		);
		bool patcherGetLatest(
				const std::string& name,
				boost::filesystem::path& so_name,
				boost::filesystem::path& config_name
		);

		void patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> f);
	private:
		SQLite::Database mDB;
};