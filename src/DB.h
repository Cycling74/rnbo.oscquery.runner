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
				const boost::filesystem::path& rnbo_patch_name,
				const std::string& max_rnbo_version,
				int audio_inputs,
				int audio_outputs,
				int midi_inputs,
				int midi_outputs
		);
		bool patcherGetLatest(
				const std::string& name,
				boost::filesystem::path& so_name,
				boost::filesystem::path& config_name,
				boost::filesystem::path& rnbo_patch_name
		);
		boost::optional<std::string> patcherNameByIndex(int index);

		void patcherDestroy(const std::string& name, std::function<void(boost::filesystem::path& so_name, boost::filesystem::path& config_name)> f);

		void patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> f);

		void presets(const std::string& patchername, std::function<void(const std::string& name, bool isinitial)> f);
		//content, name
		boost::optional<std::pair<std::string, std::string>> preset(
				const std::string& patchername,
				const std::string& presetName
		);
		//content, name
		boost::optional<std::pair<std::string, std::string>> preset(
				const std::string& patchername,
				unsigned int index
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
		void presetRename(
				const std::string& patchername,
				const std::string& oldName,
				const std::string& newName
		);
		void presetDestroy(
				const std::string& patchername,
				const std::string& presetName
		);

		void setSave(
				const std::string& name,
				const boost::filesystem::path& filename
		);
		bool setDestroy(const std::string& name);
		bool setRename(const std::string& oldName, const std::string& newName);

		boost::optional<boost::filesystem::path> setGet(
				const std::string& name
		);

		void sets(std::function<void(const std::string& name, const std::string& created)> func);
	private:
		SQLite::Database mDB;
		std::mutex mMutex;
};
