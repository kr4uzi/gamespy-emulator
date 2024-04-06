#include "database.sqlite.h"
#include "database.memory.h"
#include "master.h"
#include "gpcm.h"
#include "gpsp.h"
#include "ms.h"
#include "http.h"
#include "asio.h"
#include "dns.h"
#include <csignal>
#include <print>
#include <iostream>
#include <fstream>
#include <regex>

auto ParseGameList(std::istream stream)
{
	// FULL-TITLE	GAMENAME	GAMEID	SECRETKEY	STATS_VERSION	STATS_KEY
	std::regex pattern(R"(^(.+)\t(\w+)\t(\d*)\t(\w*)\t(\d*)\t\(\w*)$)");

	std::map<std::string, gamespy::GameData> games;
	for (std::string line; std::getline(stream, line);) {
		if (line.starts_with('#') || line.empty())
			continue;

		std::smatch match;
		if (std::regex_match(line, match, pattern)) {
			auto name = match[2].str();
			games.emplace(std::make_pair(name, gamespy::GameData(name, match[1].str(), match[4].str(), 6500, std::stoul(match[5]), match[6].str())));
		}
	}

	return games;
}

int main()
{
	try {
		boost::asio::io_context context;
		boost::asio::signal_set signals(context, SIGINT, SIGTERM);
		signals.async_wait([&](auto, auto) {
			std::println("SHUTDOWN REQUESTED");
			context.stop();
			});

		std::map<std::string, gamespy::GameData> games {
			{ "battlefield2", gamespy::GameData("battlefield2", "Battlefield 2", "hW6m9a", 6500, 1, "") },
			{ "gmtest", gamespy::GameData("gmtest", "Test / demo / temporary", "HA6zkS", 6500, 1, "") }
		};

		std::unique_ptr<gamespy::Database> db = std::make_unique<gamespy::SQLite3DB>("gs_emulator.sqlite3", games);
		if (!db->HasError()) {
			gamespy::MasterServer master(context, *db);
			gamespy::LoginServer gpcm(context, *db);
			gamespy::SearchServer gpsp(context, *db);
			gamespy::BrowserServer ms(context, master, *db);
			gamespy::NewsServer news(context);
			gamespy::DNSServer dns(context, *db);

			boost::asio::co_spawn(context, master.AcceptConnections(), boost::asio::detached);
			boost::asio::co_spawn(context, gpcm.AcceptClients(), boost::asio::detached);
			boost::asio::co_spawn(context, gpsp.AcceptClients(), boost::asio::detached);
			boost::asio::co_spawn(context, ms.AcceptClients(), boost::asio::detached);
			boost::asio::co_spawn(context, news.AcceptClients(), boost::asio::detached);
			boost::asio::co_spawn(context, dns.AcceptConnections(), boost::asio::detached);

			// http://eapusher.dice.se/image.asp?lang=English
			// gpsp.gamespy.com TCP 29901
			// gpcm.gamespy.com TCP 29900
			// gamestats.gamespy.com TCP 29920
			// %s.master.gamespy.com UDP 27900
			// %s.ms%d.gamespy.com TCP 28910
			// %s.available.gamespy.com UDP 27900 (same server as master)
			// http://BF2Web.gamespy.com/ASP/
			// http://stage-net.gamespy.com/bf2/getplayerinfo.aspx?pid=

			context.run();
		}
	}
	catch (std::exception& e) {
		std::println(std::cerr, "[ERR] {}", e.what());
	}
}