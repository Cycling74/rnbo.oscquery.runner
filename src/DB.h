#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <mutex>

class DB {
	public:

		DB();
		~DB();

		//get the RNBO versions from DB
		void rnboVersions(std::function<void(const std::string&)> f);

		//NOTE paths are all just file names, use conf to find the actual locations
		void patcherStore(
				const std::string& name,
				const boost::filesystem::path& so_name,
				const boost::filesystem::path& config_name,
				const boost::filesystem::path& rnbo_patch_name,
				const std::string& max_rnbo_version,
				bool migrate_presets,
				int audio_inputs,
				int audio_outputs,
				int midi_inputs,
				int midi_outputs
		);
		bool patcherGetLatest(
				const std::string& name,
				boost::filesystem::path& so_name,
				boost::filesystem::path& config_name,
				boost::filesystem::path& rnbo_patch_name,
				std::string rnbo_version = std::string()
		);
		boost::optional<std::string> patcherNameByIndex(int index);

		void patcherDestroy(const std::string& name, std::function<void(boost::filesystem::path& so_name, boost::filesystem::path& config_name)> f);

		void patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> f, std::string rnbo_version = std::string());

		void presets(const std::string& patchername, std::function<void(const std::string& name, bool isinitial)> f, std::string rnbo_version = std::string());

		//content, name
		boost::optional<std::pair<std::string, std::string>> preset(
				const std::string& patchername,
				const std::string& presetName,
				std::string rnbo_version = std::string()
		);

		//content, name
		//get preset by index, initial preset is always 0, then sorted by name
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

		std::vector<std::string> setPresets(const std::string& setName, std::string rnbo_version = std::string());
		//get all presets for a set perset
		void setPresets(
				const std::string& setName,
				const std::string& presetName,
				std::function<void(const std::string& patcherName, unsigned int instanceIndex, const std::string& content)> func,
				std::string rnbo_version = std::string());

		boost::optional<std::string> setPreset(
				const std::string& patchername,
				const std::string& presetName,
				const std::string& setName,
				unsigned int instanceIndex,
				std::string rnbo_version = std::string()
		);

		void setPresetSave(
				const std::string& patchername,
				const std::string& presetName,
				const std::string& setName,
				unsigned int instanceIndex,
				const std::string& content
		);

		void setPresetRename(
				const std::string& setName,
				const std::string& oldName,
				const std::string& newName,
				std::string rnbo_version = std::string()
		);

		void setPresetDestroy(
				const std::string& setName,
				const std::string& presetName,
				std::string rnbo_version = std::string()
		);

		void setSave(
				const std::string& name,
				const boost::filesystem::path& filename,
				bool migrate_presets = true
		);
		bool setDestroy(const std::string& name);
		bool setRename(const std::string& oldName, const std::string& newName);

		boost::optional<boost::filesystem::path> setGet(
				const std::string& name,
				std::string rnbo_version = std::string()
		);

		void sets(
				std::function<void(const std::string& name, const std::string& created)> func,
				std::string rnbo_version = std::string()
		);

		//returns true if anything happened
		bool listenersAdd(const std::string& ip, uint16_t port);
		bool listenersDel(const std::string& ip, uint16_t port);
		void listenersClear();
		void listeners(std::function<void(const std::string& ip, uint16_t port)> func);

	private:
		SQLite::Database mDB;
		std::mutex mMutex;
};
