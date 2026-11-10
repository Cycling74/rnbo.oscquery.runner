#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "MidiPortIdentity.h"
#include "MidiConnectionUpdate.h"

inline const std::string CONTROL_CLIENT_NAME("rnbo-control");

// Event-thread MIDI graph history. Pending cables are recovery state, never the
// live save graph. JACK notification timestamps distinguish removal from edits.
class MidiConnections {
public:
	struct Endpoint {
		std::string name;
		std::string identity;
		std::string alias; // preserves a device alias when JACK renumbers the port
		bool operator==(const Endpoint&) const = default;
	};
	struct Port {
		uint32_t id;
		Endpoint endpoint;
		bool source;
		std::vector<std::string> aliases;
		bool physical = false;
	};
	struct Edge {
		Endpoint source, sink;
		// Runtime-only removal provenance; load requests have no exclusions.
		std::optional<uint32_t> sourcePort, sinkPort;
		std::set<uint32_t> sourceExisting, sinkExisting;
		// Runtime-only: identities a live cable was requested under (set load or
		// recovery). Empty for cables that were merely observed.
		std::string sourceRequested, sinkRequested;
		// A waiting end whose port id was taken from the one exact device present.
		bool sourceAdopted = false, sinkAdopted = false;
		// Runtime-only: a control cable kept across a set switch or a save that
		// the current set does not contain. It is in the baseline exactly while it
		// is live: it joins when restored and leaves when its device goes away,
		// so it never reads as an edit.
		bool carried = false;
		// Runtime-only: a live cable that came from a request (load, recovery, a
		// satisfied waiting cable) or was committed by a save, as opposed to one
		// that was merely observed. It never takes another request.
		bool owned = false;
		bool operator==(const Edge&) const = default;
	};
	using Pair = std::pair<uint32_t, uint32_t>;
	using Clock = std::chrono::steady_clock;
	using Time = Clock::time_point;
	using Selection = std::set<uint32_t>;
	static constexpr auto disconnectWindow = std::chrono::milliseconds(250);

	static bool same(const Endpoint& a, const Endpoint& b) {
		return a.identity.empty() && b.identity.empty() ? a.name == b.name
			: !a.identity.empty() && a.identity == b.identity;
	}
	static bool same(const Edge& a, const Edge& b) {
		return same(a.source, b.source) && same(a.sink, b.sink);
	}
	static bool control(const Edge& edge) {
		return edge.source.name.starts_with(CONTROL_CLIENT_NAME + ":") ||
			edge.sink.name.starts_with(CONTROL_CLIENT_NAME + ":");
	}
	// The identity a cable was asked for. A partially resolved load rewrites its
	// present endpoint to the live port, which must not become the claim.
	static std::string requestedIdentity(const Edge& edge, bool source) {
		const auto& requested = source ? edge.sourceRequested : edge.sinkRequested;
		return !requested.empty() ? requested : (source ? edge.source : edge.sink).identity;
	}
	// A load request that could not be connected yet: keep what was asked for.
	// `retained`: the caller rebuilds a graph it had and keeps its baseline, so a
	// carried cable it asks for again stays carried.
	void defer(Edge edge, const Edge& requested, bool retained = false) {
		edge.sourceRequested = requested.source.identity;
		edge.sinkRequested = requested.sink.identity;
		if (supersede(edge) && retained) edge.carried = true;
		remember(edge, true);
	}
	// An explicit set load connected this cable: the set contains it.
	void adopt(Pair ids) {
		auto live = mLive.find(ids);
		if (live != mLive.end()) live->second.carried = false;
	}
	// A set load asks for this cable itself: a carried copy of it is superseded,
	// or one cable would be restored, and counted, twice. JACK names are only
	// hints (a saved one can be stale), so the copy is found by identity.
	std::optional<Edge> supersede(const Edge& edge) {
		for (const bool named : {true, false}) {
			auto copy = std::find_if(mPending.begin(), mPending.end(), [&](const Edge& e) {
				return e.carried && (named ? exact(e, edge) : same(e, edge));
			});
			if (copy == mPending.end()) continue;
			auto result = *copy;
			mPending.erase(copy);
			return result;
		}
		return {};
	}
	void remember(const Edge& edge, bool distinct = false) {
		// Different JACK names with the same identity are different cables.
		if (distinct || std::none_of(mPending.begin(), mPending.end(), [&](const Edge& e) { return exact(e, edge); }))
			mPending.push_back(edge);
	}
	void connected(Pair ids, bool satisfyPending = true, const Edge* request = nullptr) {
		auto a = mPorts.find(ids.first), b = mPorts.find(ids.second);
		if (a == mPorts.end() || b == mPorts.end() || mForgotten.count(ids.first) || mForgotten.count(ids.second)) return;
		Edge edge{a->second.endpoint, b->second.endpoint, ids.first, ids.second};
		// Observation re-runs for every live cable; it must keep what it was requested under.
		const auto live = mLive.find(ids);
		if (live != mLive.end()) edge.carried = live->second.carried;
		// A port of a live cable can be identified, or refined, later on.
		std::optional<MidiConnectionUpdate> refined;
		if (live != mLive.end() && key(live->second) != key(edge)) refined = MidiConnectionUpdate{key(live->second), key(edge)};
		if (request) {
			edge.sourceRequested = requestedIdentity(*request, true);
			edge.sinkRequested = requestedIdentity(*request, false);
			edge.owned = true;
			edge.carried = edge.carried || request->carried;
		} else if (live != mLive.end()) {
			edge.sourceRequested = live->second.sourceRequested;
			edge.sinkRequested = live->second.sinkRequested;
			edge.owned = live->second.owned;
		}
		mLive[ids] = edge;
		mDisconnectDeadlines.erase(ids);
		// An owned cable that flaps inside the disconnect window is still itself.
		if (mActual.insert(ids).second && satisfyPending && !edge.owned) {
			// A newly connected cable can satisfy one pending request. An already
			// connected twin must never consume the missing twin's request.
			for (auto it = mPending.begin(); it != mPending.end(); ++it) {
				if (it->sourceExisting.count(ids.first) || it->sinkExisting.count(ids.second)) continue;
				const auto sources = candidates(it->source, true), sinks = candidates(it->sink, false);
				if (std::any_of(sources.begin(), sources.end(), [&](const Port* p) { return p->id == ids.first; }) &&
					std::any_of(sinks.begin(), sinks.end(), [&](const Port* p) { return p->id == ids.second; })) {
					const auto request = *it;
					mPending.erase(it);
					mLive[ids].sourceRequested = requestedIdentity(request, true);
					mLive[ids].sinkRequested = requestedIdentity(request, false);
					mLive[ids].owned = true;
					mLive[ids].carried = request.carried;
					resolved(request, edge, ids);
					return;
				}
			}
		}
		// The baseline follows a cable it already has to its new identity. (A
		// cable that just satisfied a request is accounted for by that request.)
		if (refined) mIdentityUpdates.push_back(*refined);
	}
	void disconnected(Pair ports, Time when = Clock::now()) {
		mActual.erase(ports);
		if (mLive.count(ports)) mDisconnectDeadlines.try_emplace(ports, when + disconnectWindow);
	}
	void registered(uint32_t id) {
		mRegistered.insert(id);
		// Registration, including reuse of a JACK port ID, starts a new lifetime.
		for (auto& edge : mPending) {
			edge.sourceExisting.erase(id);
			edge.sinkExisting.erase(id);
		}
	}
	void removed(uint32_t id, Time when = Clock::now()) {
		auto lost = [&](Edge& edge) {
			if (edge.sourcePort == id) { edge.sourceExisting = mRegistered; edge.sourcePort.reset(); edge.sourceAdopted = false; }
			if (edge.sinkPort == id) { edge.sinkExisting = mRegistered; edge.sinkPort.reset(); edge.sinkAdopted = false; }
			// The removed ID itself is no longer a live candidate. It may be
			// reused on replug, even if a snapshot wins the registration callback.
			edge.sourceExisting.erase(id);
			edge.sinkExisting.erase(id);
		};
		// A waiting cable whose only candidate right now is the port that is
		// leaving was waiting on that device: ports that stay must not replace it.
		// Decided here, not remembered, so a stand-in never outranks the device.
		for (auto& edge : mPending) for (const bool source : {true, false}) {
			if (source ? edge.sourcePort : edge.sinkPort) continue;
			auto& existing = source ? edge.sourceExisting : edge.sinkExisting;
			const auto& endpoint = source ? edge.source : edge.sink;
			auto present = candidates(endpoint, source);
			std::erase_if(present, [&](const Port* p) { return existing.count(p->id) != 0; });
			if (present.size() != 1 || present.front()->id != id) continue;
			// The exact device: nothing registered now may replace it. An alias
			// stand-in: a port that registered after the last snapshot was not
			// part of this judgement and may be the device itself.
			const bool exact = present.front()->endpoint.identity == endpoint.identity;
			for (const auto seen : mRegistered) if (seen != id && (exact || mPorts.count(seen))) existing.insert(seen);
		}
		for (auto& edge : mPending) lost(edge);
		for (auto it = mLive.begin(); it != mLive.end();) {
			if (it->first.first == id || it->first.second == id) {
				auto deadline = mDisconnectDeadlines.find(it->first);
				if (deadline == mDisconnectDeadlines.end() || when < deadline->second) {
					// A carried cable leaves the baseline with its device.
					if (it->second.carried) mIdentityUpdates.push_back({key(it->second), {}});
					lost(it->second);
					it->second.sourceRequested.clear();
					it->second.sinkRequested.clear();
					it->second.owned = false;
					remember(it->second);
				}
				mDisconnectDeadlines.erase(it->first);
				it = mLive.erase(it);
			} else ++it;
		}
		std::erase_if(mActual, [id](Pair pair) { return pair.first == id || pair.second == id; });
		mRegistered.erase(id);
		mPorts.erase(id);
		mForgotten.erase(id);
		// The next snapshot rebuilds the lookup indexes; stale IDs are ignored.
	}
	void observe(const std::vector<Port>& ports, const std::vector<Pair>& connections, Time now = Clock::now()) {
		auto previousPorts = std::move(mPorts);
		mPorts.clear();
		for (auto& index : mByIdentity) index.clear();
		for (auto& index : mByName) index.clear();
		std::set<uint32_t> identified;
		for (auto port : ports) {
			auto previous = previousPorts.find(port.id);
			if (previous != previousPorts.end() && previous->second.endpoint.name == port.endpoint.name &&
				previous->second.endpoint.identity.starts_with("usb:")) port.endpoint.identity = previous->second.endpoint.identity;
			// A backend can name a device after its port is registered, and after
			// auto-connect has already cabled it.
			if (previous != previousPorts.end() && (previous->second.endpoint.identity != port.endpoint.identity ||
				previous->second.aliases != port.aliases)) identified.insert(port.id);
			if (port.endpoint.alias.empty() && !port.aliases.empty())
				port.endpoint.alias = midi_identity::alias(port.endpoint.name, port.aliases);
			mPorts.emplace(port.id, port);
			mRegistered.insert(port.id);
			if (!port.endpoint.identity.empty()) mByIdentity[port.source][port.endpoint.identity].push_back(port.id);
			std::set<std::string> names{midi_identity::normalized(port.endpoint.name)};
			for (const auto& alias : port.aliases) names.insert(midi_identity::normalized(alias));
			for (const auto& name : names) mByName[port.source][name].push_back(port.id);
		}
		// Index vectors preserve jack_get_ports order, independent of port IDs.
		std::set<Pair> actual(connections.begin(), connections.end());
		std::erase_if(mActual, [&](Pair pair) { return !actual.count(pair); });
		// Cables of a port that has only now been identified count as new, so they
		// can satisfy the request that was waiting for that device. A cable that
		// already came from a request never takes a second one.
		std::erase_if(mActual, [&](Pair pair) {
			if (!identified.count(pair.first) && !identified.count(pair.second)) return false;
			auto live = mLive.find(pair);
			return live == mLive.end() || !live->second.owned;
		});
		for (const auto& ids : connections) connected(ids);
		std::erase_if(mDisconnectDeadlines, [&](const auto& entry) {
			const auto ids = entry.first;
			if (!mLive.count(ids)) return true;
			if (now >= entry.second && mPorts.count(ids.first) && mPorts.count(ids.second)) {
				leftBaseline(mLive.at(ids));
				mLive.erase(ids);
				return true;
			}
			return false;
		});
		// Tie a waiting end to its exact device when that is the only one present,
		// so its removal is recognized even if a snapshot misses the port first.
		// Alias stand-ins are never adopted. Devices with one identity cannot be
		// told apart: the first one seen is taken to be the device and keeps the
		// cable when an identical one shows up later.
		for (auto& edge : mPending) for (const bool source : {true, false}) {
			auto& port = source ? edge.sourcePort : edge.sinkPort;
			auto& adopted = source ? edge.sourceAdopted : edge.sinkAdopted;
			if (port) continue;
			const auto& endpoint = source ? edge.source : edge.sink;
			const auto& existing = source ? edge.sourceExisting : edge.sinkExisting;
			const auto identity = endpoint.identity;
			auto present = candidates(endpoint, source);
			std::erase_if(present, [&](const Port* p) { return existing.count(p->id) || p->endpoint.identity != identity; });
			if (!port && present.size() == 1) { port = present.front()->id; adopted = true; }
		}
	}
	std::vector<const Port*> candidates(const Endpoint& stored, bool source) const {
		const auto& endpoint = stored;
		auto lookup = [&](const auto& index, const std::string& key) {
			std::vector<const Port*> result;
			auto it = index.find(key);
			if (it != index.end()) for (const auto id : it->second) {
				auto port = mPorts.find(id);
				if (port != mPorts.end() && !mForgotten.count(id)) result.push_back(&port->second);
			}
			return result;
		};
		bool pathFallback = false;
		if (!endpoint.identity.empty() && !endpoint.identity.starts_with("legacy:")) {
			auto result = lookup(mByIdentity[source], endpoint.identity);
			if (!result.empty() || !midi_identity::pathIdentity(endpoint.identity)) return result;
			pathFallback = true;
		}
		const auto wanted = endpoint.identity.starts_with("legacy:") ? endpoint.identity.substr(7) :
			midi_identity::normalized(endpoint.alias.empty() ? endpoint.name : endpoint.alias);
		auto result = lookup(mByName[source], wanted);
		if (pathFallback && !result.empty()) {
			// A serial-less device already sitting in another cable's socket belongs
			// to that cable; only unclaimed devices may stand in for this one.
			const auto claimed = claims(source, endpoint.identity);
			std::erase_if(result, [&](const Port* port) {
				return midi_identity::pathIdentity(port->endpoint.identity) && claimed.count(port->endpoint.identity);
			});
		}
		return result;
	}
	// Path identities of every saved endpoint of a set that is being loaded,
	// including edges not processed yet. Must not outlive the load.
	void claim(const std::vector<Edge>& edges) {
		for (const auto& edge : edges) {
			mClaims[1].insert(edge.source.identity);
			mClaims[0].insert(edge.sink.identity);
		}
	}
	void releaseClaims() { mClaims[0].clear(); mClaims[1].clear(); }
	const Port* resolve(const Endpoint& endpoint, bool source, const Selection& taken = {}) const {
		auto ports = candidates(endpoint, source);
		if (ports.empty()) return nullptr;
		const auto wanted = endpoint.name;
		for (const auto* port : ports) if (port->endpoint.name == wanted) return port;
		for (const auto* port : ports) if (!taken.count(port->id)) return port;
		return ports.front();
	}
	std::pair<const Port*, const Port*> resolveRecovery(const Edge& edge, const Selection& taken = {}) const {
		const auto sources = candidates(edge.source, true), sinks = candidates(edge.sink, false);
		std::pair<const Port*, const Port*> best{};
		std::tuple<bool, bool, bool, bool> bestRank;
		const auto sourceName = edge.source.name, sinkName = edge.sink.name;
		for (const auto* source : sources) for (const auto* sink : sinks) {
			// Hotplug recovery must not move a lost cable to a twin that stayed
			// online. Set-load requests retain the ordinary matching rules.
			if (edge.sourceExisting.count(source->id) || edge.sinkExisting.count(sink->id)) continue;
			// JACK cannot carry two copies of the same cable. Leave that request
			// pending until another twin appears, rather than consuming it on EEXIST.
			if (mActual.count({source->id, sink->id})) continue;
			const auto rank = std::make_tuple(source->endpoint.name != sourceName, sink->endpoint.name != sinkName,
				taken.count(source->id) != 0, taken.count(sink->id) != 0);
			if (!best.first || rank < bestRank) { best = {source, sink}; bestRank = rank; }
		}
		return best;
	}
	Selection occupied() const {
		Selection result;
		for (const auto& [source, sink] : mActual) { result.insert(source); result.insert(sink); }
		return result;
	}
	void forget(const Edge& edge) {
		auto it = std::find_if(mPending.begin(), mPending.end(), [&](const Edge& e) { return exact(e, edge); });
		if (it != mPending.end()) mPending.erase(it);
	}
	void forgetNames(const std::string& source, const std::string& sink) {
		auto match = [&](const Edge& e) { return matchesName(e.source, source, true) && matchesName(e.sink, sink, false); };
		std::erase_if(mPending, [&](const Edge& edge) {
			auto named = [](const Endpoint& endpoint, const std::string& name) {
				return endpoint.name == name || (!endpoint.alias.empty() && endpoint.alias == name);
			};
			if (named(edge.source, source) && named(edge.sink, sink)) return true;
			const auto [a, b] = resolveRecovery(edge);
			return a && b && match({a->endpoint, b->endpoint});
		});
		std::erase_if(mLive, [&](const auto& p) { return match(p.second) && leftBaseline(p.second); });
	}
	void edit(const std::string& port, bool source, const std::set<std::string>& peers) {
		auto omitted = [&](const Edge& edge, bool pending) {
			const auto& endpoint = source ? edge.source : edge.sink;
			const auto& peer = source ? edge.sink : edge.source;
			// A connected twin is not evidence that this pending peer is present.
			const auto recovery = pending ? resolveRecovery(edge) : std::pair<const Port*, const Port*>{};
			const auto* present = pending ? (source ? recovery.second : recovery.first) : resolve(peer, !source);
			return matchesName(endpoint, port, source) && present &&
				std::none_of(peers.begin(), peers.end(), [&](const auto& name) { return matchesName(peer, name, !source); });
		};
		std::erase_if(mPending, [&](const Edge& edge) { return omitted(edge, true); });
		std::erase_if(mLive, [&](const auto& entry) { return omitted(entry.second, false) && leftBaseline(entry.second); });
	}
	void forgetClient(const std::string& name) {
		for (const auto& [id, port] : mPorts) if (port.endpoint.name.starts_with(name + ":")) mForgotten.insert(id);
		auto match = [&](const Edge& e) { return e.source.name.starts_with(name + ":") || e.sink.name.starts_with(name + ":"); };
		std::erase_if(mPending, match);
		std::erase_if(mLive, [&](const auto& p) { return match(p.second); });
	}
	void saved(const std::vector<Edge>& snapshot, bool preserveControl = true) {
		auto remaining = snapshot;
		auto stored = [&](const Edge& edge) {
			auto it = std::find_if(remaining.begin(), remaining.end(), [&](const Edge& saved) { return same(edge, saved); });
			if (it == remaining.end()) return false;
			remaining.erase(it);
			return true;
		};
		// The saved snapshot becomes the caller's baseline: carried cables are
		// accounted for anew below. Identity refinements stay; one the snapshot
		// already reflects finds nothing to change.
		std::erase_if(mIdentityUpdates, [](const MidiConnectionUpdate& update) {
			return update.before == MidiConnectionUpdate::Key{} || update.after == MidiConnectionUpdate::Key{};
		});
		// Match live cables first so a surviving twin cannot preserve an absent
		// twin's cable when the saved snapshot contained only one of them.
		std::erase_if(mLive, [&](auto& p) {
			const bool inSnapshot = stored(p.second);
			if (!inSnapshot && !(preserveControl && control(p.second))) return true;
			// The save stored the current identities; older requests are obsolete.
			p.second.sourceRequested = p.second.source.identity;
			p.second.sinkRequested = p.second.sink.identity;
			p.second.owned = true;
			if (inSnapshot) p.second.carried = false; // the saved set contains it now
			// Restored after the snapshot was taken: still outside the saved set.
			else if (p.second.carried && mActual.count(p.first)) mIdentityUpdates.push_back({{}, key(p.second)});
			return false;
		});
		std::erase_if(mPending, [&](Edge& edge) {
			if (!(preserveControl && control(edge))) return !stored(edge);
			// Kept either way; one the snapshot lacks is not part of the saved set.
			edge.carried = !stored(edge);
			return false;
		});
	}
	// `carry`: the caller replaces the baseline, so kept cables are not part of it.
	// A caller that keeps its baseline reloads the graph it had; carried cables
	// were never in that graph and always stay.
	void clear(bool preserveControl = false, bool carry = true) {
		if (carry) {
			auto discard = [&](const Edge& edge) { return !(preserveControl && control(edge)); };
			std::erase_if(mPending, discard);
			std::erase_if(mLive, [&](const auto& p) { return discard(p.second); });
			// What is kept is not the new set's, unless the load then asks for it
			// (see supersede() and adopt()).
			for (auto& edge : mPending) edge.carried = true;
			for (auto& entry : mLive) entry.second.carried = true;
			mIdentityUpdates.clear();
		} else {
			// The caller's snapshot brings back every waiting cable except the
			// carried ones, and reasserts the live cables; live control cables stay
			// known here so the carried ones among them keep their standing.
			std::erase_if(mPending, [](const Edge& edge) { return !edge.carried; });
			std::erase_if(mLive, [](const auto& p) { return !control(p.second); });
		}
		std::erase_if(mDisconnectDeadlines, [&](const auto& p) { return !mLive.count(p.first); });
	}
	// The JACK client is going away and every port with it. The caller restores
	// the graph it knows from its own snapshot; carried cables are in no snapshot
	// and keep waiting here.
	void restart() {
		// Updates still queued describe changes the caller's kept baseline has
		// not seen yet; they stay.
		auto ids = mRegistered;
		for (const auto& entry : mPorts) ids.insert(entry.first);
		for (const auto id : ids) removed(id); // live carried cables leave the baseline
		std::erase_if(mPending, [](const Edge& edge) { return !edge.carried; });
		mDisconnectDeadlines.clear();
		releaseClaims();
	}
	static MidiConnectionUpdate::Key key(const Edge& edge) {
		MidiConnectionUpdate::Key result;
		const Endpoint* endpoints[] = {&edge.source, &edge.sink};
		for (int i = 0; i < 2; ++i) {
			const auto& endpoint = *endpoints[i];
			if (!endpoint.identity.empty()) { result[i * 2] = endpoint.identity; continue; }
			auto pos = endpoint.name.find(':');
			result[i * 2] = endpoint.name.substr(0, pos);
			if (pos != std::string::npos) result[i * 2 + 1] = endpoint.name.substr(pos + 1);
		}
		return result;
	}
	// Call after `old` has left the pending list. `ports` are the ports it resolved to.
	void resolved(const Edge& old, const Edge& current, std::optional<Pair> ports = {}) {
		auto before = key(old), after = key(current);
		// A carried cable is not in the baseline: an empty `before` adds it.
		if (old.carried) mIdentityUpdates.push_back({{}, after});
		else if (before != after) mIdentityUpdates.push_back({before, after});
		// A serial-less device found in another socket takes all of its cables
		// along: the ones still waiting now look for it where it is.
		for (const bool source : {true, false}) {
			const auto was = (source ? old.source : old.sink).identity;
			const auto& now = source ? current.source : current.sink;
			if (was == now.identity || !midi_identity::pathIdentity(was) || !midi_identity::pathIdentity(now.identity)) continue;
			for (auto& edge : mPending) {
				auto& endpoint = source ? edge.source : edge.sink;
				if (endpoint.identity != was) continue;
				// Never move onto a port this cable already excludes.
				if (ports && (source ? edge.sourceExisting : edge.sinkExisting).count(source ? ports->first : ports->second)) continue;
				const auto previous = key(edge);
				if (source ? edge.sourceAdopted : edge.sinkAdopted) {
					(source ? edge.sourcePort : edge.sinkPort).reset();
					(source ? edge.sourceAdopted : edge.sinkAdopted) = false;
				}
				endpoint = now;
				if (previous != key(edge)) mIdentityUpdates.push_back({previous, key(edge)});
			}
		}
	}
	std::vector<MidiConnectionUpdate> takeIdentityUpdates() {
		auto updates = std::move(mIdentityUpdates);
		mIdentityUpdates.clear();
		return updates;
	}
	const std::vector<Edge>& pending() const { return mPending; }
	bool isConnected(Pair ids) const { return mActual.count(ids) != 0; }
	// JACK port names of the live cables that are carried.
	std::set<std::pair<std::string, std::string>> carriedCables() const {
		std::set<std::pair<std::string, std::string>> result;
		for (const auto& entry : mLive) if (entry.second.carried && mActual.count(entry.first))
			result.emplace(entry.second.source.name, entry.second.sink.name);
		return result;
	}
	const std::map<uint32_t, Port>& ports() const { return mPorts; }
	std::string savedIdentity(const Port& port) const { return port.endpoint.identity; }
private:
	// Path identities (sockets) that belong to a cable of another device. Pending
	// cables claim where their device was last known; live cables claim where
	// they are, but only when they came from a saved or recovered request rather
	// than being merely observed. A cable that is looking for `wanted`, or was
	// requested under it, is a cable of the same device and claims nothing here.
	std::set<std::string> claims(bool source, const std::string& wanted) const {
		std::set<std::string> result;
		auto add = [&](const std::string& identity) { if (midi_identity::pathIdentity(identity)) result.insert(identity); };
		for (const auto& identity : mClaims[source]) add(identity);
		for (const auto& edge : mPending) {
			const auto identity = (source ? edge.source : edge.sink).identity;
			if (identity != wanted && (source ? edge.sourceRequested : edge.sinkRequested) != wanted) add(identity);
		}
		for (const auto& [ids, edge] : mLive) {
			const auto& requested = source ? edge.sourceRequested : edge.sinkRequested;
			const auto& identity = (source ? edge.source : edge.sink).identity;
			if (!requested.empty() && requested != wanted && identity != wanted) add(identity);
		}
		return result;
	}
	// A carried cable is in the baseline exactly while it is live, whatever
	// ends it. Always true, so it can sit in an erase condition.
	bool leftBaseline(const Edge& edge) {
		if (edge.carried) mIdentityUpdates.push_back({key(edge), {}});
		return true;
	}
	static bool exact(const Edge& a, const Edge& b) {
		return same(a, b) && a.source.name == b.source.name && a.sink.name == b.sink.name;
	}
	bool matchesName(const Endpoint& endpoint, const std::string& name, bool source) const {
		if (endpoint.name == name || (!endpoint.alias.empty() && endpoint.alias == name)) return true;
		const auto* port = resolve(endpoint, source);
		return port && (port->endpoint.name == name || std::find(port->aliases.begin(), port->aliases.end(), name) != port->aliases.end());
	}
	using Index = std::map<std::string, std::vector<uint32_t>>;
	Index mByIdentity[2], mByName[2];
	std::map<uint32_t, Port> mPorts;
	std::map<Pair, Edge> mLive;
	std::set<Pair> mActual;
	std::map<Pair, Time> mDisconnectDeadlines;
	std::vector<Edge> mPending;
	std::vector<MidiConnectionUpdate> mIdentityUpdates;
	std::set<uint32_t> mForgotten;
	std::set<uint32_t> mRegistered;
	std::set<std::string> mClaims[2]; // indexed by source
};
