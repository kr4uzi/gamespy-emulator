#include "http.client.h"
#include "bf2web.h"
#include <map>
#include <string_view>
#include <array>
#include <ctime>
#include <optional>
#include <print>
#include <variant>
#include <charconv>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/url.hpp>
using namespace gamespy;

HttpClient::HttpClient(boost::asio::ip::tcp::socket nSocket)
	: m_Socket{ std::move(nSocket) }
{

}

HttpClient::~HttpClient()
{

}

boost::asio::awaitable<void> HttpClient::Run()
{
	// the connection should never last more than 15 seconds (if so, it is probably due to bug in this code)
	boost::asio::steady_timer m_Timeout{ m_Socket.get_executor(), std::chrono::seconds(15) };
	m_Timeout.async_wait([&](const auto& error) {
		if (error) return;
		m_Socket.cancel();
	});

	auto buffer = boost::beast::flat_buffer{ 8192 };
	auto request = boost::beast::http::request<boost::beast::http::dynamic_body>{};
	const auto& [error, length] = co_await boost::beast::http::async_read(m_Socket, buffer, request, boost::asio::as_tuple(boost::asio::use_awaitable));
	if (error)
		co_return;

	auto uri = boost::urls::parse_origin_form(request.target());
	std::string method;
	switch (request.method()) {
	case boost::beast::http::verb::get:
		method = "GET"; break;
	case boost::beast::http::verb::head:
		method = "HEAD"; break;
	case boost::beast::http::verb::post:
		method = "POST"; break;
	default:
		method = std::to_string((int)request.method());
		break;
	}

	if (request.method() == boost::beast::http::verb::get) {
		const auto& host = request[boost::beast::http::field::host];
		if (!uri) co_return;

		auto path = uri->encoded_path();
		if (host == "eapusher.dice.se") {
			if (path == "/image_english.png") {
				auto response = boost::beast::http::response<boost::beast::http::file_body>{};
				response.version(request.version());
				response.keep_alive(false);
				response.result(boost::beast::http::status::ok);

				boost::beast::error_code ec;
				response.body().open("http/image_english.png", boost::beast::file_mode::read, ec);
				if (!ec) {
					const auto& [error, length] = co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
				}
			}
		}
		else if (host == "BF2Web.gamespy.com") {
			using rtype = bf2web::response::type;
			if (path == "/ASP/getbackendinfo.aspx") {
				auto bf2resp = bf2web::response{};
				bf2resp.Append(rtype::HEADER, "ver", "rnk", "now");
				bf2resp.Append(rtype::DATA, "0.1", "1", std::to_string(std::time(nullptr)));
				bf2resp.Append(rtype::HEADER, "id", "kit", "name", "descr");
				bf2resp.Append(rtype::DATA, "11", "0", "Chsht_protecta", "Protecta shotgun with slugs");
				bf2resp.Append(rtype::DATA, "22", "1", "Usrif_g3a3", "H&K G3");
				bf2resp.Append(rtype::DATA, "33", "2", "USSHT_Jackhammer", "Jackhammer shotgun");
				bf2resp.Append(rtype::DATA, "44", "3", "Usrif_sa80", "SA-80");
				bf2resp.Append(rtype::DATA, "55", "4", "Usrif_g36c", "G36C");
				bf2resp.Append(rtype::DATA, "66", "5", "RULMG_PKM", "PKM");
				bf2resp.Append(rtype::DATA, "77", "6", "USSNI_M95_Barret", "Barret M82A2 (.50 cal rifle)");
				bf2resp.Append(rtype::DATA, "88", "1", "sasrif_fn2000", "FN2000");
				bf2resp.Append(rtype::DATA, "99", "2", "sasrif_mp7", "MP-7");
				bf2resp.Append(rtype::DATA, "111", "3", "sasrif_g36e", "G36E");
				bf2resp.Append(rtype::DATA, "222", "4", "usrif_fnscarl", "FN SCAR - L");
				bf2resp.Append(rtype::DATA, "333", "5", "sasrif_mg36", "MG36");
				bf2resp.Append(rtype::DATA, "444", "0", "eurif_fnp90", "P90");
				bf2resp.Append(rtype::DATA, "555", "6", "gbrif_l96a1", "L96A1");

				auto response = boost::beast::http::response<boost::beast::http::string_body>{};
				response.version(request.version());
				response.keep_alive(false);
				response.result(boost::beast::http::status::ok);
				response.body() += bf2resp.ToString();
				co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));

				co_return;
			}

			unsigned long pid{ 0 };
			const auto& params = uri->params();
			if (auto iter = params.find("pid"); iter != params.end()) {
				auto begin = (*iter).value.c_str();
				char* end;
				pid = std::strtoul(begin, &end, 10);
				if (begin != end)
					co_return;
			}

			
			if (pid == 0) {
				co_return;
			}

			auto response = boost::beast::http::response<boost::beast::http::string_body>{};
			response.version(request.version());
			response.keep_alive(false);
			response.result(boost::beast::http::status::ok);

			if (path == "/ASP/getunlocksinfo.aspx") {				
				auto bf2resp = bf2web::response{};
				bf2resp.Append(rtype::HEADER, "pid", "nick", "asof");
				bf2resp.Append(rtype::DATA, std::to_string(pid), "All_Unlocks", std::to_string(std::time(nullptr)));

				bf2resp.Append(rtype::HEADER, "enlisted", "officer");
				bf2resp.Append(rtype::DATA, "0", "0");

				bf2resp.Append(rtype::HEADER, "id", "state");
				for (const auto& id : std::array{ "11", "22", "33", "44", "55", "66", "77", "88", "99", "111", "222", "333", "444", "555" })
					bf2resp.Append(rtype::DATA, id, "s");

				response.body() += bf2resp.ToString();
				co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
			}
			else if (path == "/ASP/getrankinfo.aspx") {
				/*
				auto stmt = sqlite::stmt{m_DB, "SELECT rank_id, chng, decr FROM player WHERE id=?", pid};
				if (std::tuple<int, int, int> data; stmt.query(data)) {
					auto bf2resp = bf2web::response{};
					bf2resp.Append(rtype::HEADER, "rank", "chng", "decr");
					bf2resp.Append(rtype::DATA, std::get<0>(data), std::get<1>(data), std::get<2>(data));
					response.body() += bf2resp.ToString();

					if (std::get<1>(data) > 0 || std::get<2>(data) > 0) {
						sqlite::stmt{ m_DB, "UPDATE player SET chng=0, decr=0 WHERE id=?", pid }.update();
					}

					co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
				}
				*/
			}
			else if (path == "/ASP/getplayerinfo.aspx") {
				const auto& params = uri->params();
				const auto& info = params.find("info");
				const auto& kit = params.find("kit");
				const auto& vehicle = params.find("vehicle");
				const auto& weapon = params.find("weapon");
				const auto& map = params.find("map");

				using row_t = std::map<std::string_view, std::string_view>;

				auto dbMap = std::map<std::string, std::variant<std::string, std::function<std::string(const row_t&)>>>{
					{ "pid", "id" },
					{ "nick", "name" },
					{ "scor", "score" },
					{ "jond", "joined" },
					{ "wins", "wins" },
					{ "loss", "losses" },
					{ "mode0", "mode0" },
					{ "mode1", "mode1" },
					{ "mode2", "mode2" },
					{ "time", "time" },
					{ "smoc", [](const row_t& row) { return row.at("rank_id") == "11" ? "1" : "0"; } },
					{ "cmsc", "skillscore" },
					{ "osaa", [](const row_t& row) { return "0.00"; } }, // Overall small arms accuracy
					{ "kill", "kills" },
					{ "kila", "damageassists" },
					{ "deth", "deaths" },
					{ "suic", "suicides" },
					{ "bksk", "killstreak" },
					{ "wdsk", "deathstreak" },
					{ "tvcr", "0" }, // Top Victim
					{ "topr", "0" }, // Top Opponent
					{ "klpm", [](const row_t& row) {
						auto killsStr = row.at("kills");
						auto timeStr = row.at("time");
						unsigned long kills{}, time{};
						std::from_chars(killsStr.data(), killsStr.data() + killsStr.size(), kills);
						std::from_chars(timeStr.data(), timeStr.data() + timeStr.size(), time);

						return std::format("{:2}", 60 * (kills / std::max(1ul, time))); } 
					}, // Kills per min
					{ "dtpm", "" }, // @number_format(60 * ($row['deaths'] / max(1, $row['time'])), 2, '.', ''), // Deaths per min
					{ "ospm", "" }, // @number_format(60 * ($row['score'] / max(1, $row['time'])), 2, '.', ''), // Score per min
					{ "klpr", "" }, // @number_format($row['kills'] / max(1, $row['rounds']), 2, '.', ''), // Kill per round
					{ "dtpr", "" }, // "@number_format($row['deaths'] / max(1, $row['rounds']), 2, '.', ''), // Deaths per round
					{ "twsc", "teamscore" },
					{ "cpcp", "captures" },
					{ "cacp", "captureassists" },
					{ "dfcp", "defends" },
					{ "heal", "heals" },
					{ "rviv", "revives" },
					{ "rsup", "resupplies" },
					{ "rpar", "repairs" },
					{ "tgte", "targetassists" },
					{ "dkas", "driverassists" },
					{ "dsab", "driverspecials" },
					{ "cdsc", "cmdscore" },
					{ "rank", "rank_id" },
					{ "kick", "kicked" },
					{ "bbrs", "bestscore" },
					{ "tcdr", "cmdtime" },
					{ "ban", "banned" },
					{ "lbtl", "lastonline" },
					{ "vrk", "0" }, // Vehicle Road Kills
					{ "tsql", "sqltime" },
					{ "tsqm", "sqmtime" },
					{ "tlwf", "lwtime" },
					{ "mvks", "0" }, // Top Victim kills
					{ "vmks", "0" }, // Top Opponent Kills
					{ "mvns", "-" }, // Top Victim name
					{ "mvrs", "0" }, // Top Victim rank
					{ "vmns", "-" }, // Top opponent name
					{ "vmrs", "0" }, // Top opponent rank
					{ "fkit", "0" }, // Fav Kit
					{ "fmap", "" }, // getFavMap($pid), // Fav Map
					{ "fveh", "0" }, // Fav vehicle
					{ "fwea", "0" }, // Fav Weapon
					{ "tnv", "0" }, // NIGHT VISION GOGGLES Time - NOT USED
					{ "tgm", "0" } // GAS MASK TIME - NOT USED
				};
			}
		}
	}
	else {
		auto response = boost::beast::http::response<boost::beast::http::string_body>{};
		response.version(request.version());
		response.keep_alive(false);
		response.result(boost::beast::http::status::bad_request);
		response.set(boost::beast::http::field::content_type, "text/plain");
		response.body() += "Invalid request-method '" + std::string(request.method_string()) + "'";
		const auto& [error, length] = co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
	}
}