#include "sqlite.h"
#include <sqlite3.h>
#include <limits>
#include <exception>

void sqlite::db::close(void* db)
{
	// Outstanding statements retain the connection until they are finalized.
	sqlite3_close_v2(static_cast<sqlite3*>(db));
}

sqlite::db::db(const std::string& memoryName, bool shared)
	: m_DB(nullptr, &db::close)
{
	int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY;
	if (shared)
		flags |= SQLITE_OPEN_SHAREDCACHE;
	else
		flags |= SQLITE_OPEN_PRIVATECACHE;
	
	sqlite3* db = nullptr;
	const auto ec = sqlite3_open_v2(memoryName.c_str(), &db, flags, nullptr);
	m_DB.reset(db);
	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

sqlite::db::db(const std::filesystem::path& dbFile)
	: m_DB(nullptr, &db::close)
{
	sqlite3* db = nullptr;
	const auto filename = dbFile.u8string();
	const auto ec = sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()), &db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	m_DB.reset(db);
	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

void sqlite::db::exec(const std::string& sql)
{
	sqlite3* db = reinterpret_cast<sqlite3*>(m_DB.get());
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

sqlite::db::~db()
{
	// A statement may outlive this wrapper; do not retain callbacks to it.
	// Avoid expiring statements when no authorizer was installed.
	if (m_Authorizer)
		sqlite3_set_authorizer(static_cast<sqlite3*>(m_DB.get()), nullptr, nullptr);
}

void sqlite::db::exec(const std::string& sql, std::function<bool(const std::map<std::string_view, std::string_view>&)> handler)
{
	struct executor_t
	{
		static int exec(void* _self, int numColumns, char** columnValues, char** columnNames) noexcept
		{
			auto& self = *static_cast<executor_t*>(_self);
			try {
				// SQLite owns these views only for this callback, including the names.
				std::map<std::string_view, std::string_view> row;
				for (int i = 0; i < numColumns; i++)
					row.emplace(columnNames[i], columnValues[i] ? columnValues[i] : "");
				return self.m_Handler(row);
			}
			catch (...) {
				self.exception = std::current_exception();
				return 1;
			}
		}

		decltype(handler)& m_Handler;
		std::exception_ptr exception;
	} executor{ handler, {} };

	auto db = reinterpret_cast<sqlite3*>(m_DB.get());
	const auto ec = sqlite3_exec(db, sql.c_str(), &executor_t::exec, &executor, nullptr);
	if (executor.exception)
		std::rethrow_exception(executor.exception);
	if (ec != SQLITE_OK && ec != SQLITE_ABORT)
		throw sqlite::error{ sqlite3_errmsg(db) };
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
			return sqlite3_set_authorizer(db, [](void* _self, int actionCode, const char* detail1, const char* detail2, const char* detail3, const char* detail4) noexcept {
				auto self = reinterpret_cast<sqlite::db*>(_self);
				try {
					auto res = self->m_Authorizer(static_cast<auth_action>(actionCode), detail1 ? detail1 : "", detail2 ? detail2 : "", detail3 ? detail3 : "", detail4 ? detail4 : "");
					return static_cast<int>(res);
				}
				catch (...) {
					return SQLITE_DENY; // Never unwind through SQLite's C callbacks.
				}
			}, this);
		else
			return sqlite3_set_authorizer(db, nullptr, nullptr);
	}();

	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
}

void sqlite::stmt::finalize(void* stmt)
{
	sqlite3_finalize(static_cast<sqlite3_stmt*>(stmt));
}

std::size_t sqlite::stmt::columns()
{
	return static_cast<std::uint32_t>(sqlite3_column_count(reinterpret_cast<sqlite3_stmt*>(m_Stmt.get())));
}

std::string_view sqlite::stmt::column_name(std::size_t pos)
{
	if (pos >= columns())
		throw std::out_of_range{ "column index out of range" };
	const auto name = sqlite3_column_name(static_cast<sqlite3_stmt*>(m_Stmt.get()), static_cast<int>(pos));
	return name ? name : "";
}

sqlite::stmt::stmt_ptr_t sqlite::stmt::prepare(void* _db, const std::string_view& sql)
{
	if (sql.length() > std::numeric_limits<int>::max())
		throw std::length_error{ "sql query too large" };

	auto db = reinterpret_cast<sqlite3*>(_db);
	sqlite3_stmt* stmt = nullptr;
	int RC = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.length()), &stmt, nullptr);
	auto result = stmt_ptr_t{ stmt, &stmt::finalize };
	if (RC != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(db) };
	if (!stmt)
		throw sqlite::error{ "SQL statement is empty" };

	return result;
}

void sqlite::stmt::reset()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_reset(stmt);
	if (ec != SQLITE_OK)
		throw sqlite::error{ sqlite3_errmsg(sqlite3_db_handle(stmt)) };
}

void sqlite::stmt::insert()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_step(stmt);
	if (ec != SQLITE_DONE)
		throw sqlite::error{ sqlite3_errmsg(sqlite3_db_handle(stmt)) };
}

bool sqlite::stmt::query()
{
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_step(stmt);
	if (ec == SQLITE_ROW)
		return true;
	else if (ec == SQLITE_DONE)
		return false;
	
	throw sqlite::error{ sqlite3_errmsg(sqlite3_db_handle(stmt)) };
}

void sqlite::stmt::bind_at(std::size_t pos, const std::string_view& str)
{
	if (pos > std::numeric_limits<int>::max())
		throw std::overflow_error{ "column_at pos out of range" };

	if (str.length() > std::numeric_limits<int>::max())
		throw std::length_error{ "string too large to bind" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	int ec = sqlite3_bind_text(stmt, static_cast<int>(pos), str.empty() ? "" : str.data(), static_cast<int>(str.length()), SQLITE_TRANSIENT);
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
	if (pos >= columns())
		throw std::out_of_range{ "column index out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	auto text = sqlite3_column_text(stmt, static_cast<int>(pos));
	return reinterpret_cast<const char*>(text);
}

std::string_view sqlite::stmt::column_text_view(std::size_t pos)
{
	const auto text = column_text(pos);
	if (!text)
		return {};
	const auto size = sqlite3_column_bytes(static_cast<sqlite3_stmt*>(m_Stmt.get()), static_cast<int>(pos));
	return { text, static_cast<std::size_t>(size) };
}

std::int32_t sqlite::stmt::column_int(std::size_t pos)
{
	if (pos >= columns())
		throw std::out_of_range{ "column index out of range" };
		
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_int(stmt, static_cast<int>(pos));
}

std::int64_t sqlite::stmt::column_int64(std::size_t pos)
{
	if (pos >= columns())
		throw std::out_of_range{ "column index out of range" };
		
	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_int64(stmt, static_cast<int>(pos));
}

double sqlite::stmt::column_double(std::size_t pos)
{
	if (pos >= columns())
		throw std::out_of_range{ "column index out of range" };

	auto stmt = reinterpret_cast<sqlite3_stmt*>(m_Stmt.get());
	return sqlite3_column_double(stmt, static_cast<int>(pos));
}
