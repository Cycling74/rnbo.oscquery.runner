#include "DB.h"
#include "Config.h"

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

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

void DB::patchers(std::function<void(const std::string&, int, int, int, int, const std::string&)> func) {
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

void DB::setSave(
		const std::string& name,
		const boost::filesystem::path& filename
		)
{
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
