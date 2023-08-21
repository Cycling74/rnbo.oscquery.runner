#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <mutex>

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

		void presets(const std::string& patchername, std::function<void(const std::string& name, bool isinitial)> f);
		boost::optional<std::string> preset(
				const std::string& patchername,
				const std::string& presetName
		);
		void presetSave(
				const std::string& patchername,
				const std::string& presetName,
				const std::string& preset
		);
		void presetSetInitial(
				const std::string& patchername,
				const std::string& presetName
		);
		void presetDestroy(
				const std::string& patchername,
				const std::string& presetName
		);

		void setSave(
				const std::string& name,
				const boost::filesystem::path& filename
		);

		boost::optional<boost::filesystem::path> setGet(
				const std::string& name
		);

		void sets(std::function<void(const std::string& name, const std::string& created)> func);
	private:
		SQLite::Database mDB;
		std::mutex mMutex;
};
