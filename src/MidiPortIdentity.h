#pragma once

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace midi_identity {
// May also be supplied by MIDI bridges that know the Bluetooth identity or
// native endpoint ID but cannot express it in JACK's display names.
inline constexpr const char* metadata_key = "http://cycling74.com/ns/rnbo/midi-device-id";

inline std::string read(const std::filesystem::path& path) {
	std::ifstream stream(path);
	std::string value;
	std::getline(stream, value);
	return value;
}

inline std::string usbCard(const std::string& card, const std::filesystem::path& sys = "/sys") {
	std::error_code ec;
	const auto root = std::filesystem::canonical(sys, ec);
	if (ec) return {};
	auto path = std::filesystem::canonical(root / "class/sound" / ("card" + card) / "device", ec);
	if (ec) return {};
	for (; !path.empty() && path != path.root_path(); path = path.parent_path()) {
		auto vendor = read(path / "idVendor"), product = read(path / "idProduct");
		if (vendor.empty() || product.empty()) continue;
		auto serial = read(path / "serial");
		// A real USB serial, not udev's ID_SERIAL (which can be just a model name).
		if (!serial.empty()) return "usb:" + vendor + ":" + product + ":serial:" + serial;
		return "usb:" + vendor + ":" + product + ":path:" + path.lexically_relative(root).string();
	}
	return {};
}

inline std::string alias(const std::string& name, const std::vector<std::string>& aliases) {
	std::string result = name;
	for (const auto& a : aliases)
		if (!a.starts_with("alsa_pcm") || aliases.size() == 1) result = a;
	return result;
}

inline std::string normalized(const std::string& alias) {
	static const std::regex raw(R"(^(in|out)-hw-\d+-(\d+)-(\d+)-(.*)$)");
	std::smatch match;
	if (std::regex_match(alias, match, raw))
		return match[1].str() + "-hw-*-" + match[2].str() + "-" + match[3].str() + "-" + match[4].str();
	return alias;
}

inline bool legacyAlias(const std::string& name) {
	static const std::regex raw(R"(^(in|out)-hw-\d+-\d+-\d+-.+$)");
	static const std::regex seq(R"(^.+(?::midi/(capture|playback)_\d+|/midi_(capture|playback)_\d+)$)");
	// JACK2's seq backend also exposes "client:port (out)" / "(in)".
	static const std::regex seqNamed(R"(^.+:.+ \((in|out)\)$)");
	return std::regex_match(name, raw) || std::regex_match(name, seq) ||
		std::regex_match(name, seqNamed) || name.starts_with("alsa_midi:");
}

inline bool instanceAlias(const std::string& name) {
	return name.find(":rnbomidi") != std::string::npos;
}

inline bool pathIdentity(const std::string& identity) {
	return identity.starts_with("usb:") && identity.find(":path:") != std::string::npos;
}

inline std::string get(const std::string& name, const std::vector<std::string>& aliases,
		bool physical, bool source, const std::string& metadata = {},
		const std::filesystem::path& sys = "/sys") {
	// These aliases describe RNBO application ports, not external devices. Their
	// client names are rewritten from instance indices when a patcher is renamed.
	for (const auto& a : aliases) if (instanceAlias(a)) return {};
	const std::string direction = source ? ":source" : ":sink";
	if (!metadata.empty()) return "bridge:" + metadata + direction;
	// Sequencer bridges may omit the physical flag for Bluetooth/software MIDI.
	// Their aliases still identify MIDI endpoints; anonymous application ports
	// are tracked by their exact JACK names by the recovery graph.
	if (!physical && aliases.empty() && !name.starts_with("system:midi_")) return {};
	static const std::regex raw(R"(^(in|out)-hw-(\d+)-(\d+)-(\d+)-(.*)$)");
	for (const auto& a : aliases) {
		std::smatch match;
		if (std::regex_match(a, match, raw)) {
			auto usb = usbCard(match[2].str(), sys);
			if (!usb.empty())
				return usb + ":midi:" + match[3].str() + ":" + match[4].str() + direction;
		}
	}
	// Registration often arrives before a backend sets its aliases. Never bind
	// to system:midi_capture_N: that number can later belong to a different device.
	if (aliases.empty() && name.starts_with("system:midi_")) return {};
	return "name:" + normalized(alias(name, aliases)) + direction;
}
}
