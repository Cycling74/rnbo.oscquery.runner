#pragma once

#include "RNBO.h"
#include <map>
#include <set>
#include <string>

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
	template <typename Connections> static Json connections(const Connections &connections) {
		Json edges = Json::object();
		for (const auto &edge : connections) {
			Json key = {edge.source_name, edge.source_port_name, edge.sink_name, edge.sink_port_name};
			edges[key.dump()] = true;
		}
		return edges;
	}
	bool dirty() const { return !mChanged.empty(); }
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
	std::set<std::string> mChanged;
};
