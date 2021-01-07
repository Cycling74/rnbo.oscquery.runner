#pragma once
#include <filesystem>

namespace config {
	namespace key {
		const static std::string SourceCacheDir = "source_cache_dir"; //where do we generated source we get
		const static std::string CompileCacheDir = "compile_cache_dir"; //where do we store compiled shared objects
		const static std::string SOBuildExe = "so_build_exe"; //optional path to the executable used for building shared objects, if empty, just uses PATH
		const static std::string SOBuildDir = "so_build_dir"; //the directory that contains the CMakeLists.txt for building shared objects
		const static std::string RnboCPPDir = "rnbo_cpp_dir"; //the directory that contains the RNBO c++ source files
		const static std::string DataFileDir = "datafile_dir"; //the directory where data (audio) files can be read from or written to

		const static std::string InstanceAutoConnectAudio = "instance_auto_connect_audio"; //if applicable (Jack), should an instance be automatically connected to audio
		const static std::string InstanceAutoConnectMIDI = "instance_auto_connect_midi"; //if applicable (Jack), should an instance be automatically connected to midi

		const static std::string HostNameOverride = "host_name_override"; //indicate a value to override the host name to report via OSCQuery
	}

	void set_file_path(std::filesystem::path p);
	std::filesystem::path file_path();
	void read_file();
	void write_file();
	void init(); //read and write

	template <typename T>
	T get(const std::string& key);

	//make's an absolute path with ~ replaced with $HOME appropriately
	std::filesystem::path make_path(const std::string& p);
}
