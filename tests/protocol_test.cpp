#include "game.h"
#include "gamedb.h"
#include "qr.h"
#include "sapphire.h"
#include "sb_request.h"
#include "../bf2-dlls/download_path.h"
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace gamespy;
using Bytes = std::vector<std::uint8_t>;

namespace {
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	template<class T>
	T run(task<T> operation)
	{
		boost::asio::io_context context;
		auto result = boost::asio::co_spawn(context, std::move(operation), boost::asio::use_future);
		context.run();
		return result.get();
	}

	void string(Bytes& bytes, std::string_view text)
	{
		bytes.append_range(text);
		bytes.push_back(0);
	}

	void integer(Bytes& bytes, std::uint32_t value)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	void heartbeat_test()
	{
		Bytes bytes{ 3, 1, 2, 3, 4 };
		string(bytes, "gamename");
		string(bytes, "gmtest");
		string(bytes, "");
		const auto serverOnlySize = bytes.size();
		auto packet = QRPacket::Parse(bytes);
		require(packet && QRHeartbeatPacket::Parse(*packet).has_value(), "server-only heartbeat");

		bytes.append_range(std::array<std::uint8_t, 2>{ 0, 2 });
		string(bytes, "player_");
		string(bytes, "score_");
		string(bytes, "");
		string(bytes, ""); // An empty player name is a value, not another header terminator.
		string(bytes, "5");
		string(bytes, "Bob");
		string(bytes, "10");
		bytes.append_range(std::array<std::uint8_t, 2>{ 0, 1 });
		string(bytes, "team_t");
		string(bytes, "");
		string(bytes, "Blue");
		auto tablePacket = QRPacket::Parse(bytes);
		require(tablePacket.has_value(), "heartbeat packet");
		auto parsed = QRHeartbeatPacket::Parse(*tablePacket);
		require(parsed.has_value(), "heartbeat tables");
		require(parsed->playerValues.size() == 2 && parsed->playerValues[0][0].empty(), "empty player value");
		require(parsed->playerValues[1][1] == "10" && parsed->teamValues[0][0] == "Blue", "table values");

		for (std::size_t size = 0; size < bytes.size(); ++size) {
			if (size == serverOnlySize) continue;
			auto truncated = QRPacket::Parse(std::span(bytes).first(size));
			require(!truncated || !QRHeartbeatPacket::Parse(*truncated), "truncated heartbeat accepted");
		}

		bytes.resize(serverOnlySize);
		bytes.append_range(std::array<std::uint8_t, 6>{ 255, 255, 0, 255, 255, 0 });
		auto keylessPacket = QRPacket::Parse(bytes);
		auto keyless = QRHeartbeatPacket::Parse(*keylessPacket);
		require(keyless && keyless->playerValues.empty() && keyless->teamValues.empty(), "keyless tables must not allocate count rows");
		require(!QRPacket::Parse(Bytes{ 9, 0, 0, 0, 0 }), "empty availability payload");
		require(!QRPacket::Parse(Bytes{ 1, 0, 0, 0, 0 }), "empty challenge payload");
		require(QRPacket::Parse(Bytes{ 8, 0, 0, 0, 0 }).has_value(), "header-only keepalive");
	}

	void integer_le(Bytes& bytes, std::uint32_t value)
	{
		for (int shift = 0; shift < 32; shift += 8)
			bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}

	// GameSpy/serverbrowsing/sb_serverlist.c: SBServerListConnectAndQuery.
	Bytes list_request(std::uint32_t flags, std::string_view fields = "\\hostname\\numplayers")
	{
		Bytes bytes{ 1, 3 };
		integer_le(bytes, 0xFEDCBA98);
		string(bytes, "target");
		string(bytes, "source");
		bytes.append_range(std::array<std::uint8_t, 8>{ 0, 1, 2, 3, 4, 5, 6, 7 });
		string(bytes, "");
		string(bytes, fields);
		integer(bytes, flags);
		if (flags & 8) integer(bytes, 0xC0000201);
		if (flags & 128) integer_le(bytes, 500);
		return bytes;
	}

	void browser_test()
	{
		auto bytes = list_request(1 | 8 | 128);
		auto parsed = ServerListRequest::Parse(bytes);
		require(parsed.has_value(), "combined browser options");
		require(parsed->toGame == "target" && parsed->fromGame == "source", "query-for before query-from");
		const auto emptyRequest = list_request(2, "");
		auto emptyFields = ServerListRequest::Parse(emptyRequest);
		require(emptyFields && emptyFields->fieldList.empty(), "empty field list is supported by SDK");
		require(parsed->fromGameVersion == 0xFEDCBA98, "little-endian game version");
		require(parsed->alternateSourceIP->to_uint() == 0xC0000201 && parsed->limitResultCount == 500, "optional browser integers");
		for (std::size_t size = 0; size < bytes.size(); ++size)
			require(!ServerListRequest::Parse(std::span(bytes).first(size)), "truncated browser request");
		require(!ServerListRequest::Parse(list_request(16)), "reserved browser option");
		bytes.insert(bytes.begin(), 0xFF);
		require(ServerListRequest::Parse(std::span(bytes).subspan(1)).has_value(), "unaligned browser request");
	}

	void game_ownership_test()
	{
		Game game{ GameData{
			.name = "protocol-test", .secretKey = "secret",
			.keys = { { "numplayers", GameData::GameKey::Send::as_byte, GameData::GameKey::Store::as_integer } },
			.misssingKeyPolicy = GameData::MissingKeyPolicy::add_as_string
		} };
		run(game.Connect());
		for (int i = 0; i < 32; ++i) {
			const auto key = "dynamic_key_" + std::to_string(i);
			Game::IncomingServer incoming{
				.public_ip = "127.0.0.1", .public_port = 6500,
				.data = { { "numplayers", "5" }, { key, "value" } }
			};
			run(game.AddOrUpdateServer(incoming));
			require(game.GetParamSendType("numplayers") == GameData::GameKey::Send::as_byte, "metadata after vector growth");
			auto rows = run(game.GetServers("", { key }, 10));
			require(rows.size() == 1 && rows[0].data.at(key) == "value", "first value of automatic key");
		}

		std::vector<Game::SavedServer> saved;
		{
			std::string transientField = "dynamic_key_31";
			saved = run(game.GetServers("", { transientField }, 10));
			transientField.assign(1000, 'x');
		}
		require(saved[0].data.at("dynamic_key_31") == "value", "saved rows must own field names");
		require(!Game::IsValidParamName(std::string(1, '\xFF')), "non-ASCII column name");
		require(!Game::IsValidParamName("__public_ip"), "reserved column name");
	}

	void game_schema_test()
	{
		Game game{ GameData{
			.name = "schema-test", .secretKey = "secret", .keys = { { "select" } },
			.misssingKeyPolicy = GameData::MissingKeyPolicy::add_as_string
		} };
		run(game.Connect());
		Game::IncomingServer incoming{
			.public_ip = "127.0.0.1", .public_port = 6500,
			.data = { { "select", "configured keyword" }, { "order", "automatic keyword" } }
		};
		run(game.AddOrUpdateServer(incoming));
		const auto rows = run(game.GetServers("", { "select", "order" }, 10));
		require(rows.size() == 1 && rows[0].data.at("select") == "configured keyword"
			&& rows[0].data.at("order") == "automatic keyword", "SQL keywords as column names");
		require(run(game.GetServers("", { "missing_field" }, 10)).empty(), "unknown quoted field must not become a string literal");

		const auto originalKeys = game.keys().size();
		incoming.data = { { "a_first", "first" }, { "duplicate", "one" }, { "DUPLICATE", "two" } };
		bool rejected = false;
		try { run(game.AddOrUpdateServer(incoming)); }
		catch (const std::invalid_argument&) { rejected = true; }
		require(rejected && game.keys().size() == originalKeys, "case-colliding batch must not change metadata");
		const auto preserved = run(game.GetServers("", { "select" }, 10));
		require(preserved.size() == 1 && preserved[0].data.at("select") == "configured keyword", "failed batch must preserve existing row");
		incoming.data = { { "a_first", "retry" }, { "duplicate", "valid" } };
		run(game.AddOrUpdateServer(incoming));
		const auto retried = run(game.GetServers("", { "a_first", "duplicate" }, 10));
		require(retried.size() == 1 && retried[0].data.at("a_first") == "retry"
			&& retried[0].data.at("duplicate") == "valid", "failed batch must leave schema usable for retry");
		require(game.keys().size() == originalKeys + 2, "retry adds each key once");

		incoming.data = { { "DUPLICATE", "existing column" } };
		run(game.AddOrUpdateServer(incoming));
		require(game.keys().size() == originalKeys + 2, "case variant must reuse existing SQL column");
		const auto existing = run(game.GetServers("", { "duplicate" }, 10));
		require(existing.size() == 1 && existing[0].data.at("duplicate") == "existing column", "case variant updates existing column");

		Game duplicateConfig{ GameData{ .name = "duplicate-config", .secretKey = "secret", .keys = { { "hostname" }, { "HOSTNAME" } } } };
		rejected = false;
		try { run(duplicateConfig.Connect()); }
		catch (const std::invalid_argument&) { rejected = true; }
		require(rejected, "case-colliding configured keys");
	}

	void gamedb_test()
	{
		const auto array = nlohmann::json::array();
		auto config = nlohmann::json::array({ {
			{ "name", "custom" }, { "secretKey", "secret" }, { "keys", { "hostname" } },
			{ "sendAsShort", array }, { "sendAsByte", array }, { "saveAsReal", array },
			{ "ignoredKeys", array }, { "autoKeys", false }
		} });
		require(GameDBInMemory::ValidateConfig(config), "valid game config");
		config.push_back(config.front());
		require(!GameDBInMemory::ValidateConfig(config), "duplicate game names");
		config.erase(config.begin() + 1);
		config[0]["keys"] = { "hostname", "HOSTNAME" };
		require(!GameDBInMemory::ValidateConfig(config), "case-colliding configured keys rejected early");
		config[0]["keys"] = { "hostname" };
		config[0]["secretKey"] = "";
		require(!GameDBInMemory::ValidateConfig(config), "empty game secret");
		require(!GameDBInMemory::ValidateConfig(nlohmann::json::object()), "config must be an array");
		boost::asio::io_context context;
		GameDBInMemory db{ context };
		require(!run(db.GetGame("not-present")), "unknown game lookup returns no game");
	}

	void response_test()
	{
		Game game{ GameData{ .name = "response-test", .secretKey = "secret", .keys = { { "hostname" } } } };
		run(game.Connect());
		game.SetPopularValues({ "first", "second" });
		Game::IncomingServer server{
			.public_ip = "127.0.0.1", .public_port = 6500, .data = { { "hostname", "second" } }
		};
		const auto response = ServerListRequest::GetServerBytes(game, server, { "hostname" }, true);
		require(response.size() == 6 && response.back() == 1, "popular value index");
		// ParseServer(..., usepopularlist=0) expects no 0xFF prefix for pushes.
		const auto pushed = ServerListRequest::GetServerBytes(game, server, { "hostname" }, false);
		require(pushed.size() == 12 && pushed[5] == 's' && pushed.back() == 0, "push strings have no popular marker");
		server.data = { { "HOSTNAME", "second" } };
		require(ServerListRequest::GetServerBytes(game, server, { "hostname" }, false) == pushed, "push key matching follows SQL column case rules");
		server.data.clear();
		const auto empty = ServerListRequest::GetServerBytes(game, server, { "hostname" }, true);
		require((empty.front() & 64) && empty[5] == 0xFF && empty[6] == 0, "empty requested values still set field flag");
	}

	void download_path_test()
	{
		for (const auto* path : { "mods/bf2/cache/banner.png", "mods\\bf2\\cache\\banner.png" })
			require(is_relative_download_path(path), "relative download path");
		for (const auto* path : { "", "../file", "a/../file", ".. \\file", "/file", "\\file",
			"C:file", "C:\\file", "//host/file", "\\\\host\\file", "file:stream", "http://host/file" })
			require(!is_relative_download_path(path), "unsafe download path");
	}

	void sapphire_test()
	{
		const auto key = std::array<unsigned char, 4>{ 1, 2, 3, 4 };
		// Generated by bundled GameSpy/serverbrowsing/sb_crypt.c GOAEncryptByte,
		// SDK commit d1deb2d1a951cf77933dda040b8d311cc09815a7; plaintext 0..31.
		constexpr std::array<unsigned char, 32> expected{
			0x7F, 0x1B, 0xF0, 0x6B, 0x7B, 0xF6, 0x9F, 0x31,
			0xB4, 0x6B, 0xE0, 0x88, 0x81, 0xF3, 0x06, 0x95,
			0x80, 0xFC, 0x32, 0x78, 0xCE, 0xC9, 0x61, 0xB7,
			0xB2, 0xC2, 0x5E, 0x1D, 0x2F, 0x86, 0xF0, 0x5A
		};
		sapphire reference{ key };
		for (std::size_t i = 0; i < expected.size(); ++i)
			require(reference.encrypt(static_cast<unsigned char>(i)) == expected[i], "GameSpy cipher reference vector");
		sapphire encoder{ key }, decoder{ key };
		for (unsigned int i = 0; i < 512; ++i) {
			const auto value = static_cast<unsigned char>(i);
			require(decoder.decrypt(encoder.encrypt(value)) == value, "Sapphire round trip");
		}
		sapphire emptyEncoder{ std::span<const unsigned char>{} }, emptyDecoder{ std::span<const unsigned char>{} };
		require(emptyDecoder.decrypt(emptyEncoder.encrypt(42)) == 42, "empty Sapphire key initializes state");
		bool rejected = false;
		try { sapphire oversized{ std::array<unsigned char, 256>{} }; }
		catch (const std::overflow_error&) { rejected = true; }
		require(rejected, "oversized Sapphire key");
	}
}

int main()
{
	try {
		heartbeat_test();
		browser_test();
		game_ownership_test();
		game_schema_test();
		gamedb_test();
		response_test();
		sapphire_test();
		download_path_test();
		std::cout << "Protocol and ownership regressions passed\n";
	}
	catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
