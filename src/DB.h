#pragma once

#include <SQLiteCpp/SQLiteCpp.h>

class DB {
	public:
		DB();
		~DB();
	private:
		SQLite::Database mDB;
};
