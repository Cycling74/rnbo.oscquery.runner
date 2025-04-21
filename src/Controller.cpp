#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <utility>
#include <chrono>
#include <algorithm>
#include <libbase64.h>
#include <iomanip>
#include <regex>

#include "Controller.h"
#include "Config.h"
#include "Defines.h"
#include "JackAudio.h"
#include "PatcherFactory.h"
#include "RNBO_Version.h"
#include "RNBO_LoggerImpl.h"

#include <boost/process.hpp>
#include <boost/process/child.hpp>

#include <boost/uuid/detail/md5.hpp>
#include <boost/algorithm/hex.hpp>

#include <ossia/context.hpp>
#include <ossia/detail/config.hpp>

#include <ossia/protocols/oscquery/oscquery_server_asio.hpp>
#include <ossia/protocols/osc/osc_factory.hpp>

#include <ossia/network/context.hpp>
#include <ossia/network/local/local.hpp>
#include <ossia/network/base/parameter_data.hpp>
#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include <sys/types.h>
#include <signal.h>

#ifdef RNBO_USE_DBUS
#include "RunnerUpdateState.h"
#include "RnboUpdateServiceProxy.h"
#endif

using std::cout;
using std::cerr;
using std::endl;
using std::chrono::steady_clock;

namespace fs = boost::filesystem;
namespace bp = boost::process;

namespace {
	static const std::string runner_version(RUNNER_VERSION);
	static const std::string rnbo_version(RNBO_VERSION);
	static const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
	static const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);

#if defined(RNBOOSCQUERY_CXX_COMPILER_ID)
	static const std::string rnbo_compiler_id = std::string(RNBOOSCQUERY_CXX_COMPILER_ID);
#else
	static const std::string rnbo_compiler_id = "unknown";
#endif
#if defined(RNBOOSCQUERY_CXX_COMPILER_VERSION)
	static const std::string rnbo_compiler_version = std::string(RNBOOSCQUERY_CXX_COMPILER_VERSION);
#else
	static const std::string rnbo_compiler_version = "unknown";
#endif

	static std::string build_program("rnbo-compile-so");

	static const std::string rnbo_dylib_suffix(RNBO_DYLIB_SUFFIX);

	static const std::chrono::milliseconds save_debounce_timeout(500);

	static const std::chrono::milliseconds process_poll_period(10);

	static const std::chrono::milliseconds datafile_debounce_timeout(50);

	static const std::string last_file_name = "last";
	static const std::string set_instances_key = "instances";
	static const std::string set_meta_key = "meta";
	static const std::string set_connections_key = "connections";

	static const std::string UNTITLED_SET_NAME = "(untitled)";
	static const std::string LAST_SET_NAME = "RNBO_LAST_SET"; //XXX NO LONGER USED

	ossia::net::node_base * find_or_create_child(ossia::net::node_base * parent, const std::string name) {
			auto c = parent->find_child(name);
			if (!c) {
				c = parent->create_child(name);
			}
			return c;
	}

	std::string escapeFileName(std::string name) {
		const std::regex re(R"([^a-zA-Z0-9_\-])");
		return std::regex_replace(name, re, "");
	}

	fs::path saveFilePath(std::string file_name = std::string(), std::string version = std::string(RNBO::version)) {
		if (file_name.size() == 0) {
			file_name = last_file_name;
		} else { //for creation, we add a tag
			//add timetag for unique file paths
			std::string timeTag = std::to_string(std::chrono::seconds(std::time(NULL)).count());
			file_name = escapeFileName(file_name) + "-" + timeTag;
		}
		//add version
		file_name = file_name + "-" + escapeFileName(version) + ".json";
		return config::get<fs::path>(config::key::SaveDir).get() / file_name;
	}

	bool base64_decode_inplace(std::string& v) {
		size_t read = 0;
		std::vector<char> decoded(v.size()); //will be smaller than the in fileName
		if (base64_decode(v.c_str(), v.size(), &decoded.front(), &read, 0) != 1) {
			return false;
		}
		v = std::string(decoded.begin(), decoded.end());
		return true;
	}

	struct CompileInfo {
		bp::group mGroup;
		bp::child mProcess;
		std::string mCommandId;
		RNBO::Json mConf;
		fs::path mConfFilePath;
		fs::path mRNBOPatchPath;
		std::string mMaxRNBOVersion;
		bool mMigratePresets;
		boost::optional<unsigned int> mInstanceIndex;
		fs::path mLibPath;
		CompileInfo(
				std::string command, std::vector<std::string> args,
				fs::path libPath,
				std::string cmdId,
				RNBO::Json conf,
				fs::path confFilePath,
				fs::path rnboPatchPath,
				std::string maxRNBOVersion,
				bool migratePresets,
				boost::optional<unsigned int> instanceIndex
				) :
			mProcess(command, args, mGroup),
			mCommandId(cmdId),
			mConf(conf),
			mConfFilePath(confFilePath),
			mRNBOPatchPath(rnboPatchPath),
			mMaxRNBOVersion(maxRNBOVersion),
			mMigratePresets(migratePresets),
			mLibPath(libPath),
			mInstanceIndex(instanceIndex)
		{ }
		CompileInfo(CompileInfo&&) = default;
		CompileInfo& operator=(CompileInfo&& other) = default;

		CompileInfo(CompileInfo&) = delete;
		CompileInfo& operator=(CompileInfo& other) = delete;

		~CompileInfo() {
			if (mProcess.running()) {
				mGroup.terminate();
				//should we just .wait()
				mProcess.detach();
			}
		}

		bool valid() { return mProcess.valid(); }
	};

	boost::optional<CompileInfo> compileProcess;

	std::string sanitizeName(std::string n) {
		n.erase(std::remove_if(n.begin(), n.end(),
					[](unsigned char x) {
					return !(std::isalnum(x) || x == '-' || x == '_' || x == '.');
					}), n.end());
		return n;
	}

	std::string targetid() {
		static const std::string name = sanitizeName(rnbo_system_processor + "-" + rnbo_system_name + "-" + rnbo_compiler_id + "-" + rnbo_compiler_version);
		return name;
	}

	fs::path packagedir(std::string rnboVersion) {
		return config::get<fs::path>(config::key::PackageDir).get() / sanitizeName(rnboVersion);
	}

	void addSetContent(RNBO::Json& setJson, std::shared_ptr<DB>& db, std::string name, std::string rnboVersion, bool include_presets, bool include_views = true) {
		setJson["name"] = name;
		//get presets
		//XXX could this become too large??
		if (include_presets) {
			RNBO::Json presets = RNBO::Json::object();
			std::vector<std::string> presetNames = db->setPresets(name, rnboVersion);
			if (presetNames.size()) {
				for (auto presetName: presetNames) {
					RNBO::Json preset = RNBO::Json::array();

					db->setPresets(
							name, presetName,
							[&preset](const std::string& patcherName, unsigned int instanceIndex, const std::string& content, const std::string& patcherPresetName) {
							RNBO::Json entry;
							entry["patchername"] = patcherName;
							entry["instanceindex"] = static_cast<int>(instanceIndex);
							if (patcherPresetName.size()) {
								entry["presetname"] = patcherPresetName;
							} else {
								entry["content"] = RNBO::Json::parse(content);
							}
							preset.push_back(entry);
							}, rnboVersion);

					presets[presetName] = preset;
				}

				setJson["presets"] = presets;
			}
		}

		if (include_views) {
			RNBO::Json views = RNBO::Json::array();
			auto indexes = db->setViewIndexes(name, rnboVersion);
			if (indexes.size()) {
				for (auto index: indexes) {
					auto data = db->setViewGet(name, index, rnboVersion);
					if (!data)
						continue;
					RNBO::Json entry;

					entry["index"] = index;
					entry["name"] = std::get<0>(data.get());
					entry["params"] = std::get<1>(data.get());
					entry["sort_order"] = std::get<2>(data.get());

					views.push_back(entry);
				}

				setJson["views"] = views;
			}
		}
	}

	void storeSetContent(const RNBO::Json& setData, std::shared_ptr<DB>& db, std::string setname) {
		//handle presets
		if (setData.contains("presets") && setData["presets"].is_object()) {
			//TODO delete all existing presets?
			for (const auto& kv: setData["presets"].items()) {
				std::string presetName = kv.key();
				db->setPresetDestroy(setname, presetName);
				if (kv.value().is_array()) {
					auto& presets = kv.value();
					for (const auto& entry: presets) {
						if (entry["patchername"].is_string() && entry["instanceindex"].is_number()) {
							std::string patchername = entry["patchername"];
							std::string patcherPresetName;
							unsigned int instanceindex = static_cast<unsigned int>(entry["instanceindex"].get<int>());
							RNBO::Json content = RNBO::Json::object();
							if (entry.contains("content") && entry["content"].is_object())
								content = entry["content"];
							if (entry.contains("presetname") && entry["presetname"].is_string()) {
								patcherPresetName = entry["presetname"];
							}
							db->setPresetSave(patchername, presetName, setname, instanceindex, content.dump(), patcherPresetName);
						} else {
							std::cerr << "don't know how to handle set: " << setname << " preset: " << presetName << " entry" << std::endl;
						}
					}
				}
			}
		}
		if (setData.contains("views") && setData["views"].is_array()) {
			std::vector<int> sort_order;
			//sorted
			for (auto entry: setData["views"]) {
				if (entry.contains("params") && entry.contains("sort_order") && entry.contains("name") && entry.contains("index")) {
					auto index = entry["index"].get<int>();
					auto name = entry["name"].get<std::string>();

					try {
						std::vector<ViewParam> viewParams;
						for (auto p: entry["params"]) {
							viewParams.push_back(ViewParam::fromString(p.get<std::string>()));
						}
						sort_order.push_back(index);
						db->setViewCreate(setname, name, viewParams, index);
					} catch (const std::exception& e) {
						std::cerr << "failed to import param view " << name << std::endl;
					}
				}
			}
			db->setViewsUpdateSortOrder(setname, sort_order);
		}
	}

	struct PackageConfig {
		bool include_presets = true;
		bool include_binaries = true;
		bool include_datafiles = true;
		bool include_views = true;
		bool repackage_existing = true; //do we exit early if a tar already exists with the computed name?
		std::string system_pretty_name;
	};

	void writeJson(const RNBO::Json& data, fs::path filePath) {
		fs::create_directories(filePath.parent_path());
		std::fstream fs;
		fs.open(filePath.string(), std::fstream::out | std::fstream::binary | std::fstream::trunc);
		if (!fs.is_open()) {
			std::string msg = "failed to open file for write: " + filePath.string();
			throw std::runtime_error(msg);
		}

		std::string out = data.dump();
		fs.write(&out.front(), sizeof(char) * out.size());
		fs.close();
	}

	RNBO::Json readJson(fs::path filePath) {
		if (!fs::exists(filePath)) {
			std::string msg = "no file at: " + filePath.string();
			throw std::runtime_error(msg);
		}
		RNBO::Json j;
		std::ifstream i(filePath.string());
		i >> j;
		return j;
	}

	//return tar name
	fs::path createPackage(
			std::shared_ptr<DB>& db,
			std::string packagename,
			std::set<std::string>& setnames,
			std::set<std::string>& patchernames,
			PackageConfig& config,
			std::string rnboVersion
			)
	{
		auto sanitizedPackageName = sanitizeName(packagename + "-rnbo-" + rnboVersion);

		auto exportdir = packagedir(rnboVersion);
		auto tarname = fs::path(sanitizedPackageName + ".rnbopack");
		auto exportlocation = exportdir / tarname;

		if (!config.repackage_existing && fs::exists(exportlocation)) {
			return tarname;
		}

		RNBO::Json info = RNBO::Json::object();
		info["schema_version"] = 1;
		info["name"] = packagename;

		fs::path tmpdir = config::get<fs::path>(config::key::TempDir).get();
		fs::path tmppath = tmpdir / sanitizedPackageName;

		fs::path sourceCache = config::get<fs::path>(config::key::SourceCacheDir).get();
		fs::path compileCache = config::get<fs::path>(config::key::CompileCacheDir).get();

		//remove if it exists
		boost::system::error_code ec;
		fs::remove(exportlocation, ec);
		fs::remove_all(tmppath, ec);

		fs::create_directories(tmppath);

		auto do_copy = [&tmppath](const fs::path& src, const fs::path& location) {
			fs::path dst = tmppath / location;
			if (!fs::exists(dst.parent_path())) {
				fs::create_directories(dst.parent_path());
			}

			if (!fs::copy_file(src, dst)) {
				std::string msg = "failed to copy file name: \"" + src.filename().string();
				throw std::runtime_error(msg);
			}
		};

		std::set<std::string> datafilenames;
		auto append_datarefs = [&datafilenames](const RNBO::Json& content) {
			if (content.contains("datarefs") && content["datarefs"].is_object()) {
				auto datarefs = content["datarefs"];
				for (auto it = datarefs.begin(); it != datarefs.end(); ++it) {
					datafilenames.insert(it.value().get<std::string>());
				}
			}
		};

		RNBO::Json sets = RNBO::Json::array();
		for (auto setname: setnames) {
			auto setInfo = db->setGet(setname, rnboVersion);
			fs::path location = fs::path("sets") / fs::path(sanitizeName(setname) + ".json");
			for (auto inst: setInfo->instances) {
				if (patchernames.count(inst.patcher_name)) {
					continue;
				}
				patchernames.insert(inst.patcher_name);
			}

			RNBO::Json setJson = setInfo->toJson();
			//TODO could the content get too large with presets and views in it??
			addSetContent(setJson, db, setname, rnboVersion, config.include_presets, config.include_views);
			writeJson(setJson, tmppath / location);

			//parse presets for datafiles
			if (config.include_datafiles && setJson.contains("presets")) {
				//key  value, value is an array of entries
				const RNBO::Json& presets = setJson["presets"];
				for (auto pit = presets.begin(); pit != presets.end(); ++pit) {
					const RNBO::Json& entries = pit.value();
					for (auto entry: entries) {
						if (entry.contains("content") && entry["content"].is_object()) {
							const RNBO::Json& preset = entry["content"];
							append_datarefs(preset);
						}
					}
				}
			}

			RNBO::Json setDesc = RNBO::Json::object();
			setDesc["name"] = setname;
			setDesc["location"] = location.string();
			setDesc["created_at"] = setInfo->created_at;

			sets.push_back(setDesc);
		}

		if (setnames.size()) {
			info["sets"] = sets;
		}

		RNBO::Json targets = RNBO::Json::object();
		fs::path targetdir = "targets" / fs::path(targetid());
		if (config.include_binaries) {
			RNBO::Json target = RNBO::Json::object();

			target["system_processor"] = rnbo_system_processor;
			target["system_name"] = rnbo_system_name;
			target["compiler_id"] = rnbo_compiler_id;
			target["compiler_version"] = rnbo_compiler_version;
			target["dir"] = targetdir.string();

			if (config.system_pretty_name.size()) {
				target["system_os_name"] = config.system_pretty_name;
			}

			targets[targetid()] = target;
			info["targets"] = targets;
		}

		info["rnbo_version"] = rnboVersion;
		info["runner_version"] = runner_version;

		fs::path srcdir =  "src";

		RNBO::Json patchers = RNBO::Json::array();
		for (auto patchername: patchernames) {
			fs::path libPath;
			fs::path confPath;
			fs::path patcherPath;
			std::string created_at;

			if (!db->patcherGetLatest(patchername, libPath, confPath, patcherPath, created_at, rnboVersion)) {
				std::string msg = "patcher with name: \"" + patchername + " and rnbo version: " + rnboVersion + " not found";
				throw std::runtime_error(msg);
			}

			libPath = fs::absolute(compileCache / libPath);
			confPath = fs::absolute(sourceCache / confPath);
			patcherPath = fs::absolute(sourceCache / patcherPath);

			RNBO::Json patcherinfo;

			if (config.include_binaries) {
				//binary data
				RNBO::Json binaries = RNBO::Json::object();
				{
					fs::path location = targetdir / "patchers" / libPath.filename();
					do_copy(libPath, location);

					binaries[targetid()] = location.string();
				}
				patcherinfo["binaries"] = binaries;
			}

			//src data
			if (fs::exists(patcherPath)) {
				fs::path location = srcdir / patcherPath.filename();
				do_copy(patcherPath, location);
				patcherinfo["patcher"] = location.string();
			}

			if (fs::exists(confPath)) {
				fs::path location = srcdir / confPath.filename();
				do_copy(confPath, location);
				patcherinfo["config"] = location.string();

				if (config.include_datafiles) {
					RNBO::Json patcherConfig;
					std::ifstream i(confPath.string());
					i >> patcherConfig;
					i.close();
					append_datarefs(patcherConfig);
				}
			}

			if (config.include_presets) {
				fs::path location = srcdir / fs::path(sanitizeName(patchername) + "-presets.json");

				std::vector<std::string> presetnames;
				std::string initialpreset;
				db->presets(patchername, [&presetnames, &initialpreset](const std::string& n, bool isinitial) {
						presetnames.push_back(n);
						if (isinitial) {
						initialpreset = n;
						}
						}, rnboVersion);

				RNBO::Json content = RNBO::Json::object();
				for (auto name: presetnames) {
					auto preset = db->preset(patchername, name, rnboVersion);
					if (preset) {
						RNBO::Json presetJson = RNBO::Json::parse(preset->first);

						if (config.include_datafiles) {
							append_datarefs(presetJson);
						}

						content[name] = presetJson;
					} else {
						throw std::runtime_error("preset does not exist");
					}
				}
				if (initialpreset.size()) {
					patcherinfo["preset_initial"] = initialpreset;
				}

				writeJson(content, tmppath / location);
				patcherinfo["presets"] = location.string();
			}

			//common info
			patcherinfo["name"] = patchername;
			patcherinfo["created_at"] = created_at;

			patchers.push_back(patcherinfo);
		}

		info["patchers"] = patchers;

		if (config.include_datafiles && datafilenames.size()) {
			RNBO::Json datafiles = RNBO::Json::array();

			auto datafiledir = config::get<fs::path>(config::key::DataFileDir).get();
			for (auto name: datafilenames) {
				fs::path src = datafiledir / fs::path(name);
				if (!fs::exists(src)) {
					std::cerr << "cannot find datafile to package at: " << src.string() << " skipping" << std::endl;
					continue;
				}
				fs::path location = fs::path("datafiles") / fs::path(name);

				RNBO::Json datafileinfo = RNBO::Json::object();
				do_copy(src, location);
				datafileinfo["location"] = location.string();
				datafileinfo["name"] = name;
				//TODO any other reasonable info/
				datafiles.push_back(datafileinfo);
			}

			info["datafiles"] = datafiles;
		}

		writeJson(info, tmppath	 / "info.json");

		fs::create_directories(exportlocation.parent_path());
		std::vector<std::string> args { "cf", exportlocation.string(), sanitizedPackageName };
		if (bp::system(bp::search_path("tar"), args, bp::start_dir(tmpdir)) != 0) {
			std::string msg = "failed to create tar file";
			throw std::runtime_error(msg);
		}
		fs::remove_all(tmppath, ec);
		return tarname;
	}

}

//for some reason RNBO's defaualt logger doesn't get to journalctl, using cout does
void RunnerLog(RNBO::LogLevel level, const char* message) {
	const static std::array<const std::string, 3> levelStr = { "[INFO]", "[WARNING]", "[ERROR]" };
	const size_t index = std::min((size_t)level, levelStr.size());
	std::cout << levelStr[index] << "\t" << message << std::endl;
}

Controller::Controller(std::string server_name) {

	RNBO::console->setLoggerOutputCallback(RunnerLog);

	mDB = std::make_shared<DB>();

	//if the old set exists, rename it
	mDB->setRename(LAST_SET_NAME, UNTITLED_SET_NAME);

	mProtocol = new ossia::net::multiplex_protocol();
	mOssiaContext = ossia::net::create_network_context();
	auto serv_proto = new ossia::oscquery_asio::oscquery_server_protocol(mOssiaContext, 1234, 5678);

	mServer = std::unique_ptr<ossia::net::generic_device>(new ossia::net::generic_device(std::unique_ptr<ossia::net::protocol_base>(mProtocol), server_name));
	mServer->set_echo(true);

	mProtocol->expose_to(std::unique_ptr<ossia::net::protocol_base>(serv_proto));
	mServer->on_unhandled_message.connect<&Controller::onUnhandledOSC>(this);

	mSourceCache = config::get<fs::path>(config::key::SourceCacheDir).get();
	mCompileCache = config::get<fs::path>(config::key::CompileCacheDir).get();

	//setup user defined location of the build program, if they've set it
	auto configBuildExe = config::get<fs::path>(config::key::SOBuildExe);
	if (configBuildExe && fs::exists(configBuildExe.get()))
		build_program = configBuildExe.get().string();

	mProcessNext = steady_clock::now();

	auto root = mServer->create_child("rnbo");

	{
		auto rep = root->create_child("listeners");

		{
			auto n = rep->create_child("entries");
			mListenersListParam = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			n->set(ossia::net::description_attribute{}, "list of OSC listeners");
		}

		auto cmdBuilder = [](std::string method, const std::string& payload) -> std::string {
			try {
				//default ip to localhost, maybe override with message payload
				std::string ip = "127.0.0.1";
				int port = 0;
				auto p = payload.find(":");
				if (p != std::string::npos) {
					ip =  payload.substr(0, p);
					port = std::stoi(payload.substr(p + 1, std::string::npos));
				} else {
					port = std::stoi(payload);
				}
				RNBO::Json cmd = {
					{"method", method},
					{"id", "internal"},
					{"params",
						{
							{"ip", ip},
							{"port", port},
						}
					}
				};
				return cmd.dump();
			} catch (...) {
			}
			std::cerr << "failed to make " + method + " command for " + payload << std::endl;
			return {};
		};

		{
			auto n = rep->create_child("add");
			auto p = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "add OSC UDP listener: \"[ip:]port\"");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				std::string addr;
				//address can be just a port or ip:port
				//port can be int or string
				if (v.get_type() == ossia::val_type::STRING) {
					addr = v.get<std::string>();
				} else if (v.get_type() == ossia::val_type::INT) {
					addr = std::to_string(v.get<int>());
				} else {
					return;
				}
				auto cmd = cmdBuilder("listener_add", addr);
				if (cmd.size()) {
					mCommandQueue.push(cmd);
				}
			});
		}

		{
			auto n = rep->create_child("del");
			auto p = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "delete OSC UDP listener: \"[ip:]port\"");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				std::string addr;
				if (v.get_type() == ossia::val_type::STRING) {
					addr = v.get<std::string>();
				} else if (v.get_type() == ossia::val_type::INT) {
					addr = std::to_string(v.get<int>());
				} else {
					return;
				}
				auto cmd = cmdBuilder("listener_del", addr);
				if (cmd.size()) {
					mCommandQueue.push(cmd);
				}
			});
		}
		{
			auto n = rep->create_child("clear");
			auto p = n->create_parameter(ossia::val_type::IMPULSE);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "clear all OSC UDP listeners");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				mCommandQueue.push(cmdBuilder("listener_clear", "0:0"));
			});
		}

		restoreListeners();
	}

	//expose some information
	auto info = root->create_child("info");
	info->set(ossia::net::description_attribute{}, "information about RNBO and the running system");

	for (auto it: {
			std::make_pair("version", rnbo_version),
			std::make_pair("system_name", rnbo_system_name),
			std::make_pair("system_processor", rnbo_system_processor),
			std::make_pair("runner_version", runner_version),
			std::make_pair("target_id", targetid()),
			}) {
		auto n = info->create_child(it.first);
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		p->push_value(it.second);
	}

	//get PRETTY_NAME from os-release if it exists
	try {
		fs::path osRelease("/etc/os-release");
		if (fs::exists(osRelease)) {
			std::ifstream i(osRelease.string());
			std::string line;
			while (std::getline(i, line)) {
				std::size_t found = line.find("PRETTY_NAME");
				if (found != std::string::npos) {
					auto eq = line.find("=");
					if (eq != std::string::npos) {
						std::string name = line.substr(eq + 1);

						//trim quotes
						if (name.size() && name[0] == '"' && name[name.size() - 1] == '"') {
							name = name.substr(1, name.size() - 2);
						}
						mSystemPrettyName = name;

						auto n = info->create_child("system_os_name");
						auto p = n->create_parameter(ossia::val_type::STRING);
						n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

						p->push_value(name);
					} else {
						std::cerr << "/etc/os-release not in expected KEY=VALUE format" << std::endl;
					}
					break;
				}
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "error reading /etc/os-release: " << e.what() << std::endl;
	}

	{
		auto n = info->create_child("system_id");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "a unique, one time generated id for this system");
		p->push_value(config::get_system_id());
	}

	{
		auto n = info->create_child("compiler_version");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "the version of the compiler that this executable was built with");
		p->push_value(rnbo_compiler_version);
	}

	{
		auto n = info->create_child("compiler_id");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "the id of the compiler that this executable was built with");
		p->push_value(rnbo_compiler_id);
	}

	{
		//ossia doesn't seem to support 64bit integers, so we use a string as 31 bits
		//might not be enough to indicate disk space
		{
			auto n = info->create_child("disk_bytes_available");
			mDiskSpaceParam = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			n->set(ossia::net::description_attribute{}, "a byte count (as a string) of the bytes available in the compile cache dir");
		}

		{
			auto n = info->create_child("datafile_dir_mtime");
			mDataFileDirMTimeParam = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			n->set(ossia::net::description_attribute{}, "the last modification time of the datafile directory, as a string representation, time since epoch");
		}

		updateDiskStats();
		updateDatafileStats();
	}

	//add support for commands
	{
		std::vector<ossia::value> supported = {
			"file_write_extended",
			"file_read",
			"file_read64",
			"file_exists",
			"package_create",
			"package_install",
#ifdef RNBOOSCQUERY_ENABLE_COMPILE
			"compile-with_config_file",
			"compile-with_instance_and_name",
#endif
			"instance_load-multi",
			"patcherstore",
			"patcherfilestore"
		};

		auto n = info->create_child("supported_cmds");
		auto p = n->create_parameter(ossia::val_type::LIST);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "details about post 1.0 commands added");
		p->push_value(supported);
	}

	{
		std::vector<ossia::value> unsupported = {
#ifndef RNBOOSCQUERY_ENABLE_COMPILE
			"compile",
			"compile-with_config_file",
			"compile-with_instance_and_name"
#endif
		};

		auto n = info->create_child("unsupported_cmds");
		auto p = n->create_parameter(ossia::val_type::LIST);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "details about post 1.0 commands removed");
		p->push_value(unsupported);
	}

	{
		auto n = root->create_child("cmd");
		auto p = n->create_parameter(ossia::val_type::STRING);

		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
		n->set(ossia::net::description_attribute{}, "command handler");

		p->add_callback([this, p](const ossia::value& v) {
				//libossia reports the value of SET only parameters, so silently set
				//the value to empty when we get an update
				if (v.get_type() == ossia::val_type::STRING) {
					auto s = v.get<std::string>();
					if (s.size() > 0) {
						mCommandQueue.push(s);
						p->set_value_quiet(std::string());
					}
				}
		});
	}
	{
		auto n = root->create_child("resp");
		mResponseParam = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "command response");
	}

	auto j = root->create_child("jack");
	NodeBuilder builder = [j, this](std::function<void(ossia::net::node_base *)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(j);
	};

	mProcessAudio = std::make_shared<ProcessAudioJack>(
			builder,
			std::bind(&Controller::handleProgramChange, this, std::placeholders::_1)
	);

	{
		auto n = j->create_child("active");
		mAudioActive = n->create_parameter(ossia::val_type::BOOL);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		mAudioActive->push_value(mProcessAudio->isActive());

		mAudioActive->add_callback([this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					RNBO::Json cmd = {
						{"method", "activate_audio"},
						{"id", "internal"},
						{"params",
							{
								{"active", v.get<bool>()},
							}
						}
					};
					mCommandQueue.push(cmd.dump());
				}
		});
	}

	{
		auto n = j->create_child("restart");
		auto p = n->create_parameter(ossia::val_type::BOOL);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

		p->add_callback([this](const ossia::value&) {
				RNBO::Json cmd = {
					{"method", "restart_audio"},
					{"id", "internal"},
					{"params",
						{
							{"restart", true},
						}
					}
				};
				mCommandQueue.push(cmd.dump());
		});
	}

	mPatchersNode = root->create_child("patchers");
	mPatchersNode->set(ossia::net::description_attribute{}, "patcher descriptions");

	updatePatchersInfo();

	mInstancesNode = root->create_child("inst");
	mInstancesNode->set(ossia::net::description_attribute{}, "code export instances");
	{
		auto ctl = mInstancesNode->create_child("control");

		{
			auto cmdBuilder = [](std::string method, int index, const std::string patcher_name = std::string(), const std::string instance_name = std::string()) -> std::string {
				RNBO::Json cmd = {
					{"method", method},
					{"id", "internal"},
					{"params",
						{
							{"index", index},
							{"patcher_name", patcher_name},
							{"instance_name", instance_name},
						}
					}
				};
				return cmd.dump();
			};

			{
				auto n = ctl->create_child("unload");
				auto p = n->create_parameter(ossia::val_type::INT);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Unload a running instance by index, negative will unload all");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::INT) {
							auto index = v.get<int>();
							mCommandQueue.push(cmdBuilder("instance_unload", index));
						}
				});

			}

			{
				mInstanceLoadNode = ctl->create_child("load");
				auto p = mInstanceLoadNode->create_parameter(ossia::val_type::LIST);
				mInstanceLoadNode->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				mInstanceLoadNode->set(ossia::net::description_attribute{}, "Load a pre-built patcher by name into the given index. args: index patcher_name [instance_name], a -1 index will create a new instance at the next available index, -2 will do the same but not auto connect it to anything");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
					if (v.get_type() == ossia::val_type::LIST) {
						auto l = v.get<std::vector<ossia::value>>();
						if (l.size() >= 2 && l[1].get_type() == ossia::val_type::STRING && l[0].get_type() == ossia::val_type::INT) {
							auto index = l[0].get<int>();
							auto name = l[1].get<std::string>();
							std::string instance_name;

							if (l.size() > 2 && l[2].get_type() == ossia::val_type::STRING) {
								instance_name = l[2].get<std::string>();
							}

							mCommandQueue.push(cmdBuilder("instance_load", index, name, instance_name));

						}
					}
				});

			}
		}

		{
			auto sets = ctl->create_child("sets");

			auto cmdBuilder = [](std::string method, const std::string& name, std::string meta = std::string()) -> std::string {
				RNBO::Json cmd = {
					{"method", method},
					{"id", "internal"},
					{"params",
						{
							{"name", name},
							{"meta", meta}
						}
					}
				};
				return cmd.dump();
			};

			{
				auto n = sets->create_child("meta");
				mSetMetaParam = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Set/get the metadata for the current set");

				mSetMetaParam->add_callback([this](const ossia::value&) {
					queueSave();
					mSetDirtyParam->push_value(true);
				});
			}

			{
				auto n = sets->create_child("save");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Save a set of instances assigning the given name");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::STRING) {
							auto name = v.get<std::string>();
							if (name.size()) {
								//get metadata
								auto metav = mSetMetaParam->value();
								std::string meta;
								if (metav.get_type() == ossia::val_type::STRING) {
									meta = metav.get<std::string>();
								}
								mCommandQueue.push(cmdBuilder("instance_set_save", name, meta));
							}
						}
				});
			}

			{
				auto n = mSetLoadNode = sets->create_child("load");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Load a set with the given name");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::STRING) {
							auto name = v.get<std::string>();
							if (name.size()) {
								mCommandQueue.push(cmdBuilder("instance_set_load", name));
							}
						}
				});
			}

			{
				auto n = sets->create_child("destroy");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Delete a set of instances with the given name");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::STRING) {
							auto name = v.get<std::string>();
							if (name.size()) {
								mCommandQueue.push(cmdBuilder("instance_set_delete", name));
							}
						}
				});
			}

			{
				auto n = sets->create_child("rename");
				auto p = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				n->set(ossia::net::description_attribute{}, "Rename a set of instances: oldName, newName");

				p->add_callback([this](const ossia::value& v) {
					if (v.get_type() == ossia::val_type::LIST) {
						auto l = v.get<std::vector<ossia::value>>();
						if (l.size() == 2 && l[0].get_type() == ossia::val_type::STRING && l[1].get_type() == ossia::val_type::STRING) {
							auto name = l[0].get<std::string>();
							auto newName = l[1].get<std::string>();
							if (name.size() && newName.size()) {
								RNBO::Json cmd = {
									{"method", "instance_set_rename"},
									{"id", "internal"},
									{"params",
										{
											{"name", name},
											{"newName", newName}
										}
									}
								};
								mCommandQueue.push(cmd.dump());
							}
						}
					}
				});
			}

			//current
			{
				auto current = sets->create_child("current");
				{
					auto n = current->create_child("name");
					mSetCurrentNameParam = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					n->set(ossia::net::description_attribute{}, "The currently loaded set's name");
					mSetCurrentNameParam->push_value(UNTITLED_SET_NAME);
				}

				{
					auto n = current->create_child("dirty");
					mSetDirtyParam = n->create_parameter(ossia::val_type::BOOL);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					n->set(ossia::net::description_attribute{}, "Indication that the current set has unsaved changes");
					mSetDirtyParam->push_value(false);
				}
			}

			{
				auto n = sets->create_child("initial");
				auto p = mSetInitialNameParam = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Give the name of the set (or none) that should load when the runner first starts");

				p->add_callback([this, cmdBuilder](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::STRING) {
							auto name = v.get<std::string>();
							mCommandQueue.push(cmdBuilder("instance_set_initial", name));
						}
				});
			}

			{
				auto presets = sets->create_child("presets");
				{
					auto n = presets->create_child("save");
					auto p = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "Save a loaded set preset with the given name");

					p->add_callback([this, cmdBuilder](const ossia::value& v) {
							if (v.get_type() == ossia::val_type::STRING) {
								auto name = v.get<std::string>();
								if (name.size()) {
									mCommandQueue.push(cmdBuilder("instance_set_preset_save", name));
								}
							}
					});
				}
				{
					auto n = mSetPresetLoadNode = presets->create_child("load");
					auto p = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "Load a loaded set preset with the given name");

					p->add_callback([this, cmdBuilder](const ossia::value& v) {
							if (v.get_type() == ossia::val_type::STRING) {
								auto name = v.get<std::string>();
								if (name.size()) {
									mCommandQueue.push(cmdBuilder("instance_set_preset_load", name));
								}
							}
					});
				}
				{
					auto n = presets->create_child("loaded");
					auto p = mSetPresetLoadedParam = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					n->set(ossia::net::description_attribute{}, "Indicates the last loaded preset");
				}
				{
					auto n = presets->create_child("destroy");
					auto p = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "Delete a loaded set preset with the given name");

					p->add_callback([this, cmdBuilder](const ossia::value& v) {
							if (v.get_type() == ossia::val_type::STRING) {
								auto name = v.get<std::string>();
								if (name.size()) {
									mCommandQueue.push(cmdBuilder("instance_set_preset_delete", name));
								}
							}
					});
				}

				{
					auto n = presets->create_child("rename");
					auto p = n->create_parameter(ossia::val_type::LIST);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "Rename the loaded set's preset: oldName, newName");

					p->add_callback([this](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::LIST) {
							auto l = v.get<std::vector<ossia::value>>();
							if (l.size() == 2 && l[0].get_type() == ossia::val_type::STRING && l[1].get_type() == ossia::val_type::STRING) {
								auto name = l[0].get<std::string>();
								auto newName = l[1].get<std::string>();
								if (name.size() && newName.size()) {
									RNBO::Json cmd = {
										{"method", "instance_set_preset_rename"},
										{"id", "internal"},
										{"params",
											{
												{"name", name},
												{"newName", newName}
											}
										}
									};
									mCommandQueue.push(cmd.dump());
								}
							}
						}
					});
				}
			}

			{
				auto views = sets->create_child("views");
				{
					auto n = views->create_child("create");
					auto p = n->create_parameter(ossia::val_type::LIST);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "create a new set view, first parameter is the name, the rest compose the initial view parameter list");

					p->add_callback([this](const ossia::value& v) {
						RNBO::Json params = RNBO::Json::array();
						if (v.get_type() == ossia::val_type::LIST) {
							std::string name;
							auto l = v.get<std::vector<ossia::value>>();
							for (auto n: l) {
								if (n.get_type() == ossia::val_type::STRING) {
									if (name.size() == 0) {
										name = n.get<std::string>();
									} else {
										params.push_back(n.get<std::string>());
									}
								}
							}
							if (name.size() > 0) {
								RNBO::Json cmd = {
									{"method", "instance_set_view_create"},
									{"id", "internal"},
									{"params",
										{
											{"name", name},
											{"params", params},
										}
									}
								};
								mCommandQueue.push(cmd.dump());
							}
						}
					});
				}
				{
					auto n = views->create_child("destroy");
					auto p = n->create_parameter(ossia::val_type::INT);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "destroy a view via its id, use a negative value to destroy all views");

					p->add_callback([this](const ossia::value& v) {
						if (v.get_type() == ossia::val_type::INT) {
							int index = v.get<int>();
							RNBO::Json cmd = {
								{"method", "instance_set_view_destroy"},
								{"id", "internal"},
								{"params",
									{
										{"index", index},
									}
								}
							};
							mCommandQueue.push(cmd.dump());
						}
					});
				}

				{
					auto n = mSetViewsListNode = views->create_child("list");
				}
				{
					auto n = views->create_child("order");
					auto p = mSetViewsOrderParam = n->create_parameter(ossia::val_type::LIST);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
					n->set(ossia::net::description_attribute{}, "set a sort order for the set views, using their ids");
					p->add_callback([this](const ossia::value& val) {
						RNBO::Json indexes = RNBO::Json::array();
						if (val.get_type() == ossia::val_type::LIST) {
							auto l = val.get<std::vector<ossia::value>>();
							for (auto item: l) {
								if (item.get_type() == ossia::val_type::INT) {
									indexes.push_back(item.get<int>());
								}
							}
						}
						RNBO::Json cmd = {
							{"method", "instance_set_view_order"},
							{"id", "internal"},
							{"params",
								{
									{"order", indexes},
								}
							}
						};
						mCommandQueue.push(cmd.dump());
					});
				}
			}

			updateSetNames();
		}
	}

	std::vector<ossia::value> midivalues;
	for (auto& kv: config_midi_channel_values) {
		midivalues.push_back(kv.first);
	}

	{
		auto conf = mInstancesNode->create_child("config");
		{
			auto key = config::key::InstanceAutoConnectAudio;
			auto n = conf->create_child("auto_connect_audio");
			n->set(ossia::net::description_attribute{}, "Automatically connect newly activated instances to audio i/o");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAutoConnectAudioIndexed;
			auto n = conf->create_child("auto_connect_audio_indexed");
			n->set(ossia::net::description_attribute{}, "Automatically connect newly activated instances to audio i/o, using the RNBO i/o index to inform the connections");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAutoConnectMIDI;
			auto n = conf->create_child("auto_connect_midi");
			n->set(ossia::net::description_attribute{}, "Automatically connect newly activated instances to midi i/o");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAutoConnectMIDIHardware;
			auto n = conf->create_child("auto_connect_midi_hardware");
			n->set(ossia::net::description_attribute{}, "Automatically connect newly activated instances to midi hardware i/o");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAutoConnectPortGroup;
			auto n = conf->create_child("auto_connect_port_group");
			n->set(ossia::net::description_attribute{}, "Automatically connect newly activated instances to midi/audio via the rnbo-graph-user-io port group property");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAutoStartLast;
			auto n = conf->create_child("auto_start_last");
			n->set(ossia::net::description_attribute{}, "Automatically start the last instance configuration on runner startup");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAudioFadeIn;
			auto n = conf->create_child("audio_fade_in");
			n->set(ossia::net::description_attribute{}, "Fade in milliseconds when creating new instances");
			auto p = n->create_parameter(ossia::val_type::FLOAT);
			mInstFadeInMs = static_cast<float>(config::get<double>(key).value_or(10.0));
			p->push_value(mInstFadeInMs);
			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::FLOAT) {
					mInstFadeInMs = v.get<float>();
					config::set(static_cast<double>(mInstFadeInMs), key);
				}
			});
		}
		{
			auto key = config::key::InstanceAudioFadeOut;
			auto n = conf->create_child("audio_fade_out");
			n->set(ossia::net::description_attribute{}, "Fade out milliseconds when closing instances");
			auto p = n->create_parameter(ossia::val_type::FLOAT);
			mInstFadeOutMs = static_cast<float>(config::get<double>(key).value_or(10.0));
			p->push_value(mInstFadeOutMs);
			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::FLOAT) {
					mInstFadeOutMs = v.get<float>();
					config::set(static_cast<double>(mInstFadeOutMs), key);
				}
			});
		}
		{
			auto key = config::key::InstancePortToOSC;
			auto n = conf->create_child("port_to_osc");
			n->set(ossia::net::description_attribute{}, "Map slash prefixed port names to/from OSC messages by default");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(true));
			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::PresetMIDIProgramChangeChannel;
			auto n = conf->create_child(key);

			n->set(ossia::net::description_attribute{}, "Which channel (or none or omni) should listen for program changes to load a preset by index");
			auto dom = ossia::init_domain(ossia::val_type::STRING);
			ossia::set_values(dom, midivalues);
			n->set(ossia::net::domain_attribute{}, dom);

			auto p = n->create_parameter(ossia::val_type::STRING);

			auto s = config::get<std::string>(key).value_or("none");
			if (config_midi_channel_values.find(s) == config_midi_channel_values.end()) {
				s = "none";
			}
			p->push_value(s);
			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					std::string s = v.get<std::string>();
					if (config_midi_channel_values.find(s) != config_midi_channel_values.end()) {
						config::set(s, key);
					}
				}
			});
		}
	}

	{
		auto conf = root->create_child("config");
		{
			auto key = config::key::PatcherMIDIProgramChangeChannel;
			auto n = conf->create_child(key);
			n->set(ossia::net::description_attribute{}, "Which channel (or none or omni) should listen for program changes to load a patcher by index");
			auto dom = ossia::init_domain(ossia::val_type::STRING);
			ossia::set_values(dom, midivalues);
			n->set(ossia::net::domain_attribute{}, dom);

			auto p = n->create_parameter(ossia::val_type::STRING);

			auto s = config::get<std::string>(key).value_or("none");
			auto it = config_midi_channel_values.find(s);
			if (it != config_midi_channel_values.end()) {
				mPatcherProgramChangeChannel = it->second;
			} else {
				s = "none";
			}
			p->push_value(s);

			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					std::string s = v.get<std::string>();
					auto it = config_midi_channel_values.find(s);
					if (it != config_midi_channel_values.end()) {
						mPatcherProgramChangeChannel = it->second;
						config::set(s, key);
					}
				}
			});
		}
		{
			auto key = config::key::SetMIDIProgramChangeChannel;
			auto n = conf->create_child(key);
			n->set(ossia::net::description_attribute{}, "Which channel (or none or omni) should listen for program changes to load a set by index");
			auto dom = ossia::init_domain(ossia::val_type::STRING);
			ossia::set_values(dom, midivalues);
			n->set(ossia::net::domain_attribute{}, dom);

			auto p = n->create_parameter(ossia::val_type::STRING);

			auto s = config::get<std::string>(key).value_or("none");
			auto it = config_midi_channel_values.find(s);
			if (it != config_midi_channel_values.end()) {
				mSetProgramChangeChannel = it->second;
			} else {
				s = "none";
			}
			p->push_value(s);

			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					std::string s = v.get<std::string>();
					auto it = config_midi_channel_values.find(s);
					if (it != config_midi_channel_values.end()) {
						mSetProgramChangeChannel = it->second;
						config::set(s, key);
					}
				}
			});
		}
		{
			auto key = config::key::SetPresetMIDIProgramChangeChannel;
			auto n = conf->create_child(key);
			n->set(ossia::net::description_attribute{}, "Which channel (or none or omni) should listen for program changes to load a set preset by index");
			auto dom = ossia::init_domain(ossia::val_type::STRING);
			ossia::set_values(dom, midivalues);
			n->set(ossia::net::domain_attribute{}, dom);

			auto p = n->create_parameter(ossia::val_type::STRING);

			auto s = config::get<std::string>(key).value_or("none");
			auto it = config_midi_channel_values.find(s);
			if (it != config_midi_channel_values.end()) {
				mSetPresetProgramChangeChannel = it->second;
			} else {
				s = "none";
			}
			p->push_value(s);

			p->add_callback([key, this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					std::string s = v.get<std::string>();
					auto it = config_midi_channel_values.find(s);
					if (it != config_midi_channel_values.end()) {
						mSetPresetProgramChangeChannel = it->second;
						config::set(s, key);
					}
				}
			});
		}
		{
			auto key = config::key::ControlAutoConnectMIDI;
			auto n = conf->create_child(key);
			n->set(ossia::net::description_attribute{}, "Automatically connect control to midi outs");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
		{
			auto key = config::key::SetPresetDefaultPatcherNamed;
			auto n = conf->create_child(key);
			n->set(ossia::net::description_attribute{}, "Default set presets store latest preset loaded in instance instead of all of its parameter values");
			auto p = n->create_parameter(ossia::val_type::BOOL);
			p->push_value(config::get<bool>(key).value_or(false));
			p->add_callback([key](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					config::set(v.get<bool>(), key);
				}
			});
		}
	}


	bool supports_install = false;
	auto update = info->create_child("update");
	update->set(ossia::net::description_attribute{}, "Self upgrade/downgrade");

	{
		auto n = update->create_child("migration_available");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		n->set(ossia::net::description_attribute{}, "a rnbo version for existing data that may be migrated to this version. set it blank to indicate migration is complete.");

		auto m = mDB->migrationDataAvailable();
		if (m) {
			p->push_value(m.get());
		}

		p->add_callback([this, p](const ossia::value& v) {
			mDB->markDataMigrated();
			//assure we can only write empty
			if (v.get_type() != ossia::val_type::STRING || v.get<std::string>().size() != 0) {
				p->push_value_quiet("");
			}
		});
	}


#if RNBO_USE_DBUS
	mUpdateServiceProxy = std::make_shared<RnboUpdateServiceProxy>();
	if (mUpdateServiceProxy) {
		try {
			//setup dbus
			RunnerUpdateState state;
			runner_update::from(mUpdateServiceProxy->State(), state);
			std::string status = mUpdateServiceProxy->Status();

			{
				auto n = update->create_child("state");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Update state");
				p->push_value(runner_update::into(state));

				std::vector<ossia::value> accepted;
				for (auto v: runner_update::all()) {
					accepted.push_back(v);
				}
				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, accepted);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mUpdateServiceProxy->setStateCallback([p](RunnerUpdateState state) mutable { p->push_value(runner_update::into(state)); });
			}

			{
				auto n = update->create_child("status");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Latest update status");
				p->push_value(status);
				mUpdateServiceProxy->setStatusCallback([p](std::string status) mutable { p->push_value(status); });
			}

			{
				auto n = update->create_child("outdated");
				auto p = n->create_parameter(ossia::val_type::INT);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Number of outdated packages detected on the system");
				p->push_value(-1);
				mUpdateServiceProxy->setOutdatedPackagesCallback([p](uint32_t cnt) mutable { p->push_value(static_cast<int>(cnt)); });
			}
			supports_install = true;
		} catch (const std::exception& e) {
			cerr << "exception caught " << e.what() << endl;
			//reset shared ptrs as we use them later to decide if we should try to update or poll
			supports_install = false;
		} catch (...) {
			cerr << "unknown exception caught " << endl;
			supports_install = false;
		}
	}
#endif

	//let the outside know if this instance supports up/downgrade
	{
		auto n = update->create_child("supported");
		auto p = n->create_parameter(ossia::val_type::BOOL);
		n->set(ossia::net::description_attribute{}, "Does this runner support remote upgrade/downgrade");
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		p->push_value(supports_install);
	}

	registerCommands();
}

Controller::~Controller() {
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard, 0.0f);
	}
	mProtocol = nullptr;
	mProcessAudio.reset();
	mServer.reset();
}

std::shared_ptr<Instance> Controller::loadLibrary(const std::string& path, std::string cmdId, RNBO::Json conf, bool saveConfig, unsigned int instanceIndex, const fs::path& config_path) {
	//clear out our last instance presets, loadSet should already have it if there is one
	mInstanceLastPreset.clear();

	auto fname = fs::path(path).filename().string();
	try {
		//make sure that the version numbers match in the name of the library
		if (!boost::algorithm::ends_with(fname, rnbo_dylib_suffix)) {
			std::string errs("the requested library: " + fname + " doesn't match version suffix " + rnbo_dylib_suffix);
			cerr << errs << endl;
			//we should never really have an ID here because we would have just built it, but just in case.
			if (cmdId.size()) {
				reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::VersionMismatch), errs);
			}
			return nullptr;
		}

		//activate if we need to
		if (!tryActivateAudio(true)) {
			cerr << "audio is not active, cannot create instance(s)" << endl;
			if (cmdId.size()) {
				reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::AudioNotActive), "cannot activate audio");
			}
			reportActive();
			return nullptr;
		}
		//make sure that no other instances can be created while this is active
		std::lock_guard<std::mutex> iguard(mInstanceMutex);
		auto factory = PatcherFactory::CreateFactory(path);
		ossia::net::node_base * instNode = nullptr;
		std::string instIndex = std::to_string(instanceIndex);

		std::string name = "rnbo" + instIndex;
		if (conf.contains("name") && conf["name"].is_string()) {
			name = conf["name"].get<std::string>();
		}

		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			unloadInstance(guard, instanceIndex);
			instNode = mInstancesNode->create_child(instIndex);
		}
		auto builder = [instNode, this](std::function<void(ossia::net::node_base*)> f) {
			std::lock_guard<std::mutex> guard(mBuildMutex);
			f(instNode);
		};

		auto instance = std::make_shared<Instance>(mDB, factory, name, builder, conf, mProcessAudio, instanceIndex, std::bind(&Controller::dispatchOSC, this, std::placeholders::_1, std::placeholders::_2), std::bind(&Controller::registerOSCMapping, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			instance->registerPresetLoadedCallback([this, instanceIndex](const std::string& presetName, const std::string& setPresetName) {
					handleInstancePresetLoad(instanceIndex, setPresetName, presetName);
			});
			instance->activate();
			instance->processEvents();
			instance->markConfigChanged(false); //use a hammer to disable change reporting

			//queue a save whenenever the configuration changes
			instance->registerConfigChangeCallback([this] {
					mSetDirtyParam->push_value(true);
					queueSave();
			});
			mInstances.emplace_back(std::make_tuple(instance, path, config_path));
		}
		if (cmdId.size()) {
			reportCommandResult(cmdId, {
				{"code", static_cast<unsigned int>(CompileLoadStatus::Loaded)},
				{"message", "loaded"},
				{"progress", 100}
			});
		}
		if (saveConfig)
			queueSave();
		reportActive();
		mProcessAudio->updatePorts();
		return instance;
	} catch (const std::exception& e) {
		std::cerr << "failed to load library: " << fname << " exception: " << e.what() << std::endl;
	} catch (...) {
		std::cerr << "failed to load library: " << fname << std::endl;
	}
	return nullptr;
}

void Controller::loadInitialSet() {
	std::string lastSetName = config::get<std::string>(config::key::SetLastName).value_or(UNTITLED_SET_NAME);
	loadSet(mDB->setNameInitial().value_or(lastSetName));
}

//actually just queue it
void Controller::loadSet(std::string name) {
	std::lock_guard<std::mutex> guard(mSetLoadPendingMutex);
	if (!tryActivateAudio(true)) {
		std::cerr << "cannot activate audio, cannot load set" << std::endl;
		return;
	}
	mSetLoadPending = name;

	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard, mInstFadeOutMs);
	}
}

void Controller::doLoadSet(std::string setname) {
	try {
		std::unordered_map<unsigned int, RNBO::UniquePresetPtr> presets;
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			//get the last preset (saved before clearing);
			std::swap(presets, mInstanceLastPreset);
		}

		auto setInfo = mDB->setGet(setname);
		if (!setInfo)
			return;

		//load instances
		std::vector<std::shared_ptr<Instance>> instances;
		for (const auto& entry: setInfo->instances) {
			std::string name = entry.patcher_name;
			unsigned int index = entry.instance_index;

			fs::path libPath;
			fs::path confPath;
			fs::path patcherPath; //ignored
			std::string created_at; //ignored
			if (!mDB->patcherGetLatest(name, libPath, confPath, patcherPath, created_at)) {
				cerr << "failed to find patcher with name '" << name << "' while loading set, skipping" << std::endl;
				continue;
			}

			libPath = fs::absolute(mCompileCache / libPath);
			confPath = fs::absolute(mSourceCache / confPath);

			RNBO::Json config;
			if (fs::exists(confPath)) {
				std::ifstream i(confPath.string());
				i >> config;
				i.close();
			}
			config["name"] = name;

			if (entry.config.size()) {
				auto instConfig = RNBO::Json::parse(entry.config);
				//override datarefs
				if (instConfig.contains("datarefs"))
					config["datarefs"] = instConfig["datarefs"];
				//load the last preset as the initial one
				if (instConfig["preset_last"].is_string()) {
					config["preset_initial"] = instConfig["preset_last"];
				}
				//override meta
				if (instConfig["metaoverride"].is_object()) {
					config["metaoverride"] = instConfig["metaoverride"];
				}
				if (instConfig.contains("setpreset")) {
					config["setpreset"] = instConfig["setpreset"];
				}
				if (instConfig["namealias"].is_string()) {
					config["namealias"] = instConfig["namealias"];
				}
			}

			//load library but don't save config
			auto inst = loadLibrary(libPath.string(), std::string(), config, false, index, confPath);
			if (!inst) {
				cerr << "failed to load library " << libPath << endl;
				continue;
			}
			instances.push_back(inst);
		}

		/*
		//XXX check in on this
		//load last preset if we have it
		if (presets.size()) {
			std::lock_guard<std::mutex> guard(mBuildMutex);
			for (auto& preset: presets) {
				for (auto i: mInstances) {
					auto inst = std::get<0>(i);
					if (inst->index() == preset.first) {
						inst->loadPreset(std::move(preset.second));
					}
				}
			}
		}
		*/

		mProcessAudio->updatePorts();
		mProcessAudio->connect(setInfo->connections, mFirstSetLoad); //only do control connections when loading first set
		mFirstSetLoad = false;

		const bool loadInitial = setname.size() && setname != UNTITLED_SET_NAME;

		//load presets and start instances
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			std::lock_guard<std::mutex> iguard(mInstanceMutex);

			mPendingSetPresetName = "initial";
			mInstancesPendingPresetLoad.clear();

			for (auto inst: instances) {
				if (loadInitial) {
					if (inst->loadPreset(mPendingSetPresetName, setname)) {
						mInstancesPendingPresetLoad.insert(inst->index());
					}
				}
				inst->start(mInstFadeInMs);
			}
		}

		if (mSetMetaParam) {
			mSetMetaParam->push_value_quiet(setInfo->meta);
		}

		mSetDirtyParam->push_value(false);
		mSetCurrentNameParam->push_value(setname);
		config::set(setname, config::key::SetLastName);
		updateSetPresetNames();
	} catch (const std::exception& e) {
		cerr << "exception " << e.what() << " trying to load last setup" << endl;
	} catch (...) {
		cerr << "unknown exception trying to load last setup" << endl;
	}
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		updateSetViews(setname);
	}
}

#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
bool Controller::loadBuiltIn() {
	mSave = false;
	try {
		fs::path confFilePath(RNBO_OSCQUERY_BUILTIN_PATCHER_CONF_PATH);
		RNBO::Json config;
		std::ifstream i(confFilePath.string());
		i >> config;
		if (!tryActivateAudio(true)) {
			reportActive();
			cerr << "audio is not active, cannot create builtin instance" << endl;
			return false;
		}

		{
			//make sure that no other instances can be created while this is active
			std::lock_guard<std::mutex> iguard(mInstanceMutex);
			auto factory = PatcherFactory::CreateBuiltInFactory();
			ossia::net::node_base * instNode;
			std::string instIndex;
			{
				std::lock_guard<std::mutex> guard(mBuildMutex);
				clearInstances(guard, 0.0f);
				instIndex = std::to_string(mInstances.size());
				instNode = mInstancesNode->create_child(instIndex);
			}
			auto builder = [instNode, this](std::function<void(ossia::net::node_base *)> f) {
				std::lock_guard<std::mutex> guard(mBuildMutex);
				f(instNode);
			};

			std::string name("builtin");
			auto instance = std::make_shared<Instance>(mDB, factory, name, builder, config, mProcessAudio, 0);
			{
				std::lock_guard<std::mutex> guard(mBuildMutex);
				instance->registerConfigChangeCallback([this] { queueSave(); });
				instance->activate();
				mInstances.emplace_back(std::make_tuple(instance, fs::path(), fs::path()));
			}

			instance->connect();
			instance->start();

			fs::path libFile;

			mDB->patcherStore(name, libFile, confFilePath, "", RNBO::version, 0, 0, 0, 0);
		}
		reportActive();
		return true;
	} catch (const std::exception& e) {
		cerr << "exception " << e.what() << " trying to load built in" << endl;
	} catch (...) {
		cerr << "unknown exception trying to load built in" << endl;
	}
	return false;
}
#endif

SetInfo Controller::setInfo() {
	SetInfo info;

	if (mSetMetaParam) {
		auto v = mSetMetaParam->value();
		if (v.get_type() == ossia::val_type::STRING) {
			info.meta = v.get<std::string>();
		}
	}

	std::lock_guard<std::mutex> iguard(mInstanceMutex);

	info.connections = mProcessAudio->connections();

	for (auto& i: mInstances) {
		auto& inst = std::get<0>(i);

		info.instances.push_back(SetInstanceInfo(inst->name(), inst->index(), inst->currentConfig().dump()));

		//client is named patchName-index
		std::string clientName = inst->name() + "-" + std::to_string(inst->index());

		//add index to connections
		for (auto& c: info.connections) {
			if (c.source_name == clientName) {
				c.source_instance_index = static_cast<int>(inst->index());
			} else if (c.sink_name == clientName) {
				c.sink_instance_index = static_cast<int>(inst->index());
			}
		}
	}

	return info;
}

void Controller::patcherStore(
		const std::string& name,
		const boost::filesystem::path& libFile,
		const boost::filesystem::path& configFilePath,
		const boost::filesystem::path& rnboPatchPath,
		const std::string& maxRNBOVersion,
		const RNBO::Json& conf,
		bool migrate_presets
		) {
	int audio_inputs = 0;
	int audio_outputs = 0;
	int midi_inputs = 0;
	int midi_outputs = 0;

	if (conf.contains("numInputChannels")) {
		audio_inputs = conf["numInputChannels"].get<int>();
	}
	if (conf.contains("numOutputChannels")) {
		audio_outputs = conf["numOutputChannels"].get<int>();
	}
	if (conf.contains("numMidiInputPorts")) {
		midi_inputs = conf["numMidiInputPorts"].get<int>();
	}
	if (conf.contains("numMidiOutputPorts")) {
		midi_outputs = conf["numMidiOutputPorts"].get<int>();
	}

	mDB->patcherStore(name, libFile, configFilePath, rnboPatchPath, maxRNBOVersion, migrate_presets, audio_inputs, audio_outputs, midi_inputs, midi_outputs);

	//save presets
	if (conf.contains("presets")) {
		for (auto& kv: conf["presets"].items()) {
			assert(kv.value().is_object());
			mDB->presetSave(name, kv.key(), kv.value().dump());
		}
	}

	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		updatePatchersInfo(name);
	}
}

void Controller::queueSave() {
	std::lock_guard<std::mutex> guard(mSaveMutex);
	mSave = true;
}

void Controller::updatePatchersInfo(std::string addedOrUpdated) {
	mDB->patchers([this, &addedOrUpdated](const std::string& name, int audio_inputs, int audio_outputs, int midi_inputs, int midi_outputs, const std::string& created_at) {
			if (addedOrUpdated.length() && name != addedOrUpdated) {
				return;
			}

			auto r = find_or_create_child(mPatchersNode, name);

			{
				auto n = find_or_create_child(r, "io");
				auto p = n->get_parameter();
				if (!p) {
					p = n->create_parameter(ossia::val_type::LIST);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					n->set(ossia::net::description_attribute{}, "input and output counts: audio ins, audio outs, midi ins, midi outs");
				}

				std::vector<ossia::value> l;
				l.push_back(audio_inputs);
				l.push_back(audio_outputs);
				l.push_back(midi_inputs);
				l.push_back(midi_outputs);

				p->push_value(l);
			}

			{
				auto n = find_or_create_child(r, "created_at");
				auto p = n->get_parameter();
				if (!p) {
					p = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
				p->push_value(created_at);
			}

			{
				auto n = find_or_create_child(r, "destroy");
				auto p = n->get_parameter();
				if (!p) {
					p = n->create_parameter(ossia::val_type::IMPULSE);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "delete this patcher");
					p->add_callback([this, name](const ossia::value&) {
							RNBO::Json cmd = {
								{"method", "patcher_destroy"},
								{"id", "internal"},
								{"params",
									{
										{"name", name}
									}
								}
							};
							mCommandQueue.push(cmd.dump());
					});
				}
			}

			{
				auto n = find_or_create_child(r, "rename");
				auto p = n->get_parameter();
				if (!p) {
					p = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
					n->set(ossia::net::description_attribute{}, "rename this patcher");
					p->add_callback([this, name](const ossia::value& val) {
							if (val.get_type() == ossia::val_type::STRING) {
								std::string newName = val.get<std::string>();
								RNBO::Json cmd = {
									{"method", "patcher_rename"},
									{"id", "internal"},
									{"params",
										{
											{"name", name},
											{"newName", newName}
										}
									}
								};
								mCommandQueue.push(cmd.dump());
							}

					});
				}
			}
	});
}

void Controller::destroyPatcher(const std::string& name) {
	mDB->patcherDestroy(name, [this](fs::path& so_name, fs::path& config_name) {
			boost::system::error_code ec;
			fs::remove(fs::absolute(mCompileCache / so_name), ec);
			fs::remove(fs::absolute(mSourceCache / config_name), ec);
	});
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		mPatchersNode->remove_child(name);
	}
}

void Controller::updateSetNames() {
	std::lock_guard<std::mutex> guard(mSetNamesMutex);
	mSetNames.clear();
	std::string initialName;
	mDB->sets([this, &initialName](const std::string& name, const std::string& /*created*/, bool initial) {
			if (name != UNTITLED_SET_NAME) {
				mSetNames.push_back(name);
				if (initial) {
					initialName = name;
				}
			}
	});

	updateSetInitialName(initialName);
	mSetNamesUpdated = true;
}

void Controller::updateSetViews(const std::string& setname) {
	//we have the build mutex
	mSetViewsListNode->clear_children();
	std::vector<ossia::value> order;

	if (setname.size()) {
		auto indexes = mDB->setViewIndexes(setname);
		for (auto i: indexes) {
			addSetView(setname, i);
			order.push_back(i);
		}
	}
	mSetViewsOrderParam->push_value_quiet(order);
}

void Controller::addSetView(std::string setname, int index) {
	auto data = mDB->setViewGet(setname, index);
	if (!data) {
		return;
	}

	std::string name = std::get<0>(data.get());
	std::vector<std::string> params = std::get<1>(data.get());
	//int sortOrder = std::get<2>(data.get());

	auto view = mSetViewsListNode->create_child(std::to_string(index));
	{
		auto n = view->create_child("name");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		n->set(ossia::net::description_attribute{}, "Optional name for this set view");
		if (name.size()) {
			p->push_value(name);
		}
		p->add_callback([this, setname, index](const ossia::value& val) {
			if (val.get_type() == ossia::val_type::STRING) {
				std::string name = val.get<std::string>();
				mDB->setViewUpdateName(setname, index, name);
			}
		});
	}
	{
		auto n = view->create_child("params");
		auto p = n->create_parameter(ossia::val_type::LIST);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		n->set(ossia::net::description_attribute{}, "list of parameters in format instance_index:parameter_id");
		std::vector<ossia::value> l;
		for (auto param: params) {
			l.push_back(param);
		}
		p->push_value(l);
		p->add_callback([this, setname, index](const ossia::value& val) {
			std::vector<ViewParam> params;
			if (val.get_type() == ossia::val_type::LIST) {
				auto l = val.get<std::vector<ossia::value>>();
				for (auto item: l) {
					if (item.get_type() == ossia::val_type::STRING) {
						params.push_back(ViewParam::fromString(item.get<std::string>()));
					}
				}
			}
			mDB->setViewUpdateParams(setname, index, params);
		});
	}
}

void Controller::removeSetView(int index) {
	mSetViewsListNode->remove_child(std::to_string(index));
}

void Controller::reportSetViewOrder(const std::string& setname) {
	std::vector<ossia::value> order;
	if (setname.size()) {
		auto indexes = mDB->setViewIndexes(setname);
		for (auto i: indexes) {
			order.push_back(i);
		}
	}
	mSetViewsOrderParam->push_value_quiet(order);
}

void Controller::updateSetInitialName(std::string name) {
	//if we don't have the param or the param is a string and matches, do nothing
	if (!mSetInitialNameParam || (mSetInitialNameParam->get_value_type() == ossia::val_type::STRING && mSetInitialNameParam->value().get<std::string>() == name)) {
		return;
	}
	//update param
	mSetInitialNameParam->push_value(name);
}

void Controller::updateSetPresetNames(std::string toadd) {
	std::lock_guard<std::mutex> guard(mSetPresetNamesMutex);
	mSetPresetNames.clear();

	std::string setname = getCurrentSetName();
	if (setname.size()) {
		std::vector<std::string> presetNames = mDB->setPresets(setname);
		for (auto name: presetNames) {
			if (name != toadd) {
				mSetPresetNames.push_back(name);
			}
		}
		if (toadd.size()) {
			mSetPresetNames.push_back(toadd);
		}
	}
	mSetPresetNamesUpdated = true;
}

void Controller::saveSetPreset(const std::string& setName, std::string presetName) {
	//ditch the old one
	mDB->setPresetDestroy(setName, presetName);
	//save the new ones
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	for (auto& i: mInstances) {
		std::get<0>(i)->savePreset(presetName, setName);
	}
}

void Controller::loadSetPreset(const std::string& setName, std::string presetName) {
	//load the preset and if it exists for an instance make note so we can watch callback and report once they're all loaded
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	mPendingSetPresetName = presetName;
	mInstancesPendingPresetLoad.clear();
	for (auto& i: mInstances) {
		auto inst = std::get<0>(i);
		if (inst->loadPreset(presetName, setName)) {
			mInstancesPendingPresetLoad.insert(inst->index());
		}
	}
}

void Controller::handleInstancePresetLoad(unsigned int index, const std::string& setPresetName, const std::string& /*presetName*/) {
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	if (setPresetName != mPendingSetPresetName || mInstancesPendingPresetLoad.count(index) == 0) {
		return;
	}

	//we got the final instance preset loaded, report it
	mInstancesPendingPresetLoad.erase(index);
	if (mInstancesPendingPresetLoad.size() == 0) {
		mSetPresetLoadedParam->push_value(setPresetName);
	}
}

unsigned int Controller::nextInstanceIndex() {
	unsigned int index = 0;
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	for (auto& i: mInstances) {
		index = std::max(std::get<0>(i)->index() + 1, index);
	}
	return index;
}

void Controller::installPackage(const boost::filesystem::path& contentdir) {
	//get info
	RNBO::Json info = readJson(contentdir / "info.json");
	if (!info.contains("schema_version") || info["schema_version"] != 1 || !info.contains("rnbo_version")) {
		throw std::runtime_error("don't know how to read info.json");
	}

	std::string package_rnbo_version = info["rnbo_version"].get<std::string>();

	if (package_rnbo_version != rnbo_version) {
		std::string msg = "package rnbo_version: " + package_rnbo_version + " does not match current library rnbo_version: " + rnbo_version;
		throw std::runtime_error(msg);
	}

	auto do_copy = [&contentdir](const fs::path& src, const fs::path& dst) {
		if (!fs::exists(dst.parent_path())) {
			fs::create_directories(dst.parent_path());
		}

		if (!fs::copy_file(contentdir / src, dst, fs::copy_option::overwrite_if_exists)) {
			std::string msg = "failed to copy file name: \"" + src.filename().string();
			throw std::runtime_error(msg);
		}
	};

	if (info.contains("datafiles")) {
		auto dstdir = config::get<fs::path>(config::key::DataFileDir).get();
		auto datafiles = info["datafiles"];
		for (const auto& entry: datafiles) {
			fs::path dst = dstdir / entry["name"].get<std::string>();
			if (fs::exists(dst)) {
				continue; //skip overwritting data files
			}
			do_copy(fs::path(entry["location"].get<std::string>()), dst);
		}
	}

	if (info.contains("patchers")) {
		auto entries = info["patchers"];
		const auto target = targetid();
		for (const auto& entry: entries) {
			std::string libFile;
			std::string configFileName;
			std::string patcherFileName;
			RNBO::Json config;

			std::string name = entry["name"].get<std::string>();
			if (!entry.contains("binaries") || !entry["binaries"].contains(target)) {
				std::string msg = "no binary for patcher " + name + " in package";
				throw std::runtime_error(msg);
			}

			//TODO check for collisions?
			{
				fs::path src = fs::path(entry["binaries"][target].get<std::string>());
				//don't overwrite an existing lib, we assume they're the same
				fs::path dst = mCompileCache / src.filename();
				if (!fs::exists(dst)) {
					do_copy(src, dst);
				}
				libFile = src.filename().string();
			}

			if (entry.contains("config")) {
				fs::path src = fs::path(entry["config"].get<std::string>());
				do_copy(src, mSourceCache / src.filename());
				configFileName = src.filename().string();
				config = readJson(mSourceCache / src.filename());
			}

			if (entry.contains("patcher")) {
				fs::path src = fs::path(entry["patcher"].get<std::string>());
				do_copy(src, mSourceCache / src.filename());
				patcherFileName = src.filename().string();
			}

			patcherStore(name, libFile, configFileName, patcherFileName, package_rnbo_version, config, false);

			if (entry.contains("presets")) {
				RNBO::Json presets = readJson(contentdir / fs::path(entry["presets"].get<std::string>()));
				for (auto& kv: presets.items()) {
					assert(kv.value().is_object());
					mDB->presetSave(name, kv.key(), kv.value().dump());
				}
			}
		}
	}

	if (info.contains("sets")) {
		auto entries = info["sets"];
		for (const auto& entry: entries) {
			std::string name = entry["name"].get<std::string>();
			RNBO::Json setData = readJson(contentdir / fs::path(entry["location"].get<std::string>()));
			SetInfo info = SetInfo::fromJson(setData);
			mDB->setSave(name, info);
			storeSetContent(setData, mDB, name);
		}
		updateSetNames();
	}
}

bool Controller::processEvents() {
	auto now = steady_clock::now();

	try {
		{
			std::lock_guard<std::mutex> guard(mOssiaContextMutex);
			ossia::net::poll_network_context(*mOssiaContext);
		}

		if (mProcessNext < now) {
			mProcessNext = now + process_poll_period;
		} else {
			return true;
		}

		processCommands();

		std::string loadedset = getCurrentSetName();
		auto handleConnectionChange = [this, &loadedset](ConnectionChange change) {
			//figure out if connections are inconsistent with those in the DB
			if (change.issource) {
				if (!mDB->setMatchesConnections(loadedset, change.port, change.connections)) {
					mSetDirtyParam->push_value(true);
				}
			}
		};

		bool anyInstances = false;
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			if (mProcessAudio)
				mProcessAudio->processEvents(handleConnectionChange);
			for (auto& i: mInstances) {
				auto& inst = std::get<0>(i);
				inst->processEvents();

				//manage broadcasting preset changes across instances
				if (inst->presetsDirty()) {
					for (auto& j: mInstances) {
						auto& jinst = std::get<0>(j);
						if (jinst != inst && jinst->name() == inst->name()) {
							jinst->presetsUpdateMarkClean();
						}
					}
				}
			}
			//manage stopping instances
			for (auto it = mStoppingInstances.begin(); it != mStoppingInstances.end();) {
				auto p = *it;
				p->processEvents();
				if (p->audioState() == AudioState::Stopped) {
					it = mStoppingInstances.erase(it);
				} else {
					it++;
				}
			}
			anyInstances = mInstances.size() > 0 || mStoppingInstances.size() > 0;
		}

		//if we have no instances, look to see if we should load a set
		if (!anyInstances) {
			boost::optional<std::string> pending;
			{
				std::lock_guard<std::mutex> guard(mSetLoadPendingMutex);
				mSetLoadPending.swap(pending);
			}
			if (pending) {
				doLoadSet(pending.get());
			}
		}

		if (mDiskSpacePollNext <= now) {
			//XXX shouldn't need this mutex but removing listeners is causing this to throw an exception so
			//using a hammer to make sure that doesn't happen
			std::lock_guard<std::mutex> guard(mOssiaContextMutex);
			updateDiskStats();
		}

		if (mDatafilePollNext <= now) {
			//XXX shouldn't need this mutex but removing listeners is causing this to throw an exception so
			//using a hammer to make sure that doesn't happen
			std::lock_guard<std::mutex> guard(mOssiaContextMutex);
			updateDatafileStats();
		}

		bool save = false;
		{
			//see if we got the save flag set, debounce
			std::lock_guard<std::mutex> guard(mSaveMutex);
			if (mSave) {
				mSave = false;
				mSaveNext = steady_clock::now() + save_debounce_timeout;
			} else if (mSaveNext && mSaveNext.get() < now) {
				save = true;
				mSaveNext.reset();
			}
		}
		if (save) {
			auto info = setInfo();
			mDB->setSave(UNTITLED_SET_NAME, info);
		}

		//sets
		{
			std::lock_guard<std::mutex> guard(mSetNamesMutex);
			if (mSetNamesUpdated) {
				mSetNamesUpdated = false;

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, mSetNames);
				mSetLoadNode->set(ossia::net::domain_attribute{}, dom);
				mSetLoadNode->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
			}
		}
		{
			std::lock_guard<std::mutex> guard(mSetPresetNamesMutex);
			if (mSetPresetNamesUpdated) {
				mSetPresetNamesUpdated = false;

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, mSetPresetNames);
				mSetPresetLoadNode->set(ossia::net::domain_attribute{}, dom);
				mSetPresetLoadNode->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "exception in Controller::process thread " << e.what() << std::endl;
	}

	//TODO allow for quitting?
	return true;
}

void Controller::handleActive(bool active) {
	//clear out instances if we're deactivating
	if (!active) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		//save preset to reload on activation
		//might cause a glitch?
		mInstanceLastPreset.clear();
		for (auto i: mInstances) {
			auto inst = std::get<0>(i);
			mInstanceLastPreset.insert({inst->index(), inst->getPresetSync()});
		}

		clearInstances(guard, 0.0f);
		mStoppingInstances.clear();
	}

	bool wasActive = mProcessAudio->isActive();
	if (mProcessAudio->setActive(active) != active) {
		cerr << "couldn't set active" << endl;
		//XXX defer to setting not active
	} else if (!wasActive && mProcessAudio->isActive()) {
		//if the process audio became active and there are no instances loaded, try to load_last
		std::lock_guard<std::mutex> guard(mBuildMutex);
		if (!mInstances.size()) {
			//load last if we're activating from inactive
			mCommandQueue.push("load_last");
		}
	}
}

bool Controller::tryActivateAudio(bool startServer) {
	if (!mProcessAudio->isActive())
		mProcessAudio->setActive(true, startServer);
	return mProcessAudio->isActive();
}

void Controller::dispatchOSC(const std::string& addr, const ossia::value& v) {
	//based on code example from jcelerier
	auto nodes = ossia::net::find_nodes(mServer->get_root_node(), addr);
	bool message_sent = false;
	for (auto& n : nodes) {
		if (auto param = n->get_parameter()) {
			param->push_value(v);
			message_sent = true;
		}
	}

	//send out but also callback into any local osc listeners
	if (!message_sent) {
		mProtocol->push_raw({addr, v});
		onUnhandledOSC(addr, v);
	}
}


void Controller::onUnhandledOSC(ossia::string_view addrview, const ossia::value& val) {
	std::lock_guard<std::recursive_mutex> guard(mOSCMapMutex);

	//c++17 can't use string_view for lookup: https://www.cppstories.com/2021/heterogeneous-access-cpp20/
	std::string addr(addrview.begin(), addrview.end());
	auto it = mOSCToParam.find(addr);
	if (it == mOSCToParam.end()) {
		return;
	}

	auto& root = mServer->get_root_node();
	for (auto localaddr: it->second) {
		auto node = ossia::net::find_node(root, localaddr);
		if (node != nullptr) {
			auto param = node->get_parameter();
			if (param) {
				param->push_value(val);
			}
		}
	}
}


void Controller::registerOSCMapping(bool doregister, const std::string& oscaddr, const std::string& localaddr) {
	std::lock_guard<std::recursive_mutex> guard(mOSCMapMutex);
	auto it = mOSCToParam.find(oscaddr);
	if (doregister) {
		if (it != mOSCToParam.end()) {
			it->second.insert(localaddr);
		} else {
			mOSCToParam.insert({oscaddr, {localaddr}});
		}
	} else {
		if (it != mOSCToParam.end()) {
			it->second.erase(localaddr);
			if (it->second.size() == 0) {
				mOSCToParam.erase(it);
			}
		}
	}
}

void Controller::reportActive() {
	mAudioActive->set_value(mProcessAudio->isActive());
}

void Controller::clearInstances(std::lock_guard<std::mutex>&, float fadeTime) {
	for (auto it = mInstances.begin(); it < mInstances.end(); ) {
		auto inst = std::get<0>(*it);
		auto index = inst->index();
		inst->stop(fadeTime);
		if (fadeTime > 0.0f) {
			mStoppingInstances.push_back(inst);
		}
		it = mInstances.erase(it);
		if (!mInstancesNode->remove_child(std::to_string(index))) {
			std::cerr << "failed to remove instance node with index " << index << std::endl;
		}
	}
}

void Controller::unloadInstance(std::lock_guard<std::mutex>&, unsigned int index) {
	for (auto it = mInstances.begin(); it < mInstances.end(); it++) {
		auto inst = std::get<0>(*it);
		if (inst->index() == index) {
			mInstances.erase(it);
			inst->stop(mInstFadeOutMs);
			mStoppingInstances.push_back(inst);
			if (!mInstancesNode->remove_child(std::to_string(index))) {
				std::cerr << "failed to remove instance node with index " << index << std::endl;
			}
			return;
		}
	}
}

void Controller::registerCommands() {
	mCommandHandlers.insert({
			"activate_audio",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				handleActive(params["active"].get<bool>());
			}
	});

	mCommandHandlers.insert({
			"restart_audio",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				handleActive(false);
				handleActive(true);
			}
	});

	mCommandHandlers.insert({
			"patcher_destroy",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				destroyPatcher(name);
			}
	});
	mCommandHandlers.insert({
			"patcher_rename",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string newName = params["newName"].get<std::string>();
				mDB->patcherRename(name, newName);
				{
					std::lock_guard<std::mutex> guard(mBuildMutex);
					mPatchersNode->remove_child(name);
					updatePatchersInfo(newName);
				}
			}
	});
	mCommandHandlers.insert({
			"instance_load",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				int index = params["index"].get<int>();
				std::string name = params["patcher_name"].get<std::string>();
				std::string instance_name;
				bool connect = true;
				if (params.contains("instance_name"))
					instance_name = params["instance_name"].get<std::string>();

				//automatically set the index if it is less than zero
				//only auto connect if the index is -1
				if (index < 0) {
					connect = index == -1;
					index = nextInstanceIndex();
				}

				fs::path libPath;
				fs::path confPath;
				fs::path patcherName; //ignored
				std::string created_at; //ignored
				RNBO::Json config;
				if (mDB->patcherGetLatest(name, libPath, confPath, patcherName, created_at)) {
					libPath = fs::absolute(mCompileCache / libPath);
					confPath = fs::absolute(mSourceCache / confPath);

					if (fs::exists(libPath)) {
						if (fs::exists(confPath)) {
							std::ifstream i(confPath.string());
							i >> config;
							i.close();
						}

						//add client name to config..
						if (instance_name.size()) {
							RNBO::Json jack;
							if (config.contains("jack"))
								jack = config["jack"];
							jack["client_name"] = instance_name;
							config["jack"] = jack;
						}
						config["name"] = name;

						auto inst = loadLibrary(libPath.string(), id, config, true, static_cast<unsigned int>(index), confPath);

						if (inst) {
							if (connect) {
								inst->connect();
							}
							inst->start(mInstFadeInMs);
						}
					}
					reportCommandResult(id, {
						{"code", 0},
						{"message", "loaded"},
						{"progress", 100}
					});
					mSetDirtyParam->push_value(true);
				} else {
					reportCommandError(id, 1, "failed");
				}
				queueSave();
			}
	});

	mCommandHandlers.insert({
			"instance_unload",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				int index = params["index"].get<int>();
				{
					std::lock_guard<std::mutex> guard(mBuildMutex);
					if (index < 0) {
						clearInstances(guard, mInstFadeOutMs);
					} else {
						unloadInstance(guard, index);
					}
				}
				if (index < 0) {
					//clear out all set views and presets from UNTITLED_SET_NAME
					const std::string loaded(UNTITLED_SET_NAME);
					mDB->setPresetDestroyAll(loaded);
					mDB->setViewDestroy(loaded, -1);

					const std::string empty;
					mSetCurrentNameParam->push_value(UNTITLED_SET_NAME);
					mSetPresetLoadedParam->push_value(empty);

					updateSetPresetNames();
					{

						std::lock_guard<std::mutex> guard(mBuildMutex);
						updateSetViews(loaded);
					}
				}
				mProcessAudio->updatePorts();
				queueSave();
				mSetDirtyParam->push_value(true);
				reportCommandResult(id, {
					{"code", 0},
					{"message", "unloaded"},
					{"progress", 100}
				});
			}
	});
	mCommandHandlers.insert({
			"instance_set_save",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string meta = params["meta"].get<std::string>();
				//TODO what about meta
				auto info = setInfo();
				if (info.instances.size() && name != UNTITLED_SET_NAME) {
					std::string loaded = getCurrentSetName();

					mDB->setSave(name, info);
					mSetDirtyParam->push_value(false);
					//TODO copy set presets??

					const std::string presetName = "initial";
					saveSetPreset(name, presetName);
					reportCommandResult(id, {
						{"code", 0},
						{"message", "saved"},
						{"progress", 100}
					});
					mSetCurrentNameParam->push_value(name);
					updateSetNames();
					updateSetPresetNames(presetName);

					//set views
					{
						//copy views
						if (loaded.size() > 0 && loaded != name) {
							mDB->setViewsCopy(loaded, name);
						}
						std::lock_guard<std::mutex> guard(mBuildMutex);
						updateSetViews(name);

						if (loaded == UNTITLED_SET_NAME) {
							mDB->setDestroy(loaded);
						}
					}
					mSetPresetLoadedParam->push_value(presetName);
					config::set(name, config::key::SetLastName);
				} else {
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_load",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				loadSet(name);
				reportCommandResult(id, {
					{"code", 0},
					{"message", "loaded"},
					{"progress", 100}
				});
			}
	});

	mCommandHandlers.insert({
			"instance_set_initial",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				//if the name wasn't correctly set, clear out name
				if (!mDB->setInitial(name)) {
					reportCommandError(id, 1, "failed");
					updateSetInitialName("");
				} else {
					reportCommandResult(id, {
						{"code", 0},
						{"message", "initial"},
						{"progress", 100}
					});
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_delete",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				mDB->setDestroy(name);
				reportCommandResult(id, {
					{"code", 0},
					{"message", "deleted"},
					{"progress", 100}
				});
				updateSetNames();

				if (getCurrentSetName() == name) {
					std::string empty;
					config::set(empty, config::key::SetLastName);
					mSetCurrentNameParam->push_value(UNTITLED_SET_NAME);
					mSetDirtyParam->push_value(false);
					{
						std::lock_guard<std::mutex> guard(mBuildMutex);
						mSetViewsListNode->clear_children();
					}
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_rename",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string newName = params["newName"].get<std::string>();
				if (mDB->setRename(name, newName)) {
					reportCommandResult(id, {
						{"code", 0},
						{"message", "renamed"},
						{"progress", 100}
					});
					//if we renamed the current set, indicate that
					if (getCurrentSetName() == name) {
						mSetCurrentNameParam->push_value(newName);
						config::set(newName, config::key::SetLastName);
					}
					updateSetNames();
				} else {
					reportCommandError(id, 1, "failed");
				}
			}
	});

	//set presets

	mCommandHandlers.insert({
			"instance_set_preset_save",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					saveSetPreset(setname, name);
					reportCommandResult(id, {
						{"code", 0},
						{"message", "saved"},
						{"progress", 100}
					});

					//saving is async so we force "name" into the list
					updateSetPresetNames(name);
					mSetPresetLoadedParam->push_value(name);
				} else {
					std::cerr << "no current set name, cannot save preset" << std::endl;
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_preset_load",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					loadSetPreset(setname, name);
					reportCommandResult(id, {
						{"code", 0},
						{"message", "loaded"},
						{"progress", 100}
					});
				} else {
					std::cerr << "no current set name, cannot load preset" << std::endl;
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_preset_delete",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					mDB->setPresetDestroy(setname, name);
					reportCommandResult(id, {
						{"code", 0},
						{"message", "deleted"},
						{"progress", 100}
					});
					if (name == getCurrentSetPresetName()) {
						mSetPresetLoadedParam->push_value("");
					}
					updateSetPresetNames();
				} else {
					std::cerr << "no current set name, cannot destroy preset" << std::endl;
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_preset_rename",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string newName = params["newName"].get<std::string>();
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					mDB->setPresetRename(setname, name, newName);
					if (name == getCurrentSetPresetName()) {
						mSetPresetLoadedParam->push_value(newName);
					}

					reportCommandResult(id, {
						{"code", 0},
						{"message", "deleted"},
						{"progress", 100}
					});
					updateSetPresetNames();
				} else {
					std::cerr << "no current set name, cannot rename associated preset" << std::endl;
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"instance_set_view_create",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					std::string viewname = params["name"];
					std::vector<ViewParam> viewParams;

	        try {
	          for (auto p: params["params"]) {
	              viewParams.push_back(ViewParam::fromString(p.get<std::string>()));
	          }
	        } catch (...) {
	          std::cerr << "error parsing view params" << std::endl;
	        }

					int index = mDB->setViewCreate(setname, viewname, viewParams);
					{
						std::lock_guard<std::mutex> guard(mBuildMutex);
						addSetView(setname, index);
					}

					reportCommandResult(id, {
						{"code", 0},
						{"message", "created"},
						{"progress", 100}
					});
				} else {
					std::cerr << "no current set name, cannot create associated view" << std::endl;
					reportCommandError(id, 1, "failed");
				}
				reportSetViewOrder(setname);
			}
	});

	mCommandHandlers.insert({
			"instance_set_view_destroy",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					int index = params["index"].get<int>();
					mDB->setViewDestroy(setname, index);

					{
						std::lock_guard<std::mutex> guard(mBuildMutex);
						if (index < 0) {
							mSetViewsListNode->clear_children();
						} else {
							removeSetView(index);
						}
					}

					reportCommandResult(id, {
						{"code", 0},
						{"message", "destroyed"},
						{"progress", 100}
					});
				} else {
					std::cerr << "no current set name, cannot destroy associated view" << std::endl;
					reportCommandError(id, 1, "failed");
				}
				reportSetViewOrder(setname);
			}
	});

	mCommandHandlers.insert({
			"instance_set_view_order",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string setname = getCurrentSetName();
				ensureSet(setname);
				if (setname.size()) {
					std::vector<int> indexes;
					const RNBO::Json& order = params["order"];
					for (auto item: order) {
						indexes.push_back(item.get<int>());
					}

					if (mDB->setViewsUpdateSortOrder(setname, indexes)) {
						std::vector<ossia::value> values;
						for (auto index: indexes) {
							values.push_back(index);
						}
						mSetViewsOrderParam->push_value_quiet(values);
					}

					reportCommandResult(id, {
						{"code", 0},
						{"message", "sorted"},
						{"progress", 100}
					});
				} else {
					std::cerr << "no current set name, cannot sort associated views" << std::endl;
					reportCommandError(id, 1, "failed");
				}
			}
	});

	mCommandHandlers.insert({
			"patcherstore",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string name = params["name"].get<std::string>();
				std::string libFile = params["lib"].get<std::string>();
				std::string configFileName = params["config"].get<std::string>();
				std::string rnboPatchName = params["patcher"].get<std::string>();
				bool migratePresets = params.contains("migrate_presets") && params["migrate_presets"].is_boolean() && params["migrate_presets"].get<bool>();

				RNBO::Json config;
				fs::path confFilePath = fs::absolute(mSourceCache / configFileName);
				std::ifstream i(confFilePath.string());
				i >> config;
				i.close();

				std::string maxRNBOVersion = "unknown";
				if (params.contains("rnbo_version")) {
					maxRNBOVersion = params["rnbo_version"].get<std::string>();
				}

				patcherStore(name, libFile, configFileName, rnboPatchName, maxRNBOVersion, config, migratePresets);

				reportCommandResult(id, {
					{"code", 0},
					{"message", "stored"},
					{"progress", 100}
				});
			}
	});

	//helper to validate and report as there are 2 different commands
	auto validateFileCmd = [this](const std::string& id, const RNBO::Json& params, bool withData) -> bool {
		//TODO assert that filename doesn't contain slashes so you can't specify files outside of the desired dir?
		if (!params.is_object() || !params.contains("filename") || !params.contains("filetype") || (withData && !params.contains("data"))) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "request object invalid");
			return false;
		}
		reportCommandResult(id, {
			{"code", static_cast<unsigned int>(FileCommandStatus::Received)},
			{"message", "received"},
			{"progress", 1}
		});
		return true;
	};

	//helper to validate and report as there are 2 different commands
	auto fileCmdDir = [this](const std::string& id, std::string filetype, std::string rnboVersion = std::string()) -> boost::optional<fs::path> {
		if (rnboVersion.size() == 0) {
			rnboVersion = rnbo_version;
		}
		boost::optional<fs::path> r;
		if (filetype == "datafile") {
			r = config::get<fs::path>(config::key::DataFileDir);
		} else if (filetype == "sourcefile" || filetype == "patcherfile") {
			r = mSourceCache;
		} else if (filetype == "patcherlib") {
			r = mCompileCache;
		} else if (filetype == "package") {
			r = packagedir(rnboVersion);
		} else if (filetype == "set") {
			r = config::get<fs::path>(config::key::SaveDir).get();
		} else {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "unknown filetype " + filetype);
			return {};
		}
		if (!r) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::Unknown), "no entry in config for filetype: " + filetype);
		}
		return r;
	};

	mCommandHandlers.insert({
			"file_delete",
			[this, validateFileCmd, fileCmdDir](const std::string& method, const std::string& id, const RNBO::Json& params) {
				if (!validateFileCmd(id, params, false))
					return;
				std::string filetype = params["filetype"];
				std::string rnboVersion = rnbo_version;
				if (params.contains("rnbo_version")) {
					rnboVersion = params["rnbo_version"];
				}

				auto dir = fileCmdDir(id, filetype, rnboVersion);
				if (!dir)
					return;

				std::string fileName = params["filename"];
				fs::path filePath = dir.get() / fs::path(fileName);
				boost::system::error_code ec;
				if (fs::remove(filePath, ec)) {
					reportCommandResult(id, {
						{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
						{"message", "deleted"},
						{"progress", 100}
					});

					//update file stats after datafile delete
					if (filetype == "datafile") {
						mDatafilePollNext = steady_clock::now() + datafile_debounce_timeout;
					}

				} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::DeleteFailed), "delete failed with message " + ec.message());
				}
			}
	});

	auto file_write =  [this, validateFileCmd, fileCmdDir](const std::string& method, const std::string& id, const RNBO::Json& params) {
		if (!validateFileCmd(id, params, true))
			return;

		std::string filetype = params["filetype"];

		//file_write_extended base64 encodes the file name so we can have non ascii in there
		std::string fileName = params["filename"].get<std::string>();
		try {
			bool isSet = filetype == "set";
			if (method == "file_write_extended" && !base64_decode_inplace(fileName)) {
				reportCommandError(id, static_cast<unsigned int>(FileCommandError::DecodeFailed), "failed to decode filename");
				return;
			}

			//update timeout for datafile polling while file is writing, we want to read after all file operations complete
			if (filetype == "datafile") {
				mDatafilePollNext = steady_clock::now() + datafile_debounce_timeout;
			}

			//special handling for sets, filename == set name, we actually store
			fs::path filePath;
			if (isSet) {
				filePath = saveFilePath(fileName);
			} else {
				auto dir = fileCmdDir(id, filetype);
				if (!dir)
					return;
				filePath = dir.get() / fs::path(fileName);
			}

			std::fstream fs;
			//allow for "append" to add to the end of an existing file
			bool append = params["append"].is_boolean() && params["append"].get<bool>();
			fs.open(filePath.string(), std::fstream::out | std::fstream::binary | (append ? std::fstream::app : std::fstream::trunc));
			if (!fs.is_open()) {
				reportCommandError(id, static_cast<unsigned int>(FileCommandError::WriteFailed), "failed to open file for write: " + filePath.string());
				return;
			}

			std::string data = params["data"];
			if (data.size() > 0) {
				std::vector<char> out(data.size()); //out will be smaller than the in data
				size_t read = 0;
				if (base64_decode(data.c_str(), data.size(), &out.front(), &read, 0) != 1) {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::DecodeFailed), "failed to decode data");
					return;
				}
				fs.write(&out.front(), sizeof(char) * read);
				fs.close();
			}

			const bool complete = params.contains("complete") && params["complete"].get<bool>();

			if (complete) {
				std::cout << "wrote " << filetype << " file: " << fileName << std::endl;
			}

			//special handling for set saving
			if (isSet && complete) {

				RNBO::Json setData;
				{
					std::ifstream i(filePath.string());
					i >> setData;
					i.close();
				}

				SetInfo info = SetInfo::fromJson(setData);
				if (info.instances.size() > 0) {
					mDB->setSave(fileName, info);
					storeSetContent(setData, mDB, fileName);
					updateSetNames();
				}
			}

			reportCommandResult(id, {
					{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
					{"message", "written"},
					{"progress", 100}
					});
		} catch (const std::exception& e) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::WriteFailed), std::string("write failed with message: \"") + e.what() + "\" for file: " + fileName + " type: " + filetype);
		}
	};

	auto file_read = [this, fileCmdDir](const std::string& method, const std::string& id, const RNBO::Json& params) {
		//TODO validate
		if (!params.contains("size") || !params["size"].is_number()) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "must include size param");
		}

		int size = params["size"];
		std::string filetype = params["filetype"];
		std::string fileName;
		std::string readContent;
		std::string rnboVersion;
		if (params.contains("rnbo_version")) {
			rnboVersion = params["rnbo_version"];
		}

		if (params.contains("filename")) {
			fileName = params["filename"];
		}

		if (filetype == "presets") {
			//read in content
			//filename is actually patcher name
			std::string patcherName = fileName;
			readContent.clear();

			std::vector<std::string> names;
			mDB->presets(patcherName, [&names](const std::string& n, bool) { names.push_back(n); }, rnboVersion);

			RNBO::Json content = RNBO::Json::object();
			for (auto name: names) {
				auto preset = mDB->preset(patcherName, name, rnboVersion);
				if (preset) {
					content[name] = RNBO::Json::parse(preset->first);
				} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "preset does not exist");
					return;
				}
			}

			readContent = content.dump();
		} else if (filetype == "sets") {
			std::vector<std::string> names;
			mDB->sets([&names](const std::string& n, const std::string&, bool initial) { names.push_back(n); }, rnboVersion);

			RNBO::Json content = RNBO::Json::object();
			for (auto name: names) {
				if (name == UNTITLED_SET_NAME || name == LAST_SET_NAME) {
					continue;
				}
				auto setInfo = mDB->setGet(name, rnboVersion);
				if (setInfo) {
					RNBO::Json s = setInfo->toJson();
					addSetContent(s, mDB, name, rnboVersion, true, true);
					content[name] = s;
				}
			}

			readContent = content.dump();
		} else if (filetype == "patcher" || filetype == "patcherconfig") {
			//get the latest from this version
			fs::path libPath;
			fs::path confName;
			fs::path patcherName;
			std::string created_at;
			if (mDB->patcherGetLatest(fileName, libPath, confName, patcherName, created_at, rnboVersion)) {
				fs::path contentName = filetype == "patcher" ? patcherName : confName;
				fs::path filePath = fs::path(mSourceCache) / fs::path(contentName);
				RNBO::Json content = RNBO::Json::object();
				if (fs::exists(filePath)) {
					std::ifstream i(filePath.string());
					std::stringstream b;
					b << i.rdbuf();
					content["content"] = b.str();
					content["filename"] = contentName.string();
					content["created_at"] = created_at;
					readContent = content.dump();
				} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "cannot find " + filetype + " file");
					return;
				}
			} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "cannot find patcher");
					return;
			}
		} else if (filetype == "patchers") {
			RNBO::Json content = RNBO::Json::array();
			//get patcher names
			mDB->patchers([&content](const std::string& v, int, int, int, int, const std::string&) {
					content.push_back(v);
			}, rnboVersion);
			readContent = content.dump();
		} else if (filetype == "versions") {
			RNBO::Json content = RNBO::Json::array();
			mDB->rnboVersions([&content](const std::string& v) {
					content.push_back(v);
			});
			readContent = content.dump();
		} else {
			auto dir = fileCmdDir(id, filetype, rnboVersion);
			if (!dir) {
				reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "invalid directory");
				return;
			}

			//read in file
			if (fileName.size()) {
				fs::path filePath = dir.get() / fs::path(fileName);
				if (fs::exists(filePath)) {
					std::ifstream i(filePath.string());
					if (method != "file_read64") {
						std::stringstream b;
						b << i.rdbuf();
						readContent = b.str();
					} else {
						using boost::uuids::detail::md5;

						const size_t fileBytes = fs::file_size(filePath);
						const double fileSize = static_cast<double>(fileBytes);

						md5 hash;
						int seq = 0;
						size_t remaining = fileBytes;

						auto report = [this, id, &seq, &remaining, fileSize](ssize_t read, const std::vector<char>& out, size_t nout, std::string md5sum = std::string()) {
							if (nout == 0 && md5sum.size() == 0) //if there is no data, don't report, but we must report if there is a md5
								return;

							std::string chunk;
							if (nout > 0) {
								chunk = std::string(out.begin(), out.begin() + nout);
							}
							remaining -= read;

							RNBO::Json resp = {
								{"code", static_cast<unsigned int>(FileCommandStatus::Received)},
								{"message", "read"},
								{"content64", chunk},
								{"seq", seq++},
								{"remaining", remaining},
								{"progress", static_cast<int>(std::clamp(100.0 * ((fileSize - remaining) / fileSize), 0.0, 99.0))}
							};

							if (md5sum.size() > 0) {
								resp["code"] = static_cast<unsigned int>(FileCommandStatus::Completed);
								resp["md5"] = md5sum;
								resp["progress"] = 100;
							}

							reportCommandResult(id, resp);
						};

						//stream, don't read all into ram
						std::vector<char> buf(size);
						std::vector<char> out(size * 4); //what is the correct max encoded size?
						struct base64_state state;

						// Initialize stream encoder:
						base64_stream_encode_init(&state, 0);

						size_t nout = 0;
						while (i.good()) {
							i.read(buf.data(), size);
							ssize_t read = i.gcount();

							if (read > 0) {
								hash.process_bytes(buf.data(), read);
								base64_stream_encode(&state, buf.data(), read, out.data(), &nout);
								report(read, out, nout);
							}

						}

						base64_stream_encode_final(&state, out.data(), &nout);

						//https://stackoverflow.com/questions/55070320/how-to-calculate-md5-of-a-file-using-boost
						std::string md5sum;
						{
							md5::digest_type digest;
							hash.get_digest(digest);
							const auto intDigest = reinterpret_cast<const int*>(&digest);
							boost::algorithm::hex(intDigest, intDigest + (sizeof(md5::digest_type)/sizeof(int)), std::back_inserter(md5sum));
						}

						report(0, out, nout, md5sum);
						return;
					}
				} else {
					std::string msg = "file does not exist at path " + filePath.string();
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), msg);
					return;
				}
			} else {
				fs::path dirPath = dir.get();
				if (fs::exists(dirPath)) {
					RNBO::Json content = RNBO::Json::array();
					for (const auto& entry: fs::directory_iterator(dir.get())) {
						content.push_back(entry.path().string());
					}
					readContent = content.dump();
				} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "dir doesn't exist");
					return;
				}
			}
		}

		if (readContent.size() == 0) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::ReadFailed), "no content");
			return;
		}

		int remaining = 0;
		double fileSize = readContent.size();
		double read = 0;
		int seq = 0;

		//XXX there is probably a more efficient way to do this
		while (readContent.size()) {
			std::string chunk = readContent.substr(0, size);
			readContent.erase(0, size);
			read += size;
			remaining = readContent.size();
			reportCommandResult(id, {
				{"code", static_cast<unsigned int>(remaining == 0 ? FileCommandStatus::Completed : FileCommandStatus::Received)},
				{"message", "read"},
				{"content", chunk},
				{"seq", seq++},
				{"remaining", remaining},
				{"progress", remaining == 0 ? 100 : static_cast<int>(std::clamp(100.0 * read / fileSize, 0.0, 99.0))}
			});
		}
	};

	mCommandHandlers.insert({ "file_write", file_write });
	mCommandHandlers.insert({ "file_write_extended", file_write });
	mCommandHandlers.insert({ "file_read", file_read });
	mCommandHandlers.insert({ "file_read64", file_read });

	mCommandHandlers.insert({
			"file_exists",
			[this, fileCmdDir](const std::string& method, const std::string& id, const RNBO::Json& params) {
				//TODO validate
				std::string filetype = params["filetype"];
				std::string fileName = params["filename"];

				auto dir = fileCmdDir(id, filetype);
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
					{"exists", dir ? fs::exists(dir.get() / fs::path(fileName)) : false},
					{"progress", 100}
				});
			}
	});

	mCommandHandlers.insert({
			"package_create",
			[this, fileCmdDir](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::string packagename;

				std::string rnboVersion;
				PackageConfig config;

				config.system_pretty_name = mSystemPrettyName;

				if (params.contains("rnbo_version")) {
					rnboVersion = params["rnbo_version"];
				} else {
					rnboVersion = rnbo_version;
				}

				if (params.contains("include_presets")) {
					config.include_presets = params["include_presets"].get<bool>();
				}
				if (params.contains("include_views")) {
					config.include_views = params["include_views"].get<bool>();
				}
				if (params.contains("include_datafiles")) {
					config.include_datafiles = params["include_datafiles"].get<bool>();
				}
				if (params.contains("include_binaries")) {
					config.include_binaries = params["include_binaries"].get<bool>();
				}

				std::set<std::string> patchernames;
				std::set<std::string> setnames;
				if (params.contains("all")) {
					packagename = "all";
					mDB->patchers([&patchernames](const std::string& name, int, int, int, int, const std::string&) { patchernames.insert(name); }, rnboVersion);
					mDB->sets([&setnames](const std::string& name, const std::string& /*created*/, bool /*initial*/ ) { setnames.insert(name); }, rnboVersion);
				} else if (params.contains("set")) {
					std::string setname = params["set"];
					setnames.insert(setname);

					auto setInfo = mDB->setGet(setname, rnboVersion);
					if (!setInfo) {
						reportCommandError(id, static_cast<unsigned int>(PackageCommandError::NotFound), "set does not exist");
						return;
					}
					packagename = "graph-" + setname;

				} else if (params.contains("patcher")) {
					std::string name = params["patcher"];

					fs::path libPath;
					fs::path confPath;
					fs::path patcherPath;
					std::string created_at;

					if (!mDB->patcherGetLatest(name, libPath, confPath, patcherPath, created_at, rnboVersion)) {
						reportCommandError(id, static_cast<unsigned int>(PackageCommandError::NotFound), "patcher does not exist");
						return;
					}

					patchernames.insert(name);
					packagename = "patcher-" + name;
				} else {
						reportCommandError(id, static_cast<unsigned int>(PackageCommandError::NotFound), "don't know what to package");
						return;
				}

				try {
					auto tarname = createPackage(mDB, packagename, setnames, patchernames, config, rnboVersion);
					reportCommandResult(id, {
							{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
							{"message", "completed"},
							{"packagename", packagename},
							{"filename", tarname.string()},
							{"progress", 100}
						});
				} catch (std::runtime_error& e) {
					reportCommandError(id, static_cast<unsigned int>(PackageCommandError::WriteFailed), e.what());
				}
			}
	});

	mCommandHandlers.insert({
			"package_install",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
				//TODO validate
				std::string filename = params["filename"];

				reportCommandResult(id, {
						{"code", static_cast<unsigned int>(FileCommandStatus::Received)},
						{"message", "received"},
						{"progress", 1}
					});

				fs::path packagelocation = fs::absolute(packagedir(rnbo_version) / filename);
				if (!fs::exists(packagelocation)) {
					std::string msg = "cannot find package file at: " + packagelocation.string();
					reportCommandError(id, static_cast<unsigned int>(PackageCommandError::NotFound), msg);
				}

				//delete and recreate working directory
				fs::path tmpdir = config::get<fs::path>(config::key::TempDir).get() / "rnbopackageworking";
				//remove anything that might already be there
				boost::system::error_code ec;
				fs::remove_all(tmpdir, ec);
				fs::create_directories(tmpdir);

				//extract
				std::vector<std::string> args { "xf", packagelocation.string() };
				if (bp::system(bp::search_path("tar"), args, bp::start_dir(tmpdir)) != 0) {
					reportCommandError(id, static_cast<unsigned int>(PackageCommandError::Unknown), "failed to unarchive package");
					return;
				}

				try {
					//find content location
					fs::path contentlocation;
					for (const auto& entry: fs::directory_iterator(tmpdir)) {
						if (!contentlocation.empty()) {
							throw std::runtime_error("cannot find single package directory in extracted data");
						}
						contentlocation = fs::absolute(entry.path());
					}
					if (contentlocation.empty()) {
						throw std::runtime_error("cannot find package data in extracted data");
					}
					installPackage(contentlocation);
					reportCommandResult(id, {
							{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
							{"message", "completed"},
							{"progress", 100}
						});
				} catch (std::runtime_error& e) {
					reportCommandError(id, static_cast<unsigned int>(PackageCommandError::Unknown), e.what());
				}
				fs::remove_all(tmpdir, ec);
			}
	});

	auto validateListenerCmd = [this](const std::string& id, const RNBO::Json& params, std::pair<std::string, uint16_t>& key) -> bool {
		if (!params.is_object() || !params.contains("ip") || !params.contains("port")) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "request object invalid");
			return false;
		}
		auto ip = params["ip"].get<std::string>();
		auto port = static_cast<uint16_t>(params["port"].get<int>());
		key = std::make_pair(ip, port);

		//protect against feedback
		if (ip == "127.0.0.1" && (port == 1234 || port == 5678)) {
			reportCommandError(id, static_cast<unsigned int>(ListenerCommandStatus::Failed), "feedback detected");
			return false;
		}

		reportCommandResult(id, {
			{"code", static_cast<unsigned int>(ListenerCommandStatus::Received)},
			{"message", "received"},
			{"progress", 1}
		});
		return true;

	};

	//clear all but oscquery
	auto listeners_clear = [this]() {
		//deleting invalidates iterator, so we loop until we haven't removed any
		bool found = true;
		while (found) {
			found = false;
			for (auto& p: mProtocol->get_protocols()) {
				auto o = dynamic_cast<ossia::oscquery_asio::oscquery_server_protocol*>(p.get());
				if (!o) {
					mProtocol->stop_expose_to(*p);
					found = true;
					break;
				}
			}
		}
	};

	mCommandHandlers.insert({
			"listener_add",
			[this, validateListenerCmd](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, params, key))
					return;

				if (!mDB->listenerExists(key.first, key.second)) {
					try {
						std::lock_guard<std::mutex> guard(mOssiaContextMutex);
						listenersAddProtocol(key.first, key.second);
						mDB->listenersAdd(key.first, key.second);
					} catch (const std::exception&) {
						reportCommandError(id, static_cast<unsigned int>(ListenerCommandStatus::Failed), "failed add listener for ip: " + key.first + " and port: " + std::to_string(key.second));
						return;
					}
				}
				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "created"},
					{"progress", 100}
				});
			}
	});
	mCommandHandlers.insert({
			"listener_del",
			[this, validateListenerCmd, listeners_clear](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, params, key))
					return;

				//TODO visit listeners and only remove the one we care to remove.
				//at this point, finding the type is hard so we just remove them all and then add back the ones we care about
				if (mDB->listenersDel(key.first, key.second)) {

					std::lock_guard<std::mutex> guard(mOssiaContextMutex);
					listeners_clear();
					restoreListeners();
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "deleted"},
					{"progress", 100}
				});
			}
	});
	mCommandHandlers.insert({
			"listener_clear",
			[this, validateListenerCmd, listeners_clear](const std::string& method, const std::string& id, const RNBO::Json& params) {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, params, key))
					return;

				std::lock_guard<std::mutex> guard(mOssiaContextMutex);
				mDB->listenersClear();
				listeners_clear();
				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "cleared"},
					{"progress", 100}
				});
			}
	});

	mCommandHandlers.insert({
			"install",
			[this](const std::string& method, const std::string& id, const RNBO::Json& params) {
#ifndef RNBO_USE_DBUS
				reportCommandError(id, static_cast<unsigned int>(InstallProgramError::NotEnabled), "self update not enabled for this runner instance");
				return;
#else
				if (!mUpdateServiceProxy) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::NotEnabled), "dbus object does not exist");
					return;
				}
				if (!params.is_object() || !params.contains("version")) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::InvalidRequestObject), "request object invalid");
					return;
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(InstallProgramStatus::Received)},
					{"message", "signaling update service"},
					{"progress", 10}
				});
				std::string version = params["version"];
				try {
					if (!mUpdateServiceProxy->QueueRunnerInstall(version)) {
						reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), "service reported error, check version string");
						return;
					}
					reportCommandResult(id, {
							{"code", static_cast<unsigned int>(InstallProgramStatus::Completed)},
							{"message", "installation initiated"},
							{"progress", 100}
					});
				} catch (...) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), "dbus reported error");
					return;
				}
#endif
			}
	});

}

void Controller::processCommands() {
	try {
		const bool compiling = compileProcess && compileProcess->valid();
		if (compiling) {
			//see if the process has completed
			if (!compileProcess->mProcess.running()) {
				//need to wait to get the correct exit code
				compileProcess->mProcess.wait();

				auto status = compileProcess->mProcess.exit_code();
				auto id = compileProcess->mCommandId;
				auto libPath = compileProcess->mLibPath;
				auto conf = compileProcess->mConf;
				auto confFilePath = compileProcess->mConfFilePath;
				auto rnboPatchPath = compileProcess->mRNBOPatchPath;
				auto instanceIndex = compileProcess->mInstanceIndex;
				auto maxRNBOVersion = compileProcess->mMaxRNBOVersion;
				auto migratePresets = compileProcess->mMigratePresets;

				compileProcess.reset();
				if (status != 0) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::CompileFailed), "compile failed with status: " + std::to_string(status));
				} else if (fs::exists(libPath)) {
					if (conf.contains("name") && conf["name"].is_string()) {
						std::string name = conf["name"].get<std::string>();

						patcherStore(name, libPath.filename(), confFilePath.filename(), rnboPatchPath.filename(), maxRNBOVersion, conf, migratePresets);
					}

					if (instanceIndex != boost::none) {
						reportCommandResult(id, {
							{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
							{"message", "compiled"},
							{"progress", 90}
						});
						auto inst = loadLibrary(libPath.string(), id, conf, true, instanceIndex.get(), confFilePath);
						if (inst) {
							inst->connect();
							inst->start(mInstFadeInMs);
						}
					} else {
						reportCommandResult(id, {
							{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
							{"message", "compiled"},
							{"progress", 100}
						});
					}
				} else {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::LibraryNotFound), "couldn't find compiled library at " + libPath.string());
				}
			}
		}

		while (auto cmd = mCommandQueue.tryPop()) {
			std::string cmdStr = cmd.get();

			//internal commands
			if (cmdStr == "load_last") {
				//terminate existing compile
				compileProcess.reset();

				loadSet(UNTITLED_SET_NAME);
				continue;
			}

			auto cmdObj = RNBO::Json::parse(cmdStr);
			if (!cmdObj.contains("method") || !cmdObj.contains("id")) {
				cerr << "invalid cmd json" << cmdStr << endl;
				continue;
			}
			std::string id = cmdObj["id"];
			std::string method = cmdObj["method"];
			RNBO::Json params = cmdObj["params"];
			if (method == "compile_cancel") {
				//should terminate
				compileProcess.reset();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Cancelled)},
					{"message", "cancelled"},
					{"progress", 100}
				});
				continue;
			} else if (method == "compile") {
				//terminate existing
				compileProcess.reset();

				std::string timeTag = std::to_string(std::chrono::seconds(std::time(NULL)).count());
#if RNBO_USE_DBUS
				//update the outpdated package list
				if (mUpdateServiceProxy && params.contains("update_outdated") && params["update_outdated"].get<bool>()) {
					try {
						mUpdateServiceProxy->UpdateOutdated();
					} catch (...) { }
				}
#endif

				//support either a pre-written file or embedded "code"
				if (!params.is_object() || !(params.contains("filename") || params.contains("code"))) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::InvalidRequestObject), "request object invalid");
					continue;
				}
				//get filename or generate one
				std::string fileName = params.contains("filename") ? params["filename"].get<std::string>() : ("rnbogen." + timeTag + ".cpp");
				fs::path sourceFile = fs::absolute(mSourceCache / fileName);

				//write code if we have it
				if (params.contains("code")) {
					std::string code = params["code"];
					std::fstream f;
					f.open(sourceFile.string(), std::fstream::out | std::fstream::trunc);
					if (!f.is_open()) {
						reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceWriteFailed), "failed to open file for write: " + sourceFile.string());
						continue;
					}
					f << code;
					f.close();
				}

				//make sure the source file exists
				if (!fs::exists(sourceFile)) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceFileDoesNotExist), "cannot file source file: " + sourceFile.string());
					continue;
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Received)},
					{"message", "received"},
					{"progress", 10}
				});

				//config might be in a file
				RNBO::Json config;
				fs::path confFilePath;
				if (params.contains("config_file")) {
					confFilePath = params["config_file"].get<std::string>();
					confFilePath = fs::absolute(mSourceCache / confFilePath);
					std::ifstream i(confFilePath.string());
					i >> config;
					i.close();
				} else if (params.contains("config")) {
					config = params["config"];
				}

				std::string libName = fs::path(fileName).replace_extension().string();
				fs::path libPath = fs::absolute(mCompileCache / fs::path(std::string(RNBO_DYLIB_PREFIX) + libName + "." + rnbo_dylib_suffix));
				//program path_to_generated.cpp libraryName pathToConfigFile
				std::vector<std::string> args = {
					sourceFile.string(), libName, config::get<fs::path>(config::key::RnboCPPDir).get().string(), config::get<fs::path>(config::key::CompileCacheDir).get().string()
				};
				auto cmake = config::get<fs::path>(config::key::CMakePath);
				if (cmake) {
					args.push_back(cmake.get().string());
				}

				//start compile
				{
					boost::optional<unsigned int> instanceIndex = 0;
					fs::path rnboPatchPath;

					std::string maxRNBOVersion = "unknown";

					if (params.contains("patcher_file")) {
						rnboPatchPath = params["patcher_file"].get<std::string>();
						rnboPatchPath = fs::absolute(mSourceCache / rnboPatchPath);
					}

					if (params.contains("rnbo_version")) {
						maxRNBOVersion = params["rnbo_version"].get<std::string>();
					}

					bool migratePresets = params.contains("migrate_presets") && params["migrate_presets"].is_boolean() && params["migrate_presets"].get<bool>();

					if (params.contains("load")) {
						if (params["load"].is_null()) {
							instanceIndex = boost::none;
						} else {
							int index = params["load"].get<int>();
							if (index < 0) {
								index = nextInstanceIndex();
							}
							instanceIndex = boost::make_optional(static_cast<unsigned int>(index));
							{
								std::lock_guard<std::mutex> guard(mBuildMutex);
								unloadInstance(guard, instanceIndex.get());
							}
							mProcessAudio->updatePorts();
						}
					}
					compileProcess = CompileInfo(build_program, args, libPath, id, config, confFilePath, rnboPatchPath, maxRNBOVersion, migratePresets, instanceIndex);
				}
			} else {
				try {
					auto f = mCommandHandlers.find(method);
					if (f != mCommandHandlers.end()) {
						f->second(method, id, params);
					} else {
						cerr << "unknown method " << method << endl;
					}
				} catch (const std::exception& e) {
					std::string message = "error processing method: " + method + " with error: " + e.what();
					reportCommandError(id, 1000, message);
				}
			}
		}
	} catch (const std::exception& e) {
		cerr << "exception processing command " << e.what() << endl;
	} catch (...) {
		cerr << "unknown exception processing command " << endl;
	}
}

void Controller::reportCommandResult(std::string id, RNBO::Json res) {
	reportCommandStatus(id, { {"result", res} });
}

void Controller::reportCommandError(std::string id, unsigned int code, std::string message) {
	reportCommandStatus(id, {
			{ "error",
			{
				{ "code", code },
				{ "message", message }
			}
			}
	}, true);
}

void Controller::reportCommandStatus(std::string id, RNBO::Json obj, bool printerr) {
	obj["jsonrpc"] = "2.0";
	obj["id"] = id;
	std::string status = obj.dump();
	if (id == "internal") {
		std::cout << status << std::endl;
	} else {
		mResponseParam->push_value(status);
		if (printerr) {
			std::cerr << status << std::endl;
		}
	}
}

void Controller::updateDiskStats() {
	//could also look at sample dir?
	fs::space_info compileCacheSpace = fs::space(fs::absolute(config::get<fs::path>(config::key::CompileCacheDir).get()));
	auto available = compileCacheSpace.available;
	if (mDiskSpaceLast != available) {
		mDiskSpaceLast = available;
		if (mDiskSpaceParam)
			mDiskSpaceParam->push_value(std::to_string(mDiskSpaceLast));
	}
	mDiskSpacePollNext = steady_clock::now() + mDiskSpacePollPeriod;
}

void Controller::updateDatafileStats() {
	auto dir = fs::absolute(config::get<fs::path>(config::key::DataFileDir).get());
	if (fs::exists(dir)) {
		auto time = fs::last_write_time(dir);

		char timeString[std::size("yyyy-mm-ddThh:mm:ssZ")];
		std::strftime(std::data(timeString), std::size(timeString), "%FT%TZ", std::gmtime(&time));

		std::string s(timeString);

		if (s != mDataFileDirMTimeLast) {
			mDataFileDirMTimeParam->push_value(s);
			mDataFileDirMTimeLast = s;
		}
	}

	mDatafilePollNext = steady_clock::now() + mDatafilePollPeriod;
}

void Controller::updateListenersList() {
	std::vector<ossia::value> l;
	mDB->listeners([&l](const std::string& ip, uint16_t port) {
		l.push_back(ip + ":" + std::to_string(port));
	});
	mListenersListParam->push_value(l);
}

void Controller::restoreListeners() {
	mDB->listeners([this](const std::string& ip, uint16_t port) {
			try {
				listenersAddProtocol(ip, port);
			} catch (...) {
				std::cerr << "error adding listener ip: " << ip << " port: " << std::to_string(port) << std::endl;
			}
	});
	updateListenersList();
}

void Controller::listenersAddProtocol(const std::string& ip, uint16_t port) {
	using conf = ossia::net::osc_protocol_configuration;
	auto protocol = ossia::net::make_osc_protocol(
			mOssiaContext,
			{
				.mode = conf::HOST,
				.version = conf::OSC1_1,
				.framing = conf::SLIP, //gcc doesn't like the default members, so we specify this even though it is a default
				.transport = ossia::net::udp_configuration {{
					.local = std::nullopt,
					.remote = ossia::net::send_socket_configuration {{ip, port}}
				}}
			}
		);
	mProtocol->expose_to(std::move(protocol));
}

void Controller::handleProgramChange(ProgramChange p) {
	{
		auto chan = mPatcherProgramChangeChannel;
		if (chan == 0 || chan == (p.chan + 1)) {
			auto name = mDB->patcherNameByIndex(p.prog);
			if (name) {
				RNBO::Json cmd = {
					{"method", "instance_load"},
					{"id", "internal"},
					{"params",
						{
							{"index", 0},
							{"patcher_name", name.get()},
						}
					}
				};
				mCommandQueue.push(cmd.dump());
			} else {
				std::cerr << "no patcher at index " << (int)p.prog << std::endl;
			}
		}
	}
	{
		auto chan = mSetProgramChangeChannel;
		if (chan == 0 || chan == (p.chan + 1)) {
			auto name = mDB->setNameByIndex(p.prog);
			if (name) {
				RNBO::Json cmd = {
					{"method", "instance_set_load"},
					{"id", "internal"},
					{"params",
						{
							{"name", name.get()},
						}
					}
				};
				mCommandQueue.push(cmd.dump());
			} else {
				std::cerr << "no set at index " << (int)p.prog << std::endl;
			}

		}
	}
	{
		auto chan = mSetPresetProgramChangeChannel;
		if (chan == 0 || chan == (p.chan + 1)) {
			auto setname = getCurrentSetName();
			if (setname.size() == 0) {
				std::cerr << "cannot find current set name to load set preset for" << std::endl;
				return;
			}

			auto name = mDB->setPresetNameByIndex(setname, p.prog);
			if (name) {
				RNBO::Json cmd = {
					{"method", "instance_set_preset_load"},
					{"id", "internal"},
					{"params",
						{
							{"name", name.get()},
						}
					}
				};
				mCommandQueue.push(cmd.dump());
			} else {
				std::cerr << "no set preset at index " << (int)p.prog << std::endl;
			}

		}
	}
}

void Controller::ensureSet(std::string& name) {
	if (name == UNTITLED_SET_NAME) {
		//TODO check dirty?
		auto info = setInfo();
		mDB->setSave(name, info);
	}
}

std::string Controller::getCurrentSetName() {
	std::string name;
	if (mSetCurrentNameParam && mSetCurrentNameParam->get_value_type() == ossia::val_type::STRING) {
		name = mSetCurrentNameParam->value().get<std::string>();
	}
	return name.size() ? name : UNTITLED_SET_NAME;
}

std::string Controller::getCurrentSetPresetName() {
	if (mSetPresetLoadedParam && mSetPresetLoadedParam->get_value_type() == ossia::val_type::STRING) {
		return mSetPresetLoadedParam->value().get<std::string>();
	}
	return std::string();
}
