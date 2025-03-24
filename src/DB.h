#pragma once

#include "RNBO.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <mutex>
#include <vector>
#include <unordered_map>

struct SetConnectionInfo {
	std::string source_name;
	int source_instance_index = -1;
	std::string source_port_name;

	std::string sink_name;
	int sink_instance_index = -1;
	std::string sink_port_name;

	SetConnectionInfo(
			std::string src_name, std::string src_port_name,
			std::string snk_name, std::string snk_port_name
	) : source_name(src_name), source_port_name(src_port_name),
			sink_name(snk_name), sink_port_name(snk_port_name)
	{}

	SetConnectionInfo() {}

	RNBO::Json toJson();
	static SetConnectionInfo fromJson(const RNBO::Json& json);
};

struct SetInstanceInfo {
	std::string patcher_name;
	unsigned int instance_index;
	std::string config = "{}";

	SetInstanceInfo(
			std::string name,
			unsigned int index,
			std::string conf
			) : patcher_name(name), instance_index(index), config(conf) {};
	RNBO::Json toJson();
	static SetInstanceInfo fromJson(const RNBO::Json& json);
};

struct SetInfo {
	std::vector<SetConnectionInfo> connections;
	std::vector<SetInstanceInfo> instances;
	std::string meta = "{}";

	RNBO::Json toJson();
	static SetInfo fromJson(const RNBO::Json& json);
};

struct ViewParam {
	int instance_index;
	std::string param_id;
	ViewParam(int index, std::string id) : instance_index(index), param_id(id) {}
	static ViewParam fromString(const std::string& v);
};

class DB {
	public:

		DB();
		~DB();

		//get the RNBO versions from DB
		void rnboVersions(std::function<void(const std::string&)> f);

		boost::optional<std::string> migrationDataAvailable();
		void markDataMigrated();

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
		void patcherRename(const std::string& name, std::string& newName);

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
				std::function<void(const std::string& patcherName, unsigned int instanceIndex, const std::string& content, const std::string& patcherPresetName)> func,
				std::string rnbo_version = std::string());

		//find the name of a set preset by index
		//"initial" is always at 0, otherwise sorted by name
		boost::optional<std::string> setPresetNameByIndex(
				const std::string& setName,
				unsigned int index,
				std::string rnbo_version = std::string());

		//pair is content and potential patcher preset name
		boost::optional<std::pair<std::string, std::string>> setPreset(
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
				const std::string& content,
				std::string patcherPresetName = std::string()
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

		void setPresetDestroyAll(
				const std::string& setName,
				std::string rnbo_version = std::string()
		);

		void setSave(
				const std::string& name,
				const SetInfo& info
		);

		boost::optional<SetInfo> setGet(
				const std::string& name,
				std::string rnbo_version = std::string()
		);

		bool setDestroy(const std::string& name);
		bool setInitial(const std::string& name);
		bool setRename(const std::string& oldName, const std::string& newName);

		bool setMatchesConnections(const std::string& name, const std::vector<std::string>& source, const std::vector<std::vector<std::string>>& dest);

		boost::optional<std::string> setNameInitial(
				std::string rnbo_version = std::string()
		);

		//alphabetical
		boost::optional<std::string> setNameByIndex(
				unsigned int index,
				std::string rnbo_version = std::string()
		);

		void sets(
				std::function<void(const std::string& name, const std::string& created, bool initial)> func,
				std::string rnbo_version = std::string()
		);

		//order by sort_order
		std::vector<int> setViewIndexes(const std::string& setName, std::string rnbo_version = std::string());
		boost::optional<std::tuple<
			std::string,
			std::vector<std::string>,
			int
		>> setViewGet(const std::string& setname, int viewIndex, std::string rnbo_version = std::string());

		int setViewCreate(
				const std::string& setname,
				const std::string& viewname,
				const std::vector<ViewParam> params,
				int index = -1 //less than zero means find next index
		);

		//negative to destroy all
		void setViewDestroy(
				const std::string& setname,
				int viewIndex
		);

		void setViewUpdateParams(
				const std::string& setname,
				int viewIndex,
				const std::vector<ViewParam> params
		);

		void setViewUpdateName(
				const std::string& setname,
				int viewIndex,
				const std::string& name
		);

		//returns true if indexes list was altered
		bool setViewsUpdateSortOrder(
				const std::string& setname,
				std::vector<int>& indexes
		);

		void setViewsCopy(
				const std::string& srcSetName,
				const std::string& dstSetName);

		bool listenerExists(const std::string& ip, uint16_t port);
		//returns true if anything happened
		bool listenersAdd(const std::string& ip, uint16_t port);
		bool listenersDel(const std::string& ip, uint16_t port);
		void listenersClear();
		void listeners(std::function<void(const std::string& ip, uint16_t port)> func);

	private:
		SQLite::Database mDB;
		std::mutex mMutex;
};
