#include "sqlite.h"
#include <sqlite3.h>
#include <limits>
#include <span>
#include <print>
#include <type_traits>

void sqlite::db::close(void* db)
{
	sqlite3_close(reinterpret_cast<sqlite3*>(db));
}

sqlite::db::db(const std::string& memoryName, bool shared)
	: m_DB(nullptr, &db::close)
{
	int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY;
	if (shared)
		flags |= SQLITE_OPEN_SHAREDCACHE;
	else
		flags |= SQLITE_OPEN_PRIVATECACHE;
	
	sqlite3* db;
	if (sqlite3_open_v2(memoryName.c_str(), &db, flags, nullptr) != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };

	m_DB.reset(db);
}

sqlite::db::db(const std::filesystem::path& dbFile)
	: m_DB(nullptr, &db::close)
{
	sqlite3* db;
	auto ec = [&]() {
		if constexpr (std::is_same_v<std::filesystem::path::string_type, std::wstring>)
			return sqlite3_open16(dbFile.c_str(), &db);
		else
			return sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	}();

	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };

	m_DB.reset(db);
}

void sqlite::db::exec(const std::string& sql)
{
	sqlite3* db = reinterpret_cast<sqlite3*>(m_DB.get());
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

void sqlite::db::exec(const std::string& sql, std::function<bool(const std::map<std::string_view, std::string_view>&)> handler)
{
	struct executor_t
	{
		static int exec(void* _self, int numColumns, char** columnValues, char** columNames)
		{
			auto self = reinterpret_cast<executor_t*>(_self);
			return self->_exec(numColumns, columnValues, columNames);
		}

		bool _exec(int numColumns, const char* const* columnValues, const char* const* columnNames)
		{
			if (m_Row.empty()) {
				for (int i = 0; i < numColumns; i++)
					m_Row.emplace(columnNames[i], columnValues[i]);
			}
			else {
				for (int i = 0; i < numColumns; i++)
					m_Row[columnNames[i]] = columnValues[i];
			}

			// handler will return true ("non-zero") which will lead to sqlite3_exec returning SQLITE_ABORT
			return m_handler(m_Row);
		}

		decltype(handler) m_handler;
		std::map<std::string_view, std::string_view> m_Row;
	} executor{ handler };

	auto db = reinterpret_cast<sqlite3*>(m_DB.get());
	char* error{ nullptr };
	int ec = sqlite3_exec(db, sql.c_str(), &executor_t::exec, &executor, &error);
	if (ec != SQLITE_OK && ec != SQLITE_ABORT) {
		std::string errorMessage{ error };
		sqlite3_free(error);
		throw sqlite::error{ errorMessage };
	}
}

sqlite::db::rowid_t sqlite::db::last_insert_rowid() noexcept
{
	auto db = reinterpret_cast<sqlite3*>(m_DB.get());
	return sqlite3_last_insert_rowid(db);
}

void sqlite::db::set_authorizer(decltype(m_Authorizer) authorizer)
{
	m_Authorizer = authorizer;

	auto db = reinterpret_cast<sqlite3*>(m_DB.get());
	int ec = [&]() {
		if (m_Authorizer)
			return sqlite3_set_authorizer(db, [](void* _self, int actionCode, const char* detail1, const char* detail2, const char* detail3, const char* detail4) {
				auto self = reinterpret_cast<sqlite::db*>(_self);
				auto res = self->m_Authorizer(static_cast<auth_action>(actionCode), detail1 ? detail1 : "", detail2 ? detail2 : "", detail3 ? detail3 : "", detail4 ? detail4 : "");
				return static_cast<int>(res);
			}, this);
		else
			return sqlite3_set_authorizer(db, nullptr, nullptr);
	}();

	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

void sqlite::stmt::finalize(void* stmt)
{
	int ec = sqlite3_finalize(reinterpret_cast<sqlite3_stmt*>(stmt));
	if (ec != SQLITE_OK) {
		//std::println("[sqlite3_finalize failed {}", ec);
		// not throwing here because this is usually called in a destructor
	}
}

std::size_t sqlite::stmt::columns()
{
	return static_cast<std::uint32_t>(sqlite3_column_count(reinterpret_cast<sqlite3_stmt*>(m_Stmt.get())));
}

std::string_view sqlite::stmt::column_name(std::size_t pos)
{
	return sqlite3_column_name(reinterpret_cast<sqlite3_stmt*>(m_Stmt.get()), pos);
}

sqlite::stmt::stmt_ptr_t sqlite::stmt::prepare(void* _db, const std::string_view& sql)
{
	if (sql.length() > std::numeric_limits<int>::max())
		throw std::length_error{ "sql query too large" };

	auto db = reinterpret_cast<sqlite3*>(_db);
	sqlite3_stmt* stmt;
	int RC = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.length()), &stmt, nullptr);
	if (RC != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };

	return stmt_ptr_t{ stmt, &stmt::finalize };
}

void sqlite::stmt::reset()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_reset(stmt);
	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_DB)) };
}

void sqlite::stmt::insert()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_step(stmt);
	if (ec != SQLITE_DONE)
		throw sqlite::error{ sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_DB)) };
}

bool sqlite::stmt::query()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_step(stmt);
	if (ec == SQLITE_ROW)
		return true;
	else if (ec == SQLITE_DONE)
		return false;
	
	throw sqlite::error{ sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_DB)) };
}

void sqlite::stmt::bind_at(std::size_t pos, const std::string& str)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	if (str.length() > std::numeric_limits<int>::max())
		throw std::length_error{ "string too large to bind" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_bind_text(stmt, static_cast<int>(pos), str.data(), static_cast<int>(str.length()), SQLITE_STATIC);
	if (ec != SQLITE_OK)
		throw std::runtime_error{ "Failed to bind text" };
}

void sqlite::stmt::bind_at(std::size_t pos, const std::string_view& str)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	if (str.length() > std::numeric_limits<int>::max())
		throw std::length_error{ "string too large to bind" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_bind_text(stmt, static_cast<int>(pos), str.data(), static_cast<int>(str.length()), SQLITE_STATIC);
	if (ec != SQLITE_OK)
		throw std::runtime_error{ "Failed to bind text" };
}

void sqlite::stmt::bind_at(std::size_t pos, std::int32_t val)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_bind_int(stmt, static_cast<int>(pos), val);
	if (ec != SQLITE_OK)
		throw std::runtime_error{ "Failed to bind int" };
}

void sqlite::stmt::bind_at(std::size_t pos, std::int64_t val)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_bind_int64(stmt, static_cast<int>(pos), val);
	if (ec != SQLITE_OK)
		throw std::runtime_error{ "Failed to bind int" };
}


const char* sqlite::stmt::column_text(std::size_t pos)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	auto text = sqlite3_column_text(stmt, static_cast<int>(pos));
	return reinterpret_cast<const char*>(text);
}

std::uint32_t sqlite::stmt::colum_int(std::size_t pos)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };
		
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_int(stmt, static_cast<int>(pos));
}

std::uint64_t sqlite::stmt::colum_int64(std::size_t pos)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };
		
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_int64(stmt, static_cast<int>(pos));
}

double sqlite::stmt::column_double(std::size_t pos)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_double(stmt, static_cast<int>(pos));
}
