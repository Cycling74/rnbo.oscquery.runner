#include "Config.h"
#include "RNBO.h"

#include <regex>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <boost/dll/runtime_symbol_info.hpp>

namespace fs = boost::filesystem;

namespace {
	static std::mutex mutex;
	//XXX figure out for windows
	const static std::string home_str = fs::absolute(fs::path(std::getenv("HOME"))).string();
	const static std::regex tilde("~");

	static fs::path default_so_cache = config::make_path("~/Documents/rnbo/cache/so");
	static fs::path default_src_cache = config::make_path("~/Documents/rnbo/cache/src");
	static fs::path default_save_dir = config::make_path("~/Documents/rnbo/saves/");
	static fs::path default_datafile_dir = config::make_path("~/Documents/rnbo/datafiles/");
	static fs::path default_programfile_dir = config::make_path("~/Documents/rnbo/programfiles/");

	//sketchy self update enabled
#if RNBO_SELF_UPDATE
	static bool default_self_update_allowed = true;
	static bool default_install_use_sudo = true;
	static bool default_install_do_ldconfig = true;
	static bool default_install_do_restart = true;
#else
	static bool default_self_update_allowed = false;
	static bool default_install_use_sudo = false;
	static bool default_install_do_ldconfig = false;
	static bool default_install_do_restart = false;
#endif

	//the base dir of our installation
	static fs::path base_dir;
	//the location of our found config file ,if there is one
	static fs::path config_file_path;

	template<typename T>
		T with_mutex(std::function<T()> f) {
			std::lock_guard<std::mutex> guard(mutex);
			return f();
		}

	const RNBO::Json config_default = {
		{config::key::CompileCacheDir, default_so_cache.string()},
		{config::key::SourceCacheDir, default_src_cache.string()},
		{config::key::SaveDir, default_save_dir.string()},
		{config::key::DataFileDir, default_datafile_dir.string()},
		{config::key::ProgramFileDir, default_programfile_dir.string()},
		{config::key::InstanceAutoStartLast, true},
		{config::key::InstanceAutoConnectAudio, true},
		{config::key::InstanceAutoConnectMIDI, true},

		//self update params
		{config::key::SelfUpdateAllowed, false},
		{config::key::InstallPrefix, "/usr/local/"},
		{config::key::InstallUseSudo, default_install_use_sudo},
		{config::key::InstallDoLDConfig, default_install_do_ldconfig},
		{config::key::InstallDoRestart, default_install_do_restart}
	};

	RNBO::Json config_json = config_default;
}

#include <iostream>

namespace config {
	void set_file_path(fs::path p) {
		with_mutex<void>([p](){
			config_file_path = p;
		});
	}
	fs::path file_path() {
		return with_mutex<fs::path>([](){ return config_file_path; });
	}

	void init() {
		// /foo/bar/bin/exename -> /foo/bar
		base_dir = boost::dll::program_location().parent_path().parent_path();

		//find the path
		for (auto p: {
				make_path("~/.config/rnbo/runner.json"),
				base_dir / "share" / "rnbo" / "runner.json"
				}) {
			if (fs::exists(p)) {
				config_file_path = p;
				break;
			}
		}
		read_file();
	}

	void read_file() {
		with_mutex<void>([](){
				config_json = config_default;
				if (fs::exists(config_file_path)) {
					RNBO::Json c;
					std::ifstream i(config_file_path.string());
					i >> c;
					config_json.merge_patch(c);
				}
		});
	}
	void write_file() {
		with_mutex<void>([](){
				fs::path dir;
				fs::create_directories((dir = config_file_path).remove_filename());
				std::ofstream o(config_file_path.string());
				o << std::setw(4) << config_json << std::endl;
		});
	}
	template<>
	boost::filesystem::path get<boost::filesystem::path>(const std::string& k) {
		std::string p = get<std::string>(k);
		if (p.empty()) {
			if (k == key::RnboCPPDir) {
				return base_dir / "src" / "rnbo";
			}
			if (k == key::SOBuildExe) {
				return base_dir / "bin" / "rnbo-compile-so";
			}
			if (k == key::SelfUpdateExe) {
				return base_dir / "bin" / "rnbo-install";
			}
		}
		return make_path(p);
	}
	template<typename T>
	T get(const std::string& key) {
		return with_mutex<T>([key](){
				T value;
				if (config_json.contains(key))
					value = config_json[key];
				return value;
		});
	}
	template<>
	bool get(const std::string& key) {
		return with_mutex<bool>([key](){
				if (config_json.contains(key)) {
					auto v = config_json[key];
					if (v.is_boolean())
						return v.get<bool>();
				}
				return false;
		});
	}
	fs::path make_path(const std::string& str) {
		return fs::absolute(fs::path(std::regex_replace(str, tilde, home_str)));
	}
}
