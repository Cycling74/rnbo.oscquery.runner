#include "DB.h"
#include "Config.h"

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

//https://srombauts.github.io/SQLiteCpp/

namespace fs = boost::filesystem;

namespace {
	const std::string rnbo_version(RNBO_VERSION);
	void do_migration(int curversion, SQLite::Database& db, int version, std::function<void(SQLite::Database&)> func) {
		if (curversion < version) {
			func(db);
			SQLite::Statement query(db, "INSERT INTO migrations (id, rnbo_version) VALUES (?, ?)");
			query.bind(1, version);
			query.bind(2, rnbo_version);
			query.exec();
		}
	}
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

	int cur = 0;
	{
		SQLite::Statement query(mDB, "SELECT MAX(id) FROM migrations");
		if (!query.executeStep()) {
			throw new std::runtime_error("failed to get result from migrations");
		}
		//migrate
		cur = query.getColumn(0);
	}

	do_migration(cur, mDB, 2, [](SQLite::Database& db) {
			db.exec(R"(CREATE TABLE IF NOT EXISTS patchers (
					id INTEGER PRIMARY KEY,
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
}

DB::~DB() { }

