#include "DB.h"
#include "Config.h"

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/none.hpp>

//https://srombauts.github.io/SQLiteCpp/

namespace fs = boost::filesystem;

namespace {
	const std::string rnbo_version(RNBO_VERSION);
}

DB::DB() : mDB(config::get<fs::path>(config::key::DBPath).get().string(), SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE) {
	//do migrations
	mDB.exec(
			R"(CREATE TABLE IF NOT EXISTS migrations (
					id INTEGER PRIMARY KEY,
					rnbo_version TEXT NOT NULL,
					created_at REAL DEFAULT (datetime('now', 'localtime'))
				))"
			);

	//create initial version entry, if it doesn't already exist
	{
		SQLite::Statement query(mDB, "INSERT OR IGNORE INTO migrations (id, rnbo_version) VALUES (1, ?)");
		query.bind(1, rnbo_version);
		query.exec();
	}

	int curversion = 0;
	{
		SQLite::Statement query(mDB, "SELECT MAX(id) FROM migrations");
		if (!query.executeStep()) {
			throw new std::runtime_error("failed to get result from migrations");
		}
		//migrate
		curversion = query.getColumn(0);
	}

	auto do_migration = [curversion, this](int version, std::function<void(SQLite::Database&)> func) {
		if (curversion < version) {
			func(mDB);
			SQLite::Statement query(mDB, "INSERT INTO migrations (id, rnbo_version) VALUES (?, ?)");
			query.bind(1, version);
			query.bind(2, rnbo_version);
			query.exec();
		}
	};

	do_migration(2, [](SQLite::Database& db) {
			db.exec(R"(CREATE TABLE IF NOT EXISTS patchers (
					id INTEGER PRIMARY KEY AUTOINCREMENT,
					name TEXT NOT NULL,
					so_path TEXT NOT NULL,
					config_path TEXT,
					runner_rnbo_version TEXT NOT NULL,
					max_rnbo_version TEXT NOT NULL,
					created_at REAL DEFAULT (datetime('now', 'localtime'))
			)
			)"
			);
	});
	do_migration(3, [](SQLite::Database& db) {
			db.exec("CREATE INDEX patcher_version ON patchers(runner_rnbo_version)");
			db.exec("CREATE INDEX patcher_name_version ON patchers(name, runner_rnbo_version)");
	});
	do_migration(4, [](SQLite::Database& db) {
			db.exec("ALTER TABLE patchers ADD COLUMN audio_inputs INTEGER DEFAULT 0");
			db.exec("ALTER TABLE patchers ADD COLUMN audio_outputs INTEGER DEFAULT 0");
			db.exec("ALTER TABLE patchers ADD COLUMN midi_inputs INTEGER DEFAULT 0");
			db.exec("ALTER TABLE patchers ADD COLUMN midi_outputs INTEGER DEFAULT 0");
	});
	do_migration(5, [](SQLite::Database& db) {
			db.exec(R"(CREATE TABLE IF NOT EXISTS sets (
					id INTEGER PRIMARY KEY AUTOINCREMENT,
					name TEXT NOT NULL,
					filename TEXT NOT NULL,
					runner_rnbo_version TEXT NOT NULL,
					created_at REAL DEFAULT (datetime('now', 'localtime'))
			)
			)"
			);
			db.exec("CREATE INDEX set_version ON sets(runner_rnbo_version)");
			db.exec("CREATE INDEX set_name_version ON sets(name, runner_rnbo_version)");
	});
	do_migration(6, [](SQLite::Database& db) {
			db.exec(R"(CREATE TABLE IF NOT EXISTS presets (
					id INTEGER PRIMARY KEY AUTOINCREMENT,
					patcher_id INTEGER NOT NULL,
					name TEXT NOT NULL,
					content TEXT NOT NULL,
					initial INTEGER NOT NULL DEFAULT 0,
					created_at REAL DEFAULT (datetime('now', 'localtime')),
					updated_at REAL DEFAULT (datetime('now', 'localtime')),
					FOREIGN KEY (patcher_id) REFERENCES patchers(id),
					UNIQUE (patcher_id, name)
			)
			)"
			);
			db.exec("CREATE INDEX preset_patcher_id ON presets(patcher_id)");
	});
	//add on delete CASCADE
	//https://www.sqlite.org/foreignkeys.html
	//https://www.techonthenet.com/sqlite/foreign_keys/foreign_delete.php
	do_migration(7, [](SQLite::Database& db) {
			db.exec(R"(
PRAGMA foreign_keys=off;

BEGIN TRANSACTION;

ALTER TABLE presets RENAME TO _presets_old;

CREATE TABLE presets
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	patcher_id INTEGER NOT NULL,
	name TEXT NOT NULL,
	content TEXT NOT NULL,
	initial INTEGER NOT NULL DEFAULT 0,
	created_at REAL DEFAULT (datetime('now', 'localtime')),
	updated_at REAL DEFAULT (datetime('now', 'localtime')),
	FOREIGN KEY (patcher_id) REFERENCES patchers(id) ON DELETE CASCADE,
	UNIQUE (patcher_id, name)
);

INSERT INTO presets SELECT * FROM _presets_old;

COMMIT;

PRAGMA foreign_keys=on;
			)"
			);
	});
}

DB::~DB() { }


void DB::patcherStore(
		const std::string& name,
		const fs::path& so_name,
		const fs::path& config_name,
		const std::string& max_rnbo_version,
		int audio_inputs,
		int audio_outputs,
		int midi_inputs,
		int midi_outputs
		) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, "INSERT INTO patchers (name, runner_rnbo_version, max_rnbo_version, so_path, config_path, audio_inputs, audio_outputs, midi_inputs, midi_outputs) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
	query.bind(1, name);
	query.bind(2, rnbo_version);
	query.bind(3, max_rnbo_version);
	query.bind(4, so_name.string());
	query.bind(5, config_name.string());
	query.bind(6, audio_inputs);
	query.bind(7, audio_outputs);
	query.bind(8, midi_inputs);
	query.bind(9, midi_outputs);
	query.exec();
}

bool DB::patcherGetLatest(const std::string& name, fs::path& so_name, fs::path& config_name) {
	std::lock_guard<std::mutex> guard(mMutex);

		SQLite::Statement query(mDB, "SELECT so_path, config_path FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		if (query.executeStep()) {
			const char * s = query.getColumn(0);
			so_name = fs::path(s);
			s = query.getColumn(1);
			config_name = fs::path(s);
			return true;
		}
		return false;
}

boost::optional<std::string> DB::patcherNameByIndex(int index) {

	SQLite::Statement query(mDB, R"(
		SELECT name FROM patchers
		WHERE id IN (SELECT MAX(id) FROM patchers WHERE runner_rnbo_version = ?1 GROUP BY name) ORDER BY name, created_at DESC
		LIMIT 1 OFFSET ?2
	)");
	query.bind(1, rnbo_version);
	query.bind(2, index);

	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return std::string(s);
	}

	return boost::none;
}


void DB::patcherDestroy(const std::string& name, std::function<void(boost::filesystem::path& so_name, boost::filesystem::path& config_name)> f) {
	//TODO what about sets?
	std::lock_guard<std::mutex> guard(mMutex);
	{
		SQLite::Statement query(mDB, "SELECT so_path, config_path FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		while (query.executeStep()) {
			const char * s = query.getColumn(0);
			fs::path so_name(s);
			s = query.getColumn(1);
			fs::path config_name(s);
			f(so_name, config_name);
		}
	}
	{
		SQLite::Statement query(mDB, "DELETE FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		query.executeStep();
	}
}

void DB::patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> func) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT name, audio_inputs, audio_outputs, midi_inputs, midi_outputs, datetime(created_at) FROM patchers
		WHERE id IN (SELECT MAX(id) FROM patchers WHERE runner_rnbo_version = ?1 GROUP BY name) ORDER BY name, created_at DESC
	)");
	query.bind(1, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string name(s);

		int audio_inputs = query.getColumn(1);
		int audio_outputs = query.getColumn(2);
		int midi_inputs = query.getColumn(3);
		int midi_outputs  = query.getColumn(4);

		s = query.getColumn(5);
		std::string created_at(s);

		func(name, audio_inputs, audio_outputs, midi_inputs, midi_outputs, created_at);
	}
}

void DB::presets(const std::string& patchername, std::function<void(const std::string&, bool)> f) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT name, initial FROM presets
		WHERE patcher_id IN (SELECT MAX(id) FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name) ORDER BY created_at DESC
	)");
	query.bind(1, patchername);
	query.bind(2, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string name(s);

		int initial = query.getColumn(1);
		f(name, (bool)initial);
	}
}

boost::optional<std::pair<std::string, std::string>> DB::preset(const std::string& patchername, const std::string& presetName) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT content, name FROM presets WHERE name = ?1 AND patcher_id IN
		(SELECT MAX(id) FROM patchers WHERE name = ?2 AND runner_rnbo_version = ?3 GROUP BY name)
	)");

	query.bind(1, presetName);
	query.bind(2, patchername);
	query.bind(3, rnbo_version);
	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string content(s);

		s = query.getColumn(1);
		std::string name(s);

		return std::make_pair(content, name);
	}
	return boost::none;
}

boost::optional<std::pair<std::string, std::string>> DB::preset(const std::string& patchername, unsigned int index) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT content, name FROM presets WHERE patcher_id IN
		(SELECT MAX(id) FROM patchers WHERE name = ?2 AND runner_rnbo_version = ?3 GROUP BY name)
		ORDER BY created_at
		LIMIT 1 OFFSET ?1
	)");

	query.bind(1, index);
	query.bind(2, patchername);
	query.bind(3, rnbo_version);
	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string content(s);

		s = query.getColumn(1);
		std::string name(s);

		return std::make_pair(content, name);
	}
	return boost::none;
}

void DB::presetSave(const std::string& patchername, const std::string& presetName, const std::string& preset) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		INSERT INTO presets (patcher_id, name, content)
		SELECT MAX(id), ?1, ?2 FROM patchers WHERE name = ?3 AND runner_rnbo_version = ?4 GROUP BY name
		ON CONFLICT DO UPDATE SET content=excluded.content, updated_at = datetime('now', 'localtime')
	)");

	query.bind(1, presetName);
	query.bind(2, preset);
	query.bind(3, patchername);
	query.bind(4, rnbo_version);
	query.exec();
}

void DB::presetSetInitial(const std::string& patchername, const std::string& presetName) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		UPDATE presets
			SET initial=p.initial
		FROM
			(SELECT name == ?3 as initial, id FROM presets WHERE patcher_id IN (SELECT MAX(id) FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)) AS p
		WHERE p.id = presets.id
	)");

	query.bind(1, patchername);
	query.bind(2, rnbo_version);
	query.bind(3, presetName);
	query.exec();
}

void DB::presetDestroy(const std::string& patchername, const std::string& presetName) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		DELETE FROM presets
		WHERE name = ?1 AND patcher_id IN (SELECT MAX(id) FROM patchers WHERE name = ?2 AND runner_rnbo_version = ?3 GROUP BY name)
	)");

	query.bind(1, presetName);
	query.bind(2, patchername);
	query.bind(3, rnbo_version);
	query.exec();
}

void DB::setSave(
		const std::string& name,
		const boost::filesystem::path& filename
		)
{
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, "INSERT INTO sets (name, runner_rnbo_version, filename) VALUES (?1, ?2, ?3)");
	query.bind(1, name);
	query.bind(2, rnbo_version);
	query.bind(3, filename.string());
	query.exec();
}

boost::optional<boost::filesystem::path> DB::setGet(
		const std::string& name
		)
{
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, "SELECT filename FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
	query.bind(1, name);
	query.bind(2, rnbo_version);
	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return fs::path(s);
	}
	return boost::none;
}

void DB::sets(std::function<void(const std::string& name, const std::string& created)> func)
{
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT name, datetime(created_at) FROM sets
		WHERE id IN (SELECT MAX(id) FROM sets WHERE runner_rnbo_version = ?1 GROUP BY name) ORDER BY name, created_at DESC
	)");
	query.bind(1, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string name(s);

		s = query.getColumn(1);
		std::string created_at(s);

		func(name, created_at);
	}
}
