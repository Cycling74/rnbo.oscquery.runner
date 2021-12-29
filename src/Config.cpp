#include "Config.h"
#include "RNBO.h"

#include <regex>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <chrono>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/none.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>

using std::chrono::system_clock;

namespace fs = boost::filesystem;

namespace {
	static const std::chrono::seconds save_debounce_timeout(1);
	boost::optional<std::chrono::time_point<std::chrono::system_clock>> update_next;

	static std::mutex mutex;
	//XXX figure out for windows
	const static std::string home_str = fs::absolute(fs::path(std::getenv("HOME"))).string();
	const static std::regex tilde("~");

	static fs::path default_so_cache = config::make_path("~/Documents/rnbo/cache/so");
	static fs::path default_src_cache = config::make_path("~/Documents/rnbo/cache/src");
	static fs::path default_save_dir = config::make_path("~/Documents/rnbo/saves/");
	static fs::path default_datafile_dir = config::make_path("~/Documents/rnbo/datafiles/");

	static fs::path home_dir_config_file_path = config::make_path("~/.config/rnbo/runner.json");

	static fs::path runner_uuid_path = config::make_path("~/.config/rnbo/runner-id.txt");

	static boost::uuids::uuid system_id = boost::uuids::random_generator()();

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
		{config::key::InstanceAutoStartLast, true},
		{config::key::InstanceAutoConnectAudio, true},
		{config::key::InstanceAutoConnectMIDI, true},
		{config::key::PresetMIDIProgramChangeChannel, "omni"},
	};

	RNBO::Json config_json = config_default;

	//get the namespaced json blob, expects to be in a lock
	boost::optional<RNBO::Json&> ns_json(boost::optional<std::string> ns, bool create = false) {
		auto& j = config_json;
		if (!ns)
			return j;
		if (j.contains(ns.get()))
			return j[ns.get()];
		if (create) {
			j[ns.get()] = RNBO::Json::object();
			return j[ns.get()];
		}
		return boost::optional<RNBO::Json&>{ boost::none };
	}
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
		{
			bool write = true;
			if (fs::exists(runner_uuid_path)) {
				try {
					std::ifstream i(runner_uuid_path.string());
					std::string line;
					if (std::getline(i, line)) {
						system_id = boost::lexical_cast<boost::uuids::uuid>(line);
						write = false;
					}
				} catch (...) {
				}
			}
			if (write) {
				std::ofstream o(runner_uuid_path.string());
				o << boost::lexical_cast<std::string>(system_id) << std::endl;
				o << "//automatically generated once by rnbo" << std::endl;
			}
		}

		// /foo/bar/bin/exename -> /foo/bar
		base_dir = boost::filesystem::canonical(boost::dll::program_location()).parent_path().parent_path();

		//find the path
		for (auto p: {
				home_dir_config_file_path,
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

	void write_if_dirty() {
		with_mutex<void>([](){
				if (update_next && update_next.get() <= system_clock::now()) {
					update_next.reset();

					//always write to the homedir one
					config_file_path = home_dir_config_file_path;
					fs::path dir;
					fs::create_directories((dir = config_file_path).remove_filename());
					std::ofstream o(config_file_path.string());
					o << std::setw(4) << config_json << std::endl;
				}
		});
	}

	template<>
		boost::optional<boost::filesystem::path> get<boost::filesystem::path>(const std::string& k, boost::optional<std::string> ns) {
		boost::optional<std::string> p = get<std::string>(k, ns);
		if (!p && !ns) {
			if (k == key::RnboCPPDir) {
				return base_dir / "src" / "rnbo";
			}
			if (k == key::SOBuildExe) {
				return base_dir / "bin" / "rnbo-compile-so";
			}
			return boost::none;
		}
		return make_path(p.get());
	}

	template<>
	boost::optional<bool> get(const std::string& key, boost::optional<std::string> ns) {
		return with_mutex<boost::optional<bool>>([key, ns](){
				auto j = ns_json(ns);
				if (j && j->contains(key)) {
					auto v = j->at(key);
					if (v.is_boolean())
						return boost::make_optional(v.get<bool>());
				}
				return boost::optional<bool>{ boost::none };
		});
	}

	template<typename T>
	boost::optional<T> get(const std::string& key, boost::optional<std::string> ns) {
		return with_mutex<boost::optional<T>>([key, ns](){
				auto j = ns_json(ns);
				if (j && j->contains(key)) {
					return boost::make_optional(j->at(key).get<T>());
				}
				return boost::optional<T>{ boost::none };
		});
	}


	template <typename T>
	void set(const T& value, const std::string& key, boost::optional<std::string> ns) {
		return with_mutex<void>([value, key, ns](){
				auto j = ns_json(ns, true);
				j.get()[key] = value;
				//set the write update timeout
				update_next = system_clock::now() + save_debounce_timeout;
		});
	}

	//impl for the types we need
	template boost::optional<std::string> get(const std::string& key, boost::optional<std::string> ns);
	template boost::optional<int> get(const std::string& key, boost::optional<std::string> ns);
	template boost::optional<double> get(const std::string& key, boost::optional<std::string> ns);
	template void set<bool>(const bool& value, const std::string& key, boost::optional<std::string> ns);
	template void set<int>(const int& value, const std::string& key, boost::optional<std::string> ns);
	template void set<double>(const double& value, const std::string& key, boost::optional<std::string> ns);
	template void set<std::string>(const std::string& value, const std::string& key, boost::optional<std::string> ns);

	fs::path make_path(const std::string& str) {
		return fs::absolute(fs::path(std::regex_replace(str, tilde, home_str)));
	}

	std::string get_system_id() {
		return boost::lexical_cast<std::string>(system_id);
	}
}
