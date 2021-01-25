#include "Config.h"
#include "RNBO.h"

#include <regex>
#include <mutex>
#include <fstream>
#include <iomanip>

namespace fs = boost::filesystem;

namespace {
	static std::mutex mutex;
	//XXX figure out for windows
	const static std::string home_str = fs::absolute(fs::path(std::getenv("HOME"))).string();
	const static std::regex tilde("~");

	static fs::path config_file_path = config::make_path(RNBO_CONFIG_DIR) / "runner.json";
	static fs::path default_so_cache = config::make_path(RNBO_CACHE_BASE_DIR) / "so";
	static fs::path default_so_build_dir = config::make_path(RNBO_SO_BUILD_DIR);
	static fs::path default_save_dir = config::make_path(RNBO_CACHE_BASE_DIR) / "saves";
	static fs::path default_src_cache = config::make_path(RNBO_CACHE_BASE_DIR) / "src";
	static fs::path default_datafile_dir = config::make_path(RNBO_CACHE_BASE_DIR) / "datafiles";

	template<typename T>
		T with_mutex(std::function<T()> f) {
			std::lock_guard<std::mutex> guard(mutex);
			return f();
		}

	const RNBO::Json config_default = {
		{config::key::CompileCacheDir, default_so_cache.string()},
		{config::key::SourceCacheDir, default_src_cache.string()},
		{config::key::SaveDir, default_save_dir.string()},
		{config::key::SOBuildExe, std::string()},
		{config::key::SOBuildDir, default_src_cache.string()},
		{config::key::DataFileDir, default_datafile_dir.string()},
		{config::key::InstanceAutoStartLast, true},
		{config::key::InstanceAutoConnectAudio, true},
		{config::key::InstanceAutoConnectMIDI, true},
		{config::key::HostNameOverride, std::string()},
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
		read_file();
		write_file();
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
	boost::filesystem::path get<boost::filesystem::path>(const std::string& key) {
		return make_path(get<std::string>(key));
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
