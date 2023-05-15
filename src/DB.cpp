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
}

DB::~DB() { }


void DB::patcherStore(
		const std::string& name,
		const fs::path& so_name,
		const fs::path& config_name,
		const std::string& max_rnbo_version
		) {
	SQLite::Statement query(mDB, "INSERT INTO patchers (name, runner_rnbo_version, max_rnbo_version, so_path, config_path) VALUES (?1, ?2, ?3, ?4, ?5)");
	query.bind(1, name);
	query.bind(2, rnbo_version);
	query.bind(3, max_rnbo_version);
	query.bind(4, so_name.string());
	query.bind(5, config_name.string());
	query.exec();
}

bool DB::patcherGetLatest(
		const std::string& name,
		fs::path& so_name,
		fs::path& config_name,
		std::string& created_at
		) {
		SQLite::Statement query(mDB, "SELECT so_path, config_path, created_at FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		if (query.executeStep()) {
			const char * s = query.getColumn(0);
			so_name = fs::path(s);
			s = query.getColumn(1);
			config_name = fs::path(s);
			s = query.getColumn(2);
			created_at = std::string(s);
			return true;
		}
		return false;
}
