#include "playerdb.sqlite.h"
#include "gamedb.h"
#include "master.h"
#include "gpcm.h"
#include "gpsp.h"
#include "ms.h"
#include "key.h"
#include "http.h"
#include "asio.h"
#include "dns.h"
#include <csignal>
#include <print>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv)
{
	bool startDNS = true, startHTTP = true;
	for (int i = 1; i < argc; i++) {
		if (argv[i] == std::string_view{ "dns=0" })
			startDNS = false;
		else if (argv[i] == std::string_view{ "http=0" })
			startHTTP = false;
	}

	try {
		auto context = boost::asio::io_context{};
		auto signals = boost::asio::signal_set{ context, SIGINT, SIGTERM };
		signals.async_wait([&](auto, auto) {
			std::println("SHUTDOWN REQUESTED");
			context.stop();
		});

		auto playerDB = std::unique_ptr<gamespy::PlayerDB>{ new gamespy::PlayerDBSQLite({
			.db_file = "player_db.sqlite3",
			.sql_file = "schema_sqlite.sql"
		}) };

		auto gameDB = std::unique_ptr<gamespy::GameDB>{ new gamespy::GameDBSQLite({
			 .games_list_file = "game_list.tsv",
			 .game_params_file = "game_params.cfg",
			 .auto_params = true
		}) };

		// commented because this needs to check the bf2-stats authorized servers
		//if (gameDB->HasGame("battlefield2")) {
		//	gameDB->GetGame("battlefield2").BeforeServerAdd.connect([](auto& server) {
		//		server.data["bf2_ranked"] = "1";
		//	});
		//}

		auto master = gamespy::MasterServer{ context, *gameDB };
		auto gpcm = gamespy::LoginServer{ context, *playerDB };
		auto gpsp = gamespy::SearchServer{ context, *playerDB };
		auto ms = gamespy::BrowserServer{ context, *gameDB };
		auto key = gamespy::CDKeyServer{ context };
		std::unique_ptr<gamespy::DNSServer> dns;
		std::unique_ptr<gamespy::HttpServer> http;

		if (startDNS)
			dns.reset(new gamespy::DNSServer{ context, *gameDB });
		
		if (startHTTP)
			http.reset(new gamespy::HttpServer{ context, *gameDB });

		boost::asio::co_spawn(context, master.AcceptConnections(), boost::asio::detached);
		boost::asio::co_spawn(context, gpcm.AcceptClients(), boost::asio::detached);
		boost::asio::co_spawn(context, gpsp.AcceptClients(), boost::asio::detached);
		boost::asio::co_spawn(context, ms.AcceptClients(), boost::asio::detached);
		boost::asio::co_spawn(context, key.AcceptConnections(), boost::asio::detached);

		if (dns)
			boost::asio::co_spawn(context, dns->AcceptConnections(), boost::asio::detached);

		if (http)
			boost::asio::co_spawn(context, http->AcceptClients(), boost::asio::detached);

		// strings / urls found in bf2 files
		// http://eapusher.dice.se/image.asp?lang=English
		// gpsp.gamespy.com TCP 29901
		// gpcm.gamespy.com TCP 29900
		// gamestats.gamespy.com TCP 29920 <-- not used?
		// %s.master.gamespy.com UDP 27900
		// %s.ms%d.gamespy.com TCP 28910
		// %s.available.gamespy.com UDP 27900 (same server as master)
		// http://BF2Web.gamespy.com/ASP/
		// http://stage-net.gamespy.com/bf2/getplayerinfo.aspx?pid=

		context.run();
	}
	catch (std::exception& e) {
		std::println(std::cerr, "[ERR] {}", e.what());
	}
}