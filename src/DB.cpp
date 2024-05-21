#include "DB.h"
#include "Config.h"

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/none.hpp>

//https://srombauts.github.io/SQLiteCpp/

namespace fs = boost::filesystem;

namespace {
	const std::string cur_rnbo_version(RNBO_VERSION);
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
		query.bind(1, cur_rnbo_version);
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

	int lastmigration = 1;
	auto do_migration = [curversion, &lastmigration, this](int version, std::function<void(SQLite::Database&)> func) {
		//verify that our migrations are increasing by 1 each time
		assert(version == lastmigration + 1);
		lastmigration = version;

		if (curversion < version) {
			func(mDB);
			SQLite::Statement query(mDB, "INSERT INTO migrations (id, rnbo_version) VALUES (?, ?)");
			query.bind(1, version);
			query.bind(2, cur_rnbo_version);
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
	do_migration(8, [](SQLite::Database& db) {
			db.exec("ALTER TABLE patchers ADD COLUMN rnbo_patch_name TEXT");
	});
	do_migration(9, [](SQLite::Database& db) {
			db.exec("DROP TABLE _presets_old");
	});
	do_migration(10, [](SQLite::Database& db) {
			db.exec(R"(
CREATE TABLE listeners
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	ip TEXT NOT NULL,
	port INTEGER NOT NULL,
	UNIQUE (ip, port)
);
			)");
	});

	// A set preset is just a bunch of individual presets where the
	// individual entries share a set_id, a set_preset_name and have
	// unique set_instance_index to indicate which instance they apply to
	do_migration(11, [](SQLite::Database& db) {
			db.exec(R"(
CREATE TABLE sets_presets
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	patcher_id INTEGER NOT NULL,
	set_id INTEGER NOT NULL,
	set_instance_index INTEGER NOT NULL,
	name TEXT NOT NULL,
	content TEXT NOT NULL,
	initial INTEGER NOT NULL DEFAULT 0,
	created_at REAL DEFAULT (datetime('now', 'localtime')),
	updated_at REAL DEFAULT (datetime('now', 'localtime')),
	FOREIGN KEY (patcher_id) REFERENCES patchers(id) ON DELETE CASCADE,
	FOREIGN KEY (set_id) REFERENCES sets(id) ON DELETE CASCADE,
	UNIQUE (patcher_id, set_id, set_instance_index, name)
)
			)");
			db.exec("CREATE INDEX set_preset_set_id ON sets_presets(set_id)");
			db.exec("CREATE INDEX set_preset_patcher_id_instance_index ON sets_presets(patcher_id, set_id, set_instance_index)");
	});

	//turn on foreign_keys support
	mDB.exec("PRAGMA foreign_keys=on");
}

DB::~DB() { }

void DB::rnboVersions(std::function<void(const std::string&)> f) {
		SQLite::Statement query(mDB, "SELECT DISTINCT(runner_rnbo_version) FROM patchers ORDER BY id DESC");
		while (query.executeStep()) {
			const char * s = query.getColumn(0);
			std::string str(s);
			f(s);
		}
}

void DB::patcherStore(
		const std::string& name,
		const fs::path& so_name,
		const fs::path& config_name,
		const fs::path& rnbo_patch_name,
		const std::string& max_rnbo_version,
		bool migrate_presets,
		int audio_inputs,
		int audio_outputs,
		int midi_inputs,
		int midi_outputs
		) {
	std::lock_guard<std::mutex> guard(mMutex);

	int old_id = 0; //ids always start at 1 right?
	if (migrate_presets) {
		SQLite::Statement query(mDB, "SELECT MAX(id) FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		if (query.executeStep()) {
			old_id = query.getColumn(0);
		}
	}

	{
		SQLite::Statement query(mDB, R"(
			INSERT INTO patchers (name, runner_rnbo_version, max_rnbo_version, so_path, config_path, audio_inputs, audio_outputs, midi_inputs, midi_outputs, rnbo_patch_name)
			VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10))");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		query.bind(3, max_rnbo_version);
		query.bind(4, so_name.string());
		query.bind(5, config_name.string());
		query.bind(6, audio_inputs);
		query.bind(7, audio_outputs);
		query.bind(8, midi_inputs);
		query.bind(9, midi_outputs);
		query.bind(10, rnbo_patch_name.string());
		query.exec();
	}

	if (old_id) {
		auto new_id = mDB.getLastInsertRowid();
		SQLite::Statement query(mDB, R"(
			INSERT INTO presets (patcher_id, name, content, initial, created_at, updated_at)
			SELECT ?2, name, content, initial, created_at, updated_at FROM presets WHERE patcher_id = ?1)");
		query.bind(1, old_id);
		query.bind(2, new_id);
		query.exec();
	}
}

bool DB::patcherGetLatest(const std::string& name, fs::path& so_name, fs::path& config_name, fs::path& rnbo_patch_name, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

		SQLite::Statement query(mDB, "SELECT so_path, config_path, rnbo_patch_name FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		if (query.executeStep()) {
			const char * s = query.getColumn(0);
			so_name = fs::path(s);
			s = query.getColumn(1);
			config_name = fs::path(s);
			s = query.getColumn(2);
			rnbo_patch_name = fs::path(s);
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
	query.bind(1, cur_rnbo_version);
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
		query.bind(2, cur_rnbo_version);
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
		query.bind(2, cur_rnbo_version);
		query.executeStep();
	}
}

void DB::patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> func, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

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

void DB::presets(const std::string& patchername, std::function<void(const std::string&, bool)> f, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

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

boost::optional<std::pair<std::string, std::string>> DB::preset(const std::string& patchername, const std::string& presetName, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

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
	query.bind(3, cur_rnbo_version);
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

	//XXX make sure to update patcherStore preset migration with any changes to the preset structure
	SQLite::Statement query(mDB, R"(
		INSERT INTO presets (patcher_id, name, content)
		SELECT MAX(id), ?1, ?2 FROM patchers WHERE name = ?3 AND runner_rnbo_version = ?4 GROUP BY name
		ON CONFLICT DO UPDATE SET content=excluded.content, updated_at = datetime('now', 'localtime')
	)");

	query.bind(1, presetName);
	query.bind(2, preset);
	query.bind(3, patchername);
	query.bind(4, cur_rnbo_version);
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
	query.bind(2, cur_rnbo_version);
	query.bind(3, presetName);
	query.exec();
}

void DB::presetRename(const std::string& patchername, const std::string& oldName, const std::string& newName) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		UPDATE presets
			SET name=?4
		FROM
			(SELECT MAX(patchers.id) as patcher_id, presets.id FROM patchers
				JOIN presets ON presets.patcher_id = patchers.id
			WHERE patchers.name = ?1 AND patchers.runner_rnbo_version = ?2 AND presets.name = ?3
			GROUP BY patchers.name) as p
		WHERE p.id = presets.id
	)");

	query.bind(1, patchername);
	query.bind(2, cur_rnbo_version);
	query.bind(3, oldName);
	query.bind(4, newName);
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
	query.bind(3, cur_rnbo_version);
	query.exec();


}

void DB::setPresets(const std::string& setname, std::function<void(const std::string&)> f, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT DISTINCT name FROM sets_presets
		WHERE set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name) ORDER BY created_at DESC
	)");
	query.bind(1, setname);
	query.bind(2, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string name(s);
		f(name);
	}
}

boost::optional<std::string> DB::setPreset(
		const std::string& patchername,
		const std::string& presetName,
		const std::string& setName,
		unsigned int instanceIndex,
		std::string rnbo_version
) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT content FROM sets_presets
		WHERE
		name = ?1
		AND set_instance_index = ?5
		AND patcher_id IN
		(
			SELECT MAX(id) FROM patchers WHERE name = ?2 AND runner_rnbo_version = ?3 GROUP BY name
		)
		AND set_id IN (
			SELECT MAX(id) FROM sets WHERE name = ?4 AND runner_rnbo_version = ?3 GROUP BY name
		)
		ORDER BY created_at
		LIMIT 1
	)");

	query.bind(1, presetName);
	query.bind(2, patchername);
	query.bind(3, cur_rnbo_version);
	query.bind(4, setName);
	query.bind(5, instanceIndex);

	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return { std::string(s) };
	}

	return boost::none;
}

void DB::setPresetSave(
		const std::string& patchername,
		const std::string& presetName,
		const std::string& setName,
		unsigned int instanceIndex,
		const std::string& content
) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		INSERT INTO sets_presets (patcher_id, set_id, name, set_instance_index, content)
		SELECT patchers.id, sets.id, ?1, ?2, ?3
		FROM patchers, sets
		WHERE patchers.id IN (SELECT MAX(id) FROM patchers WHERE name = ?5 AND runner_rnbo_version = ?4 GROUP BY name)
		AND sets.id IN (SELECT MAX(id) FROM sets WHERE name = ?6 AND runner_rnbo_version = ?4 GROUP BY name)
		ON CONFLICT DO UPDATE SET content=excluded.content, updated_at = datetime('now', 'localtime')
	)");

	query.bind(1, presetName);
	query.bind(2, static_cast<int>(instanceIndex));
	query.bind(3, content);
	query.bind(4, cur_rnbo_version);
	query.bind(5, patchername);
	query.bind(6, setName);
	query.exec();
}

void DB::setPresetRename(
		const std::string& setName,
		const std::string& oldName,
		const std::string& newName,
		std::string rnbo_version
) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		UPDATE sets_presets SET name = ?3
		WHERE name = ?2
		AND set_id IN (
			SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?4 GROUP BY name
		)
	)");
	query.bind(1, setName);
	query.bind(2, oldName);
	query.bind(3, newName);
	query.bind(4, rnbo_version);
	query.exec();
}

void DB::setPresetDestroy(
		const std::string& setName,
		const std::string& presetName,
		std::string rnbo_version
) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		DELETE FROM sets_presets
		WHERE name = ?2
		AND set_id IN (
			SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?3 GROUP BY name
		)
	)");
	query.bind(1, setName);
	query.bind(2, presetName);
	query.bind(3, rnbo_version);
	query.exec();

}

void DB::setSave(
		const std::string& name,
		const boost::filesystem::path& filename,
		bool migrate_presets
		)
{
	std::lock_guard<std::mutex> guard(mMutex);

	//get old id for migrating
	int old_id = 0;
	if (migrate_presets) {
		SQLite::Statement query(mDB, "SELECT id FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		if (query.executeStep()) {
			old_id = query.getColumn(0);
		}
	}

	{
		SQLite::Statement query(mDB, "INSERT INTO sets (name, runner_rnbo_version, filename) VALUES (?1, ?2, ?3)");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		query.bind(3, filename.string());
		query.exec();
	}

	//migrate presets
	if (migrate_presets && old_id) {
		auto new_id = mDB.getLastInsertRowid();
		SQLite::Statement query(mDB, "UPDATE sets_presets SET set_id = ?1 WHERE set_id = ?2");
		query.bind(1, new_id);
		query.bind(2, old_id);
		query.exec();
	}
}

bool DB::setDestroy(const std::string& name)
{
	//presets get deleted because of on delete cascade
	{
		SQLite::Statement query(mDB, "DELETE FROM sets WHERE name=?1 AND runner_rnbo_version=?2");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		return query.exec() > 0;
	}
}

bool DB::setRename(const std::string& oldName, const std::string& newName)
{
	std::lock_guard<std::mutex> guard(mMutex);

	{
		SQLite::Statement query(mDB, "UPDATE OR IGNORE sets SET name=?3 WHERE name=?1 AND runner_rnbo_version=?2");
		query.bind(1, oldName);
		query.bind(2, cur_rnbo_version);
		query.bind(3, newName);
		return query.exec() > 0;
	}
}

boost::optional<boost::filesystem::path> DB::setGet(const std::string& name, std::string rnbo_version)
{
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

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

void DB::sets(std::function<void(const std::string& name, const std::string& created)> func, std::string rnbo_version)
{
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

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

bool DB::listenersAdd(const std::string& ip, uint16_t port)
{
	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "INSERT OR IGNORE INTO listeners (ip, port) VALUES (?1, ?2)");
	query.bind(1, ip);
	query.bind(2, port);
	query.exec();
	return mDB.getChanges() != 0;
}

bool DB::listenersDel(const std::string& ip, uint16_t port)
{
	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "DELETE FROM listeners where ip = ?1 AND port = ?2");
	query.bind(1, ip);
	query.bind(2, port);
	query.exec();
	return mDB.getChanges() != 0;
}

void DB::listenersClear()
{
	std::lock_guard<std::mutex> guard(mMutex);
	mDB.exec("DELETE FROM listeners");
}

void DB::listeners(std::function<void(const std::string& ip, uint16_t port)> func)
{
	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "SELECT ip, port FROM listeners");
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string ip(s);

		int port = query.getColumn(1);

		func(ip, static_cast<uint16_t>(port));
	}
}

