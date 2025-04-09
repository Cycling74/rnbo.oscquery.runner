#pragma once
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/none.hpp>

namespace config {
	namespace key {
		//operation config values, override defaults, usually looks relative to executable
		const static std::string SOBuildExe = "so_build_exe"; //optional path to the executable used for building shared objects.
		const static std::string RnboCPPDir = "rnbo_cpp_dir"; //the directory that contains the RNBO c++ source files

		const static std::string CMakePath = "cmake_path"; //path to cmake executable, useful if if it isn't in $PATH

		//user preference config values
		const static std::string SourceCacheDir = "source_cache_dir"; //where do we generated source we get
		const static std::string CompileCacheDir = "compile_cache_dir"; //where do we store compiled shared objects
		const static std::string DataFileDir = "datafile_dir"; //the directory where data (audio) files can be read from or written to
		const static std::string SaveDir = "save_dir"; //where do we persist saved data, for auto start on restart
		const static std::string BackupDir = "backup_dir"; //where do we save backup (like db) data
		const static std::string DBPath = "db_path"; //where is the database file?
		const static std::string ExportDir = "export_dir"; //where do we store exported data (sets, patchers)
		const static std::string TempDir = "temp_dir"; //where to place temp files
																																																			//
		const static std::string HostNameOverride = "host_name_override"; //indicate a value to override the host name to report via OSCQuery

		const static std::string InstanceAutoStartLast = "instance_auto_start_last"; //try to restart the last run instance (and its settings) on startup.

		const static std::string SetLastName = "set_last_name"; //the name of last set that was saved

		const static std::string InstanceAutoConnectAudio = "instance_auto_connect_audio"; //if applicable (Jack), should an instance be automatically connected to audio
		const static std::string InstanceAutoConnectAudioIndexed = "instance_auto_connect_audio_indexed"; //if applicable (Jack), uses i/o indexes to automatically connect to hardware
		const static std::string InstanceAutoConnectMIDI = "instance_auto_connect_midi"; //if applicable (Jack), should an instance be automatically connected to midi
		const static std::string InstanceAutoConnectMIDIHardware = "instance_auto_connect_midi_hardware"; //if applicable (Jack), should an instance be automatically connected to midi hardware ports
		const static std::string InstanceAutoConnectPortGroup = "instance_auto_connect_port_group"; //if applicable (Jack), should an instance be automatically connected to the rnbo-graph-user-io port group i/o
		const static std::string InstanceAudioFadeIn = "instance_audio_fade_in"; //fade in time when creating new instances
		const static std::string InstanceAudioFadeOut = "instance_audio_fade_out"; //fade out time when creating new instances

		const static std::string InstancePortToOSC = "instance_port_to_osc"; //do we map inport/outport with / prefixes to/from OSC messages at the top of the address space by default
																																							 //
		const static std::string ControlAutoConnectMIDI = "control_auto_connect_midi"; //if applicable (Jack), should the control code (for switching patchers via program change) be automatically connected to midi

		const static std::string PresetMIDIProgramChangeChannel = "preset_midi_program_change_channel"; //string, "omni" for omni 1..16 for specific, "none" or null for none
		const static std::string PatcherMIDIProgramChangeChannel = "patcher_midi_program_change_channel"; //string, "omni" for omni 1..16 for specific, "none" or null for none
		const static std::string SetMIDIProgramChangeChannel = "set_midi_program_change_channel"; //string, "omni" for omni 1..16 for specific, "none" or null for none
		const static std::string SetPresetMIDIProgramChangeChannel = "set_preset_midi_program_change_channel"; //string, "omni" for omni 1..16 for specific, "none" or null for none
		const static std::string UUIDPath = "uuid_path"; //path where we store the unique identifier for the runner

		const static std::string SetPresetDefaultPatcherNamed = "set_preset_default_patcher_named"; //by default, when adding an instance to a set, make it so set presets for that instance save the latest preset, associated with that instance
	}

	//initialize the configuration system
	void init();

	void set_file_path(boost::filesystem::path p);
	boost::filesystem::path file_path();

	//read in the config file
	void read_file();

	//write the config file
	void write_file();

	//write out the config if dirty.
	//NOTE: you should call this periodically as it does debouncing to assure
	//that multiple fast updates don't write multiple times
	void write_if_dirty();

	//ns == namespace, optional to get to an object inside the object
	template <typename T>
	boost::optional<T> get(const std::string& key, boost::optional<std::string> ns = boost::none);

	//set a configuration value
	//ns == namespace, optional to set to an object inside the object
	template <typename T>
	void set(const T& value, const std::string& key, boost::optional<std::string> ns = boost::none);

	//make's an absolute path with ~ replaced with $HOME appropriately
	boost::filesystem::path make_path(const std::string& p);

	std::string get_system_id();
}
