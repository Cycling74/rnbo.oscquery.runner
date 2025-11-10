#include "DB.h"
#include "Config.h"

#include <algorithm>
#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/none.hpp>
#include <boost/algorithm/string.hpp>

#include <SQLiteCpp/Backup.h>

//https://srombauts.github.io/SQLiteCpp/

namespace fs = boost::filesystem;

namespace {
	const std::string cur_rnbo_version(RNBO_VERSION);

	std::string getStringColumn(SQLite::Statement& query, int col) {
		const char * s = query.getColumn(col);
		return std::string(s);
	}

	int getsetid(SQLite::Database& db, const std::string& name) {
		SQLite::Statement query(db, "SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		if (query.executeStep()) {
			return query.getColumn(0);
		}
		return 0;
	};

	int getsetviewid(SQLite::Database& db, int set_id, int view_index) {
		SQLite::Statement query(db, "SELECT id FROM sets_views WHERE set_id = ?1 AND view_index = ?2");
		query.bind(1, set_id);
		query.bind(2, view_index);
		if (query.executeStep()) {
			return query.getColumn(0);
		}
		return 0;
	}

	std::unordered_map<int, int> setInstanceToPatcherId(SQLite::Database& db, int set_id) {
		std::unordered_map<int, int> values;
		SQLite::Statement query(db, "SELECT set_instance_index, patcher_id FROM sets_patcher_instances WHERE set_id = ?1");
		query.bind(1, set_id);
		while (query.executeStep()) {
			values.insert({ query.getColumn(0), query.getColumn(1) });
		}
		return values;
	}

	void insertSetViewParams(SQLite::Database& db, int set_id, int set_view_id, const std::vector<ViewParam>& params) {
		std::unordered_map<int, int> instanceindexpatcherid = setInstanceToPatcherId(db, set_id);

		{
			SQLite::Statement query(db, "INSERT OR IGNORE INTO sets_views_params (set_view_id, sort_order, set_instance_index, param_id, patcher_id) VALUES(?1, ?2, ?3, ?4, ?5)");
			for (auto i = 0; i < params.size(); i++) {
				const auto& param = params[i];
				auto it = instanceindexpatcherid.find(param.instance_index);
				if (it != instanceindexpatcherid.end()) {
					int patcher_id = it->second;

					query.bind(1, set_view_id);
					query.bind(2, i);
					query.bind(3, param.instance_index);
					query.bind(4, param.param_id);
					query.bind(5, patcher_id);

					query.exec();
					query.reset();
				} else {
					std::cerr << "failed to find patcher id while inserting set view param data" << std::endl;
				}
			}
		}
	}

	void writePatchersParams(SQLite::Database& db, const int patcher_id, const fs::path& config_name) {
		auto configp = config::get<fs::path>(config::key::SourceCacheDir).get() / config_name;

		if (!fs::exists(configp)) {
			std::cerr << "config file does not exist at path: " << configp.string() << std::endl;
			return;
		}

		RNBO::Json config;
		std::ifstream i(configp.string());
		i >> config;
		if (!config.contains("parameters")) {
			std::cerr << "config file does not contain parameters: " << configp.string() << std::endl;
			return;
		}

		SQLite::Statement query(db, "INSERT INTO patchers_params (patcher_id, param_id, param_index) VALUES(?1, ?2, ?3)");

		for (auto p: config["parameters"]) {
			if (p.contains("type") && p.contains("index") && p.contains("paramId")) {
				if (p["type"].get<std::string>() == "ParameterTypeNumber") {
					std::string param_id = p["paramId"].get<std::string>();
					int param_index = p["index"].get<int>();
					query.bind(1, patcher_id);
					query.bind(2, param_id);
					query.bind(3, param_index);
					query.exec();
					query.reset();
				}
			} else {
				std::cerr << "config param entry does not contain expected keys: " << configp.string() << std::endl;
				break;
			}
		}
	}
}

ViewParam ViewParam::fromString(const std::string& v) {
	std::vector<std::string> details;
	boost::algorithm::split(details, v, boost::is_any_of(":"));
	if (details.size() != 2) {
		throw new std::runtime_error("view param not in expected format");
	}
	return ViewParam(stoi(details[0]), details[1]);
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
	bool backupdone = curversion <= 1; //don't need to backup the initial version
	auto do_migration = [curversion, &lastmigration, &backupdone, this](int version, std::function<void(SQLite::Database&)> func, bool intransaction = true) {
		//verify that our migrations are increasing by 1 each time
		assert(version == lastmigration + 1);
		lastmigration = version;

		if (curversion < version) {

			//do backup
			if (!backupdone) {
				std::string backupname = "oscqueryrunner-dbversion-" + std::to_string(curversion);
				backup(backupname);
				backupdone = true;
			}

			auto incr = [this, &version]() {
					SQLite::Statement query(mDB, "INSERT INTO migrations (id, rnbo_version) VALUES (?, ?)");
					query.bind(1, version);
					query.bind(2, cur_rnbo_version);
					query.exec();
			};

			if (intransaction) {
				SQLite::Transaction transaction(mDB);
				func(mDB);
				incr();
				transaction.commit();
			} else {
				func(mDB);
				incr();
			}
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
	}, false);
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

	do_migration(12, [](SQLite::Database& db) {
			db.exec("ALTER TABLE sets ADD COLUMN meta TEXT");

			db.exec(R"(
CREATE TABLE sets_patcher_instances
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	patcher_id INTEGER NOT NULL,
	set_id INTEGER NOT NULL,
	set_instance_index INTEGER NOT NULL,
	config TEXT NOT NULL,

	FOREIGN KEY (patcher_id) REFERENCES patchers(id) ON DELETE CASCADE,
	FOREIGN KEY (set_id) REFERENCES sets(id) ON DELETE CASCADE,
	UNIQUE (set_id, set_instance_index)
)
			)");

			db.exec(R"(
CREATE TABLE sets_connections
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	set_id INTEGER NOT NULL,

	source_name TEXT NOT NULL,
	source_instance_index INTEGER,
	source_port_name TEXT NOT NULL,

	sink_name TEXT NOT NULL,
	sink_instance_index INTEGER,
	sink_port_name TEXT NOT NULL,

	FOREIGN KEY (set_id) REFERENCES sets(id) ON DELETE CASCADE,
	UNIQUE (set_id, source_name, source_port_name, sink_name, sink_port_name)
)
			)");

	});

	do_migration(13, [](SQLite::Database& db) {
		db.exec("ALTER TABLE sets ADD COLUMN initial INTEGER DEFAULT 0");
		db.exec("UPDATE sets SET initial=0");
	});

	do_migration(14, [](SQLite::Database& db) {
		db.exec("ALTER TABLE sets_presets ADD COLUMN preset_name TEXT DEFAULT NULL");
	});

	do_migration(15, [](SQLite::Database& db) {
			db.exec(R"(
CREATE TABLE sets_views
(
	id INTEGER PRIMARY KEY AUTOINCREMENT,

	params TEXT NOT NULL,
	name TEXT NOT NULL,

	set_id INTEGER NOT NULL,
	view_index INTEGER NOT NULL,

	sort_order INTEGER NOT NULL DEFAULT 100,

	FOREIGN KEY (set_id) REFERENCES sets(id) ON DELETE CASCADE,
	UNIQUE (set_id, view_index)
)
			)");
	});

	do_migration(16, [](SQLite::Database& db) {
			//data migrations indicate that we've copied over the data we hope to for the runner_rnbo_version specified
			//the lack of this entry can be used to compute the presence of migration data
			db.exec(R"(
CREATE TABLE data_migrations
(
	runner_rnbo_version TEXT NOT NULL,
	data_rnbo_version TEXT NOT NULL,
	UNIQUE (data_rnbo_version)
)
			)");
	});

	//need to be able to map param id to param index for sets_views
	do_migration(17, [](SQLite::Database& db) {
			//create the table
			db.exec(R"(
CREATE TABLE patchers_params
(
	patcher_id INTEGER NOT NULL,
	param_id TEXT NOT NULL,
	param_index INTEGER NOT NULL,
	FOREIGN KEY (patcher_id) REFERENCES patchers(id) ON DELETE CASCADE,
	UNIQUE (patcher_id, param_id)
)
			)");

			//get the data, we only care about the latest set of patchers (if there are any)
			{
				std::unordered_map<int, std::string> idtoconfig;
				{
					SQLite::Statement query(db, "SELECT id, config_path FROM patchers WHERE runner_rnbo_version IN (SELECT runner_rnbo_version FROM patchers ORDER BY id DESC LIMIT 1)");
					while (query.executeStep()) {
						int id = query.getColumn(0);
						const char * s = query.getColumn(1);
						idtoconfig.insert({ id, std::string(s) });
					}
				}

				//update current patchers
				for (auto& kv: idtoconfig) {
					writePatchersParams(db, kv.first, kv.second);
				}
			}

			db.exec(R"(
CREATE TABLE sets_views_params
(
	set_view_id INTEGER NOT NULL,

	sort_order INTEGER NOT NULL,
	set_instance_index INTEGER NOT NULL,
	patcher_id INTEGER NOT NULL,
	param_id TEXT NOT NULL,

	FOREIGN KEY (patcher_id) REFERENCES patchers(id) ON DELETE CASCADE,
	FOREIGN KEY (set_view_id) REFERENCES sets_views(id) ON DELETE CASCADE
)
)");
			{
				//migrate the data
				std::vector<std::tuple<int, std::string, int>> data;
				{
					SQLite::Statement query(db, "SELECT id, params, set_id FROM sets_views");
					while (query.executeStep()) {
							int id = query.getColumn(0);
							const char * s = query.getColumn(1);
							int set_id = query.getColumn(2);

							data.emplace_back(id, getStringColumn(query, 1), set_id);
					}
				}
				for (auto& entry: data) {
					int set_view_id = std::get<0>(entry);
					int set_id = std::get<2>(entry);
					std::string& paramsString = std::get<1>(entry);

					std::vector<std::string> params;
					boost::algorithm::split(params, paramsString, boost::is_any_of(","));

					std::unordered_map<int, int> instanceindexpatcherid = setInstanceToPatcherId(db, set_id);

					//this is real slow but we only do it once
					std::vector<ViewParam> viewparams;
					for (auto p: params) {
						std::vector<std::string> details;
						boost::algorithm::split(details, p, boost::is_any_of(":"));
						if (details.size() != 2) {
							std::cerr << "param entry not in expected format: " << p << std::endl;
							continue;
						}

						int set_instance_index = stoi(details[0]);
						int param_index = stoi(details[1]);
						auto it = instanceindexpatcherid.find(set_instance_index);
						if (it == instanceindexpatcherid.end()) {
							std::cerr << "couldn't find patcher_id for set_instance_index: " << set_instance_index << std::endl;
							continue;
						}
						int patcher_id = it->second;

						SQLite::Statement query(db, "SELECT param_id FROM patchers_params WHERE patcher_id = ?1 AND param_index = ?2");
						query.bind(1, patcher_id);
						query.bind(2, param_index);
						if (!query.executeStep()) {
							std::cerr << "failed to get param_id from patchers_params with patcher_id=" << patcher_id << " and param_index=" << param_index << std::endl;
							continue;
						}
						std::string param_id = getStringColumn(query, 0);
						viewparams.emplace_back(set_instance_index, param_id);
					}

					insertSetViewParams(db, set_id, set_view_id, viewparams);
				}
			}

	});

	//turn on foreign_keys support
	mDB.exec("PRAGMA foreign_keys=on");
}

DB::~DB() { }

std::string DB::backup(std::string backupname) {
	backupname = backupname + ".sqlite";
	auto backuppath = config::get<fs::path>(config::key::BackupDir).get() / backupname;

	SQLite::Database backupDB(backuppath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
	SQLite::Backup backup(backupDB, mDB);
	try {
		backup.executeStep();
	} catch (...) {
		std::cerr << "failed to backup db to: " << backuppath.string() << std::endl;
		return "";
	}
	return backupname;
}

void DB::rnboVersions(std::function<void(const std::string&)> f) {
		SQLite::Statement query(mDB, "SELECT DISTINCT(runner_rnbo_version) FROM patchers ORDER BY id DESC");
		while (query.executeStep()) {
			const char * s = query.getColumn(0);
			std::string str(s);
			f(s);
		}
}

boost::optional<std::string> DB::migrationDataAvailable() {
	SQLite::Statement query(mDB, "SELECT DISTINCT(runner_rnbo_version) FROM patchers WHERE runner_rnbo_version != ?1 AND runner_rnbo_version NOT IN (SELECT data_rnbo_version FROM data_migrations) ORDER BY id DESC LIMIT 1");
	query.bind(1, cur_rnbo_version);

	if (query.executeStep()) {
		const char * val = query.getColumn(0);
		return { std::string(val) };
	}

	return boost::none;
}

void DB::markDataMigrated() {
		//mark all data migrated, no matter what the version is
		SQLite::Statement query(mDB, "INSERT OR IGNORE INTO data_migrations (data_rnbo_version, runner_rnbo_version) SELECT DISTINCT(runner_rnbo_version), ?1 FROM patchers WHERE runner_rnbo_version != ?1 ORDER BY id DESC");
		query.bind(1, cur_rnbo_version);
		query.exec();
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

	SQLite::Transaction transaction(mDB);

	int old_id = 0; //ids always start at 1 right?
	{
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

	auto new_id = mDB.getLastInsertRowid();
	writePatchersParams(mDB, new_id, config_name);

	if (old_id) {
		if (migrate_presets) {
			SQLite::Statement query(mDB, R"(
				INSERT INTO presets (patcher_id, name, content, initial, created_at, updated_at)
				SELECT ?2, name, content, initial, created_at, updated_at FROM presets WHERE patcher_id = ?1)");
			query.bind(1, old_id);
			query.bind(2, new_id);
			query.exec();
		}
		//always update set presets and sets_patcher_instances
		{
			SQLite::Statement query(mDB, "UPDATE sets_presets SET patcher_id = ?2 WHERE patcher_id = ?1");
			query.bind(1, old_id);
			query.bind(2, new_id);
			query.exec();
		}
		{
			SQLite::Statement query(mDB, "UPDATE sets_patcher_instances SET patcher_id = ?2 WHERE patcher_id = ?1");
			query.bind(1, old_id);
			query.bind(2, new_id);
			query.exec();
		}

		//update patcher_id for sets_views_params
		{
			SQLite::Statement query(mDB, "UPDATE sets_views_params SET patcher_id = ?2 WHERE patcher_id = ?1");
			query.bind(1, old_id);
			query.bind(2, new_id);
			query.exec();
		}
		//remove params from sets_views_params that no longer exist
		{
			SQLite::Statement query(mDB, "DELETE FROM sets_views_params WHERE patcher_id = ?1 AND param_id NOT IN (SELECT param_id FROM patchers_params WHERE patcher_id = ?1)");
			query.bind(1, new_id);
			query.exec();
		}

		//TODO delete old patchers_params ?
	}
	transaction.commit();
}

bool DB::patcherGetLatest(const std::string& name, fs::path& so_name, fs::path& config_name, fs::path& rnbo_patch_name, std::string& created_at, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

		SQLite::Statement query(mDB, "SELECT so_path, config_path, rnbo_patch_name, created_at FROM patchers WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		if (query.executeStep()) {
			so_name = fs::path(getStringColumn(query, 0));
			config_name = fs::path(getStringColumn(query, 1));
			rnbo_patch_name = fs::path(getStringColumn(query, 2));
			created_at = getStringColumn(query, 3);
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

void DB::patcherRename(const std::string& name, std::string& newName) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, "UPDATE patchers SET name = ?3 WHERE name = ?1 AND runner_rnbo_version = ?2");
	query.bind(1, name);
	query.bind(2, cur_rnbo_version);
	query.bind(3, newName);
	query.executeStep();
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
		int midi_outputs = query.getColumn(4);

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
		ORDER BY initial DESC, name ASC, id ASC
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

	{
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

	{
		//set preset
		SQLite::Statement query(mDB, R"(
			UPDATE sets_presets
				SET preset_name=?4
			FROM
				(SELECT MAX(patchers.id) as patcher_id, sets_presets.preset_name as preset_name FROM patchers
					JOIN sets_presets ON sets_presets.patcher_id = patchers.id
				WHERE patchers.name = ?1 AND patchers.runner_rnbo_version = ?2 AND sets_presets.preset_name = ?3
				GROUP BY patchers.name) as p
			WHERE p.preset_name = sets_presets.preset_name
		)");
		query.bind(1, patchername);
		query.bind(2, cur_rnbo_version);
		query.bind(3, oldName);
		query.bind(4, newName);
		query.exec();
	}

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

std::vector<std::string> DB::setPresets(const std::string& setname, std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);
	std::vector<std::string> names;

	SQLite::Statement query(mDB, R"(
		SELECT DISTINCT name FROM sets_presets
		WHERE set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
		ORDER BY name == 'initial' DESC, name ASC
	)");
	query.bind(1, setname);
	query.bind(2, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		names.push_back(std::string(s));
	}
	return names;
}

boost::optional<std::string> DB::setPresetNameByIndex(
		const std::string& setName,
		unsigned int index,
		std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT DISTINCT name FROM sets_presets
		WHERE set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
		ORDER BY name == 'initial' DESC, name ASC
		LIMIT 1 OFFSET ?3
	)");
	query.bind(1, setName);
	query.bind(2, rnbo_version);
	query.bind(3, static_cast<int>(index));
	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return { std::string(s) };
	}
	return boost::none;
}

void DB::setPresets(
		const std::string& setName,
		const std::string& presetName,
		std::function<void(const std::string& patcherName, unsigned int instanceIndex, const std::string& content, const std::string& patcherPresetName)> func,
		std::string rnbo_version) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT patchers.name, sets_presets.set_instance_index, COALESCE(presets.content, sets_presets.content), COALESCE(presets.name, "") as preset_name
		FROM sets_presets
		JOIN patchers ON patchers.id = sets_presets.patcher_id
		LEFT JOIN presets ON patchers.id = presets.patcher_id AND sets_presets.preset_name = presets.name
		WHERE sets_presets.name = ?3
		AND sets_presets.set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
		ORDER BY sets_presets.set_instance_index
	)");
	query.bind(1, setName);
	query.bind(2, rnbo_version);
	query.bind(3, presetName);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string patchername(s);

		int set_instance_index = query.getColumn(1);

		s = query.getColumn(2);
		std::string content(s);

		s = query.getColumn(3);
		std::string preset_name(s);

		func(patchername, set_instance_index, content, preset_name);
	}
}

boost::optional<std::pair<std::string, std::string>> DB::setPreset(
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
		SELECT COALESCE(presets.content, sets_presets.content), COALESCE(sets_presets.preset_name, "") FROM sets_presets
		LEFT JOIN presets ON sets_presets.preset_name = presets.name AND sets_presets.patcher_id = presets.patcher_id
		WHERE
		sets_presets.name = ?1
		AND sets_presets.set_instance_index = ?5
		AND sets_presets.patcher_id IN
		(
			SELECT MAX(id) FROM patchers WHERE name = ?2 AND runner_rnbo_version = ?3 GROUP BY name
		)
		AND sets_presets.set_id IN (
			SELECT MAX(id) FROM sets WHERE name = ?4 AND runner_rnbo_version = ?3 GROUP BY name
		)
		ORDER BY sets_presets.created_at
		LIMIT 1
	)");

	query.bind(1, presetName);
	query.bind(2, patchername);
	query.bind(3, cur_rnbo_version);
	query.bind(4, setName);
	query.bind(5, instanceIndex);

	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string content(s);

		s = query.getColumn(1);
		std::string patcherPresetName(s);

		return { std::make_pair(content, patcherPresetName) };
	}

	return boost::none;
}

void DB::setPresetSave(
		const std::string& patchername,
		const std::string& presetName,
		const std::string& setName,
		unsigned int instanceIndex,
		const std::string& content,
		std::string patcherPresetName
) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		INSERT INTO sets_presets (patcher_id, set_id, name, set_instance_index, content, preset_name)
		SELECT patchers.id, sets.id, ?1, ?2, ?3, ?7
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
	if (patcherPresetName.size() > 0) {
		query.bind(7, patcherPresetName);
	} else {
		query.bind(7, nullptr);
	}
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

void DB::setPresetDestroyAll(
		const std::string& setName,
		std::string rnbo_version
) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		DELETE FROM sets_presets
		WHERE
		set_id IN (
			SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name
		)
	)");
	query.bind(1, setName);
	query.bind(2, rnbo_version);
	query.exec();
}

void DB::setSave(
		const std::string& name,
		const SetInfo& info
		) {
	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Transaction transaction(mDB);

	//only 1 set per name per version, simply update if one exists already
	int64_t id = 0;
	{
		SQLite::Statement query(mDB, "SELECT id FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		if (query.executeStep()) {
			id = query.getColumn(0);
		}
	}

	if (id == 0) {
		SQLite::Statement query(mDB, "INSERT INTO sets (name, runner_rnbo_version, filename, meta, created_at) VALUES (?1, ?2, ?3, ?4, CASE LENGTH(?5) WHEN 0 THEN datetime('now', 'localtime') ELSE ?5 END)");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		query.bind(3, "DB"); //no longer used
		query.bind(4, info.meta);
		query.bind(5, info.created_at);
		query.exec();
		id = mDB.getLastInsertRowid();
	} else {
		{
			SQLite::Statement query(mDB, "UPDATE sets SET meta = ?1 WHERE id = ?2");
			query.bind(1, info.meta);
			query.bind(2, id);
			query.exec();
		}
		{
			SQLite::Statement query(mDB, "DELETE FROM sets_connections WHERE set_id = ?1");
			query.bind(1, id);
			query.exec();
		}
		{
			SQLite::Statement query(mDB, "DELETE FROM sets_patcher_instances WHERE set_id = ?1");
			query.bind(1, id);
			query.exec();
		}
	}

	{
		//Prepared Statement
		SQLite::Statement query(mDB, R"(
			INSERT INTO sets_connections
			(set_id, source_name, source_instance_index, source_port_name, sink_name, sink_instance_index, sink_port_name)
			VALUES
			(?1, ?2, ?3, ?4, ?5, ?6, ?7)
			)");
		for (auto& c: info.connections) {
			query.bind(1, id);
			query.bind(2, c.source_name);
			query.bind(3, c.source_instance_index);
			query.bind(4, c.source_port_name);
			query.bind(5, c.sink_name);
			query.bind(6, c.sink_instance_index);
			query.bind(7, c.sink_port_name);

			query.exec();
			query.reset();
		}
	}

	{
		//Prepared Statement
		SQLite::Statement query(mDB, R"(
			INSERT INTO sets_patcher_instances
			(set_id, patcher_id, set_instance_index, config)
			SELECT ?1, MAX(id), ?3, ?4 FROM patchers
			WHERE name = ?2 AND runner_rnbo_version = ?5
			LIMIT 1
			)");
		for (auto& i: info.instances) {

			query.bind(1, id);
			query.bind(2, i.patcher_name);
			query.bind(3, static_cast<int>(i.instance_index));
			query.bind(4, i.config);
			query.bind(5, cur_rnbo_version);

			query.exec();
			query.reset();
		}
	}
	transaction.commit();
}

boost::optional<SetInfo> DB::setGet(
		const std::string& name,
		std::string rnbo_version
) {
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

	int64_t set_id = 0;
	SetInfo info;

	{
		SQLite::Statement query(mDB, "SELECT id, meta, created_at FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 ORDER BY created_at DESC LIMIT 1");
		query.bind(1, name);
		query.bind(2, rnbo_version);
		if (!query.executeStep()) {
			return boost::none;
		}

		set_id = query.getColumn(0);
		info.meta = getStringColumn(query, 1);
		info.created_at = getStringColumn(query, 2);
	}

	//instance index -> name
	std::unordered_map<int, std::string> instanceName;

	//get instances
	{
		SQLite::Statement query(mDB, R"(
			SELECT patchers.name, sets_patcher_instances.set_instance_index, sets_patcher_instances.config
			FROM sets_patcher_instances
			JOIN patchers ON sets_patcher_instances.patcher_id = patchers.id
			WHERE sets_patcher_instances.set_id = ?1
		)");
		query.bind(1, set_id);
		while (query.executeStep()) {
			std::string name = getStringColumn(query, 0);
			int index = query.getColumn(1);
			std::string config = getStringColumn(query, 2);

			//construct an instance name, may have changed if the patcher has been renamed
			instanceName.insert({ index, name + "-" + std::to_string(index) });

			info.instances.push_back(SetInstanceInfo(name, static_cast<unsigned int>(index), config));
		}
	}

	//get connections
	{
		SQLite::Statement query(mDB, R"(
			SELECT
			source_name, source_instance_index, source_port_name, sink_name, sink_instance_index, sink_port_name
			FROM sets_connections
			WHERE set_id = ?1
		)");
		query.bind(1, set_id);
		while (query.executeStep()) {

			SetConnectionInfo c;
			c.source_name = getStringColumn(query, 0);
			c.source_instance_index = query.getColumn(1);
			c.source_port_name = getStringColumn(query, 2);
			c.sink_name = getStringColumn(query, 3);
			c.sink_instance_index = query.getColumn(4);
			c.sink_port_name = getStringColumn(query, 5);

			//update names in case the patcher names have changed
			if (c.source_instance_index >= 0) {
				auto it = instanceName.find(c.source_instance_index);
				if (it != instanceName.end()) {
					c.source_name = it->second;
				}
			}

			if (c.sink_instance_index >= 0) {
				auto it = instanceName.find(c.sink_instance_index);
				if (it != instanceName.end()) {
					c.sink_name = it->second;
				}
			}

			info.connections.push_back(c);
		}
	}

	return info;
}

bool DB::setDestroy(const std::string& name)
{
	//presets get deleted because of on delete cascade
	{
		std::lock_guard<std::mutex> guard(mMutex);
		SQLite::Statement query(mDB, "DELETE FROM sets WHERE name=?1 AND runner_rnbo_version=?2");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		return query.exec() > 0;
	}
}

bool DB::setInitial(const std::string& name)
{
	//TODO can we do this in a single statement?
	{
		std::lock_guard<std::mutex> guard(mMutex); //setNameInitial also grabs mutex
		SQLite::Statement query(mDB, "UPDATE sets SET initial = CASE WHEN name == ?1 THEN 1 ELSE 0 END WHERE runner_rnbo_version=?2");
		query.bind(1, name);
		query.bind(2, cur_rnbo_version);
		query.exec();
	}
	if (name.size() > 0)
		return setNameInitial(cur_rnbo_version).has_value();
	return true;
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

bool DB::setMatchesConnections(const std::string& name, const std::vector<std::string>& source, const std::vector<std::vector<std::string>>& dest) {
	std::lock_guard<std::mutex> guard(mMutex);
	int setid = getsetid(mDB, name);

	std::set<std::string> destset;
	for (auto d: dest) {
		destset.insert(boost::algorithm::join(d, ":"));
	}

	{
		SQLite::Statement query(mDB, "SELECT sink_name, sink_port_name FROM sets_connections WHERE source_name=?1 AND source_port_name=?2 AND set_id=?3");
		query.bind(1, source[0]);
		if (source.size() > 1) {
			query.bind(2, source[1]);
		} else {
			query.bind(2, "");
		}
		query.bind(3, setid);
		while (query.executeStep()) {
			const char * s = query.getColumn(0);
			std::string v(s);

			s = query.getColumn(1);
			if (strlen(s) > 0) {
				v += ":" + std::string(s);
			}

			//if there isn't something to remove, we don't have a match
			if (destset.erase(v) == 0) {
				//std::cout << name << " didn't find " << v << std::endl;
				return false;
			}
		}
	}

	/*
		 for (auto v: destset) {
		 std::cout << name << " left over: " << v << std::endl;
		 }
		 */

	//if there is anything left over, we don't have a match
	return destset.size() == 0;
}

boost::optional<std::string> DB::setNameInitial(std::string rnbo_version)
{
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "SELECT name FROM sets WHERE runner_rnbo_version = ?1 AND initial = 1 ORDER BY name ASC LIMIT 1");
	query.bind(1, rnbo_version);

	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return { std::string(s) };
	}
	return boost::none;
}


boost::optional<std::string> DB::setNameByIndex(
		unsigned int index,
		std::string rnbo_version
)
{
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "SELECT name FROM sets WHERE runner_rnbo_version = ?1 ORDER BY name ASC LIMIT 1 OFFSET ?2");
	query.bind(1, rnbo_version);
	query.bind(2, static_cast<int>(index));
	if (query.executeStep()) {
		const char * s = query.getColumn(0);
		return { std::string(s) };
	}
	return boost::none;
}

void DB::sets(std::function<void(const std::string& name, const std::string& created, bool initial)> func, std::string rnbo_version)
{
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);

	SQLite::Statement query(mDB, R"(
		SELECT name, datetime(created_at), initial FROM sets
		WHERE id IN (SELECT MAX(id) FROM sets WHERE runner_rnbo_version = ?1 GROUP BY name) ORDER BY name, created_at DESC
	)");
	query.bind(1, rnbo_version);
	while (query.executeStep()) {
		const char * s = query.getColumn(0);
		std::string name(s);

		s = query.getColumn(1);
		std::string created_at(s);

		int initial = query.getColumn(2);

		func(name, created_at, initial != 0);
	}
}

std::vector<int> DB::setViewIndexes(const std::string& setName, std::string rnbo_version) {
	std::vector<int> indexes;
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, R"(
		SELECT view_index FROM sets_views
		WHERE set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
		ORDER BY sort_order ASC
		)"
	);
	query.bind(1, setName);
	query.bind(2, rnbo_version);
	while (query.executeStep()) {
		indexes.push_back(query.getColumn(0));
	}

	return indexes;
}

boost::optional<std::tuple<
	std::string,
	std::vector<std::string>,
	int
>> DB::setViewGet(const std::string& setname, int viewIndex, std::string rnbo_version) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (rnbo_version.size() == 0)
		rnbo_version = cur_rnbo_version;

	boost::optional<std::tuple<std::string, std::vector<std::string>, int>> item;

	int id = 0;
	std::string name;
	int sortOrder;

	{
		SQLite::Statement query(mDB, R"(
			SELECT id, name, sort_order FROM sets_views
			WHERE view_index = ?3
			AND set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
			)"
		);
		query.bind(1, setname);
		query.bind(2, rnbo_version);
		query.bind(3, viewIndex);

		if (query.executeStep()) {
			id = query.getColumn(0);
			name = getStringColumn(query, 1);
			sortOrder = query.getColumn(2);
		}
	}

	//get params
	if (id > 0) {
		SQLite::Statement query(mDB, R"(
		SELECT FORMAT("%i:%s", set_instance_index, param_id) FROM sets_views_params
		WHERE set_view_id = ?1 ORDER BY sort_order)");
		query.bind(1, id);

		std::vector<std::string> params;
		while (query.executeStep()) {
			params.push_back(getStringColumn(query, 0));
		}

		item = {{ name, params, sortOrder }};
	}


	return item;
}

int DB::setViewCreate(
		const std::string& setname,
		const std::string& viewname,
		const std::vector<ViewParam> params,
		int viewIndex
) {
	int set_id = 0;
	{
		std::lock_guard<std::mutex> guard(mMutex);
		set_id = getsetid(mDB, setname);
	}
	if (viewIndex < 0) {
		std::lock_guard<std::mutex> guard(mMutex);
		SQLite::Statement query(mDB, "SELECT MAX(view_index) + 1 FROM sets_views WHERE set_id = ?1");
		query.bind(1, set_id);

		if (query.executeStep()) {
			viewIndex = query.getColumn(0);
		}
	} else {
		setViewDestroy(setname, viewIndex);
	}

	{
		std::lock_guard<std::mutex> guard(mMutex);

		SQLite::Statement query(mDB, R"(
			INSERT INTO sets_views (set_id, view_index, params, name)
			VALUES(?1, ?2, "", ?3)
			)"
		);

		query.bind(1, set_id);
		query.bind(2, viewIndex);
		query.bind(3, viewname);
		query.exec();
	}

	int set_view_id = mDB.getLastInsertRowid();
	{
		std::lock_guard<std::mutex> guard(mMutex);
		insertSetViewParams(mDB, set_id, set_view_id, params);
	}

	return viewIndex;
}

void DB::setViewDestroy(
		const std::string& setname,
		int viewIndex
) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (viewIndex < 0) {
		SQLite::Statement query(mDB, R"(
			DELETE FROM sets_views
			WHERE set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
			)"
		);
		query.bind(1, setname);
		query.bind(2, cur_rnbo_version);
		query.exec();
	} else {
		SQLite::Statement query(mDB, R"(
			DELETE FROM sets_views
			WHERE view_index = ?3
			AND set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
			)"
		);
		query.bind(1, setname);
		query.bind(2, cur_rnbo_version);
		query.bind(3, viewIndex);
		query.exec();
	}
}

void DB::setViewUpdateParams(
		const std::string& setname,
		int viewIndex,
		const std::vector<ViewParam> params
) {
	std::lock_guard<std::mutex> guard(mMutex);

	int set_id = getsetid(mDB, setname);
	if (set_id < 1)
		return;

	int set_view_id = getsetviewid(mDB, set_id, viewIndex);
	if (set_view_id < 1)
		return;

	SQLite::Transaction transaction(mDB);
	SQLite::Statement query(mDB, "DELETE FROM sets_views_params WHERE set_view_id = ?1");
	query.bind(1, set_view_id);
	query.exec();

	insertSetViewParams(mDB, set_id, set_view_id, params);
	transaction.commit();
}

void DB::setViewUpdateName(
		const std::string& setname,
		int viewIndex,
		const std::string& name
) {
	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, R"(
		UPDATE sets_views
		SET name = ?4
		WHERE view_index = ?3
		AND set_id IN (SELECT MAX(id) FROM sets WHERE name = ?1 AND runner_rnbo_version = ?2 GROUP BY name)
		)"
	);
	query.bind(1, setname);
	query.bind(2, cur_rnbo_version);
	query.bind(3, viewIndex);
	query.bind(4, name);

	query.exec();
}

bool DB::setViewsUpdateSortOrder(
		const std::string& setname,
		std::vector<int>& indexes
) {
	std::lock_guard<std::mutex> guard(mMutex);
	int setid = getsetid(mDB, setname);
	if (setid > 0) {
		std::vector<std::string> orderings;
		for (auto i: indexes) {
			orderings.push_back("view_index=" + std::to_string(i) + " DESC");
		}

		std::string s =
		R"(
			UPDATE sets_views
			SET sort_order = q.sort_order
			FROM
				(
					SELECT
						ROW_NUMBER() OVER (REPL) AS sort_order,
						id
					FROM
						sets_views
					WHERE
						set_id = ?1
				) AS q
			WHERE
				sets_views.id = q.id
			)";

		std::string ordering;
		if (orderings.size()) {
			ordering = "ORDER BY " + boost::algorithm::join(orderings, ",");
		}

		auto pos = s.find("REPL");
		s.replace(pos, 4, ordering);

		{
			SQLite::Statement query(mDB, s);
			query.bind(1, setid);
			query.exec();
		}

		//test to see if we got the sort order we expected
		{
			SQLite::Statement query(mDB, "SELECT view_index FROM sets_views WHERE set_id = ?1 ORDER BY sort_order");
			query.bind(1, setid);
			std::vector<int> result;
			while (query.executeStep()) {
				result.push_back(query.getColumn(0));
			}

			if (result == indexes) {
				return false;
			}
			indexes = result;
			return true;
		}

	} else {
		indexes.clear();
		return true;
	}
}

void DB::setViewsCopy(const std::string& srcSetName, const std::string& dstSetName) {
	std::lock_guard<std::mutex> guard(mMutex);
	int srcid = getsetid(mDB, srcSetName);
	int dstid = getsetid(mDB, dstSetName);
	if (srcid > 0 && dstid > 0) {
		//delete existing views
		{
			SQLite::Statement query(mDB, "DELETE FROM sets_views WHERE set_id = ?1");
			query.bind(1, dstid);
			query.exec();
		}
		{
			SQLite::Statement query(mDB, R"(
				INSERT INTO sets_views
					(set_id, view_index, name, sort_order, params)
					SELECT ?2, view_index, name, sort_order, params FROM sets_views WHERE set_id = ?1
				)"
			);
			query.bind(1, srcid);
			query.bind(2, dstid);
			query.exec();
		}
		{
			SQLite::Statement query(mDB, R"(
				INSERT OR IGNORE INTO sets_views_params
					(set_view_id, sort_order, set_instance_index, param_id, patcher_id)
					SELECT ?2, sort_order, set_instance_index, param_id, patcher_id
					FROM sets_views_params WHERE set_view_id = ?1
				)"
			);
			query.bind(1, srcid);
			query.bind(2, dstid);
			query.exec();
		}
	}
}

bool DB::listenerExists(const std::string& ip, uint16_t port)
{
	std::lock_guard<std::mutex> guard(mMutex);
	SQLite::Statement query(mDB, "SELECT COUNT(*) from listeners WHERE ip = ?1 AND port = ?2");
	query.bind(1, ip);
	query.bind(2, port);
	if (query.executeStep()) {
		int cnt = query.getColumn(0);
		return cnt > 0;
	}
	return false;
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

RNBO::Json SetConnectionInfo::toJson() {
	return {
		{"source_name", source_name},
		{"source_instance_index", source_instance_index},
		{"source_port_name", source_port_name},
		{"sink_name", sink_name},
		{"sink_instance_index", sink_instance_index},
		{"sink_port_name", sink_port_name}
	};
}

SetConnectionInfo SetConnectionInfo::fromJson(const RNBO::Json& json) {
	//XXX assumes fully valid data
	SetConnectionInfo info(
		json["source_name"].get<std::string>(),
		json["source_port_name"].get<std::string>(),
		json["sink_name"].get<std::string>(),
		json["sink_port_name"].get<std::string>()
	);

	info.source_instance_index = json["source_instance_index"].get<int>();
	info.sink_instance_index = json["sink_instance_index"].get<int>();

	return info;
}

RNBO::Json SetInstanceInfo::toJson() {
	return {
		{ "patcher_name", patcher_name },
		{ "instance_index", static_cast<int>(instance_index) },
		{ "config", RNBO::Json::parse(config) }
	};
}

SetInstanceInfo SetInstanceInfo::fromJson(const RNBO::Json& json) {
	//XXX assumes fully valid data
	return SetInstanceInfo(
			json["patcher_name"].get<std::string>(),
			static_cast<unsigned int>(json["instance_index"].get<int>()),
			json["config"].dump()
	);
}

RNBO::Json SetInfo::toJson() {
	RNBO::Json inst = RNBO::Json::array();
	RNBO::Json conn = RNBO::Json::array();

	for (auto& i: instances) {
		inst.push_back(i.toJson());
	}

	for (auto& i: connections) {
		conn.push_back(i.toJson());
	}

	RNBO::Json m = RNBO::Json::object();
	if (meta.size()) {
		try {
			m = RNBO::Json::parse(meta);
		} catch (...) {
			std::cerr << "failed to convert string meta to json";
		}
	}

	return {
		{"set_info_version", 2},
		{"created_at", created_at},
		{"meta", m},
		{"instances", inst},
		{"connections", conn}
	};
}

SetInfo SetInfo::fromJson(const RNBO::Json& json) {
	SetInfo info;
	//test for key
	if (json.contains("set_info_version")) {
		if (json["set_info_version"].get<int>() == 2) {
			if (json.contains("meta") && json["meta"].is_object()) {
				info.meta = json["meta"].dump();
			}
			for (auto& i: json["instances"]) {
				info.instances.push_back(SetInstanceInfo::fromJson(i));
			}
			for (auto& i: json["connections"]) {
				info.connections.push_back(SetConnectionInfo::fromJson(i));
			}
			if (json.contains("created_at") && json["created_at"].is_string()) {
				info.created_at = json["created_at"].get<std::string>();
			}
		} else {
			//invalid
			std::cerr << "unknown set_info_version: " << json["set_info_version"].get<int>() << std::endl;
		}
	} else {
		//old format
		if (json.contains("meta") && json["meta"].is_string()) {
			info.meta = json["meta"].get<std::string>();
		}

		//get index map
		std::unordered_map<std::string, int> instanceNameToIndex;

		if (json.contains("instances") && json["instances"].is_array()) {
			for (auto& i: json["instances"]) {
				std::string conf = "{}";
				if (i.contains("config") && i["config"].is_object()) {
					conf = i["config"].dump();
				}

				int index = i["index"].get<int>();
				SetInstanceInfo inst(
					i["name"].get<std::string>(),
					static_cast<unsigned int>(index),
					conf);
				instanceNameToIndex.insert( { inst.patcher_name + "-" + std::to_string(index), index });
				info.instances.push_back(inst);
			}
		}
		if (json.contains("connections") && json["connections"].is_object()) {
			auto cleanup_name_info = [](const std::string& name) -> std::vector<std::string> {
				std::vector<std::string> info;
				boost::algorithm::split(info, name, boost::is_any_of(":"));
				//add an empty entry if there is only 1 entry
				if (info.size() == 1) {
					info.push_back("");
				} else {
					//if there are more than 2 entries, build them back into 2
					for (auto j = 2; j < info.size(); j++) {
						info[1] += ":" + info[j];
					}
				}
				return info;
			};

			for (auto& kv: json["connections"].items()) {
				std::string name = kv.key();

				std::vector<std::string> src_info = cleanup_name_info(name);
				std::string source_name = src_info[0], source_port_name = src_info[1];

				int source_instance_index = -1;
				{
					auto it = instanceNameToIndex.find(source_name);
					if (it != instanceNameToIndex.end()) {
						source_instance_index = it->second;
					}
				}

				auto entry = kv.value();
				if (entry.is_object() && entry.contains("output") && entry["output"].is_boolean() && entry["output"].get<bool>()) {
					if (entry.contains("connections") && entry["connections"].is_array()) {
						for (auto c: entry["connections"]) {
							if (c.is_string()) {
								name = c.get<std::string>();
								std::vector<std::string> sink_info = cleanup_name_info(name);
								std::string sink_name = sink_info[0], sink_port_name = sink_info[1];
								int sink_instance_index = -1;
								{
									auto it = instanceNameToIndex.find(sink_name);
									if (it != instanceNameToIndex.end()) {
										sink_instance_index = it->second;
									}
								}

								SetConnectionInfo conn(source_name, source_port_name, sink_name, sink_port_name);
								conn.source_instance_index = source_instance_index;
								conn.sink_instance_index = sink_instance_index;
								info.connections.push_back(conn);
							}
						}
					}
				}
			}
		}
	}
	return info;
}
