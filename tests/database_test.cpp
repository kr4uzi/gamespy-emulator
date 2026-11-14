#include "asio.h"
#include "playerdb.sqlite.h"
#include "sqlite.h"
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error{ message };
	}

	template<class Exception, class Function>
	void throws(Function function)
	{
		try { function(); }
		catch (const Exception&) { return; }
		throw std::runtime_error{ "expected exception was not thrown" };
	}

	void statements()
	{
		sqlite::db db{ ":memory:", false };
		db.exec("CREATE TABLE data (value TEXT UNIQUE NOT NULL)");
		const std::string text{ "with\0zero", 9 };
		sqlite::stmt insert{ db, "INSERT INTO data VALUES (?)", std::string{ text } };
		insert.insert(); // A temporary bound string must survive construction.
		insert.reset();
		insert.bind(std::string_view{});
		insert.insert(); // An empty view must bind empty text, not SQL NULL.

		sqlite::stmt query{ db, "SELECT value FROM data ORDER BY rowid" };
		require(query.query(), "missing first row");
		require(query.column_at<std::string>(0) == text, "embedded NUL was truncated");
		require(query.column_name(0) == "value", "wrong column name");
		throws<std::out_of_range>([&] { query.column_name(1); });
		throws<std::out_of_range>([&] { query.column_int(1); });
		require(query.query(), "missing empty-string row");
		require(query.column_at<std::string>(0).empty(), "empty string did not round-trip");
		require(!query.query(), "unexpected row");

		insert.reset();
		insert.bind(text);
		throws<sqlite::error>([&] { insert.insert(); });
		throws<sqlite::error>([&] { insert.reset(); });
		insert.bind(std::string{ "after constraint" });
		insert.insert(); // sqlite3_reset resets even when reporting a prior error.

		sqlite::stmt failure{ db, "SELECT abs(-9223372036854775808)" };
		throws<sqlite::error>([&] { failure.query(); });
		throws<sqlite::error>([&] { sqlite::stmt empty{ db, "   " }; });
	}

	void callbacks()
	{
		sqlite::db db{ ":memory:", false };
		int rows = 0;
		db.exec("SELECT NULL AS first; SELECT 'ok' AS second", [&](const auto& row) {
			require(row.size() == 1, "columns leaked between SQL statements");
			if (rows++ == 0)
				require(row.at("first").empty(), "NULL callback value was not empty");
			else
				require(row.at("second") == "ok", "wrong second statement value");
			return false;
		});
		require(rows == 2, "not all callbacks ran");

		for (int i = 0; i < 32; ++i)
			db.exec("SELECT 1 UNION ALL SELECT 2", [](const auto&) { return true; });
		throws<std::logic_error>([&] {
			db.exec("SELECT 1", [](const auto&) -> bool { throw std::logic_error{ "callback failure" }; });
		});
		throws<sqlite::error>([&] { db.exec("SELECT * FROM missing_table", [](const auto&) { return false; }); });

		{
			auto guard = db.set_scoped_authorizer([](auto...) -> sqlite::auth_res {
				throw std::logic_error{ "authorizer failure" };
			});
			throws<sqlite::error>([&] { db.exec("SELECT 1"); });
		}
		db.exec("SELECT 1");
	}

	void ownership()
	{
		std::optional<sqlite::stmt> statement;
		{
			sqlite::db db{ ":memory:", false };
			statement.emplace(db, "SELECT 'alive'");
		}
		require(statement->query(), "statement lost its connection");
		require(statement->column_at<std::string>(0) == "alive", "statement used a destroyed connection");
		statement.reset();

		const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		const auto path = std::filesystem::temp_directory_path() / ("gamespy-db-test-" + suffix);
		for (int i = 0; i < 32; ++i)
			throws<sqlite::error>([&] { sqlite::db missing{ path / "missing" / "players.sqlite3" }; });
	}

	gamespy::task<void> players()
	{
		gamespy::PlayerDBSQLite db{ ":memory:" };
		co_await db.Connect();
		require(!co_await db.HasPlayer("missing"), "unexpected existing player");
		require(!(co_await db.GetPlayerByName("missing")), "missing player returned a value");
		require(!(co_await db.GetPlayerByPID(1)), "missing PID returned a value");

		gamespy::PlayerData player{ "O'Brien", "Player@Example.Test", "pass'word", "xx" };
		co_await db.CreatePlayer(player);
		require(player.id == 2900001, "initial profile ID changed");
		require(co_await db.HasPlayer(player.name), "created player was not found");
		auto stored = co_await db.GetPlayerByPID(player.id);
		require(stored && stored->name == player.name && stored->password == player.password, "player data did not round-trip");

		bool duplicateRejected = false;
		try { co_await db.CreatePlayer(player); }
		catch (const sqlite::error&) { duplicateRejected = true; }
		require(duplicateRejected, "duplicate player was accepted");
		player.country = "de";
		co_await db.UpdatePlayer(player);
		auto matches = co_await db.GetPlayerByMailAndPassword("player@example.test", player.password);
		require(matches.size() == 1 && matches.front().country == "de", "player update did not persist");
		require((co_await db.GetPlayerByMailAndPassword(player.email, "wrong")).empty(), "wrong password matched");
		co_await db.Disconnect();
	}
}

int main()
{
	try {
		statements();
		callbacks();
		ownership();
		boost::asio::io_context context;
		std::exception_ptr failure;
		boost::asio::co_spawn(context, players(), [&](std::exception_ptr error) { failure = error; });
		context.run();
		if (failure)
			std::rethrow_exception(failure);
		std::cout << "database tests passed\n";
	}
	catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
