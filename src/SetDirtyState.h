#pragma once

#include "RNBO.h"
#include "MidiConnectionUpdate.h"
#include <map>
#include <set>
#include <string>
#include <vector>

// Event-thread owned. Notifications invalidate components; only state differences are edits.
class SetDirtyState {
  public:
	using Json = RNBO::Json;
	using Components = std::map<std::string, Json>;
	void reset(Components baseline) {
		mBaseline = std::move(baseline);
		mChanged.clear();
	}
	void update(const std::string &key, const Json &value) {
		mLast[key] = value;
		auto it = mBaseline.find(key);
		if (it != mBaseline.end() && it->second == value) {
			mChanged.erase(key);
		} else {
			mChanged.insert(key);
		}
	}
	void remove(const std::string &key) {
		if (mBaseline.count(key)) {
			mChanged.insert(key);
		} else {
			mChanged.erase(key);
		}
	}
	void adopt(const std::string &key, Json value) {
		mBaseline[key] = std::move(value);
		mChanged.erase(key);
	}
	// Resolving an old alias refines the identity of an existing baseline cable;
	// it must not adopt unrelated live edits or clear other dirty components.
	void remapConnectionIdentities(const std::vector<MidiConnectionUpdate>& updates) {
		auto it = mBaseline.find("connections");
		if (it == mBaseline.end()) return;
		for (const auto& update : updates) {
			const auto before = Json(update.before).dump(), after = Json(update.after).dump();
			// An empty `before` is a restored cable the baseline left out at load.
			if (update.before != MidiConnectionUpdate::Key{}) {
				auto count = it->second.find(before);
				if (before == after || count == it->second.end()) continue;
				const auto remaining = count->get<unsigned>() - 1;
				if (remaining) *count = remaining; else it->second.erase(count);
			}
			it->second[after] = it->second.value(after, 0u) + 1;
		}
	}
	template <typename Connections> static Json connections(const Connections &connections) {
		Json edges = Json::object();
		std::set<std::string> seen;
		for (const auto &edge : connections) {
			Json key = {edge.source_name, edge.source_port_name, edge.sink_name, edge.sink_port_name};
			auto address = key;
			if constexpr (requires { edge.source_midi_port; edge.sink_midi_port; }) {
				if (!edge.source_midi_port.empty()) { address[0] = edge.source_midi_port; address[1] = ""; }
				if (!edge.sink_midi_port.empty()) { address[2] = edge.sink_midi_port; address[3] = ""; }
			}
			if (!seen.insert(address.dump()).second) continue;
			if constexpr (requires { edge.source_midi_identity; edge.sink_midi_identity; }) {
				if (!edge.source_midi_identity.empty()) { key[0] = edge.source_midi_identity; key[1] = ""; }
				if (!edge.sink_midi_identity.empty()) { key[2] = edge.sink_midi_identity; key[3] = ""; }
			}
			edges[key.dump()] = edges.value(key.dump(), 0u) + 1;
		}
		return edges;
	}
	bool dirty() const { return !mChanged.empty(); }
	// For the log: which components differ and, for connections, which cables.
	std::string describe() const {
		std::string result;
		for (const auto& key : mChanged) {
			result += (result.empty() ? "" : ", ") + key;
			auto base = mBaseline.find(key);
			auto last = mLast.find(key);
			if (key != "connections" || base == mBaseline.end() || last == mLast.end()) continue;
			for (const auto* side : {&base->second, &last->second}) {
				const auto& other = side == &base->second ? last->second : base->second;
				for (auto it = side->begin(); it != side->end(); ++it) {
					if (other.contains(it.key()) && (side != &base->second || other[it.key()] == it.value())) continue;
					result += " [" + it.key() + " saved " + std::to_string(base->second.value(it.key(), 0u)) +
						" live " + std::to_string(last->second.value(it.key(), 0u)) + "]";
				}
			}
		}
		return result;
	}
	static Json metadata(const std::string &text) {
		if (text.empty()) {
			return Json::object();
		}
		auto value = Json::parse(text, nullptr, false);
		// Set metadata historically permits opaque strings, too.
		return value.is_discarded() ? Json(text) : value;
	}
	static Json device(Json config) {
		config.erase("preset_last");
		config.erase("datarefs"); // Buffer contents belong to presets; metaoverride/datarefs does not.
		if (config.contains("metaoverride")) {
			auto &meta = config["metaoverride"];
			if (meta.contains("params") && meta["params"].is_array()) {
				Json params = Json::object();
				for (auto &entry : meta["params"])
					params[std::to_string(entry["index"].get<unsigned int>())] = entry["meta"];
				meta["params"] = std::move(params);
			}
		}
		return config;
	}

  private:
	Components mBaseline;
	Components mLast; // last compared values, for describe()
	std::set<std::string> mChanged;
};
