#pragma once

#include "MidiConnections.h"

// Candidates are per port incarnation, so late aliases can be retried and each
// of two identical devices is its own candidate. A candidate that is handled is
// never reasserted, so a cable the user removes stays off while the port lives.
// remember = false (standalone instances): nothing outlives the incarnation; a
// re-registered port is eligible again.
// remember = true (rnbo-control): a returning unit is left to the recovery
// graph, which also records deliberate disconnects. mSeen counts units: the most
// handled candidates with one key present at once. A candidate is returning when
// fewer of its twins are present than that, otherwise it is a new unit.
// Limit: a device first seen under a provisional key that it shares with other
// devices cannot be told from a new one until its identity arrives, and is then
// auto-connected like a new device.
class MidiAutoConnect {
public:
	explicit MidiAutoConnect(bool remember = true) : mRemember(remember) {}
	void portChanged(uint32_t id) { mCandidates.erase(id); }
	std::string cachedUsbIdentity(uint32_t id, const std::string& name) const {
		auto it = mCandidates.find(id);
		return it != mCandidates.end() && it->second.name == name ? it->second.usbIdentity : "";
	}
	template<class Connect> void attempt(const MidiConnections::Port& port, Connect connect,
			MidiConnections::Time now = MidiConnections::Clock::now()) {
		const auto key = port.endpoint.identity.empty() ? port.endpoint.name : port.endpoint.identity;
		auto [it, inserted] = mCandidates.try_emplace(port.id);
		auto& candidate = it->second;
		if (inserted || candidate.name != port.endpoint.name) candidate = {port.endpoint.name, {}, {}, now, false, {}};
		if (candidate.key != key || candidate.aliases != port.aliases) {
			// A handled candidate is one unit; a late identity moves that unit to
			// the final key, so the provisional key no longer suppresses others.
			if (mRemember && candidate.handled && candidate.key != key) {
				auto seen = mSeen.find(candidate.key);
				if (seen != mSeen.end() && seen->second > 0) --seen->second;
			}
			candidate.key = key;
			candidate.aliases = port.aliases;
			candidate.changed = now;
		}
		candidate.usbIdentity = port.endpoint.identity.starts_with("usb:") ? port.endpoint.identity : "";
		if (candidate.handled) {
			// Metadata/aliases can complete after the original connection. Carry
			// the suppression forward without making a second connection.
			if (mRemember) noteHandled(key);
			return;
		}
		if (mRemember) {
			size_t present = 0;
			for (const auto& [id, other] : mCandidates)
				if (id != port.id && other.handled && other.key == key) ++present;
			auto seen = mSeen.find(key);
			if (seen != mSeen.end() && present < seen->second) { candidate.handled = true; return; }
		}
		if (port.endpoint.name.starts_with("system:midi_") &&
			(port.aliases.empty() || now - candidate.changed < std::chrono::milliseconds(50))) return;
		if (connect()) { candidate.handled = true; if (mRemember) noteHandled(key); }
	}
private:
	void noteHandled(const std::string& key) {
		size_t present = 0;
		for (const auto& [id, candidate] : mCandidates) if (candidate.handled && candidate.key == key) ++present;
		auto& seen = mSeen[key];
		seen = std::max(seen, present);
	}
	struct Candidate {
		std::string name, key;
		std::vector<std::string> aliases;
		MidiConnections::Time changed;
		bool handled = false; // connected, or left to recovery
		std::string usbIdentity;
	};
	std::map<uint32_t, Candidate> mCandidates;
	std::map<std::string, size_t> mSeen;
	bool mRemember;
};
