#include "http.client.h"
#include "bf2web.h"
#include "playerdb.h"
#include "utils.h"
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
#include <vector>
using namespace gamespy;

struct UnlockInfo
{
	std::uint16_t id;
	std::uint8_t kid_id;
	std::string name;
	std::string desc;
};

std::vector<UnlockInfo> g_Unlocks = {
	{ 11, 0, "Chsht_protecta", "Protecta shotgun with slugs" },
	{ 22, 1, "Usrif_g3a3", "H&K G3" },
	{ 33, 2, "USSHT_Jackhammer", "Jackhammer shotgun" },
	{ 44, 3, "Usrif_sa80", "SA-80" },
	{ 55, 4, "Usrif_g36c", "G36C" },
	{ 66, 5, "RULMG_PKM", "PKM" },
	{ 77, 6, "USSNI_M95_Barret", "Barret M82A2 (.50 cal rifle)" },
	{ 88, 1, "sasrif_fn2000", "FN2000" },
	{ 99, 2, "sasrif_mp7", "MP-7" },
	{ 111, 3, "sasrif_g36e", "G36E" },
	{ 222, 4, "usrif_fnscarl", "FN SCAR - L" },
	{ 333, 5, "sasrif_mg36", "MG36" },
	{ 444, 0, "eurif_fnp90", "P90" },
	{ 555, 6, "gbrif_l96a1", "L96A1" }
};

boost::asio::awaitable<std::string> HttpClient::HandleBF2Web(const boost::urls::url_view& uri)
{
	using rtype = bf2web::response::type;

	unsigned long pid{ 0 };
	const auto& params = uri.params();
	if (auto iter = params.find("pid"); iter != params.end()) {
		auto parsed = utils::parse_uint32((*iter).value);
		if (!parsed) {
			co_return "";
		}

		pid = *parsed;
	}

	std::string path{ uri.encoded_path() };
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return std::tolower(c); });

	if (path == "/asp/createplayer.aspx") {
		co_return "OK";
	}
			
	if (path == "/asp/getawardsinfo.aspx") {
		auto bf2resp = bf2web::response{};
		bf2resp.Append(rtype::HEADER, "pid", "asof");
		bf2resp.Append(rtype::DATA, pid, std::time(nullptr));
		bf2resp.Append(rtype::HEADER, "award", "level", "when", "first");
		co_return bf2resp.ToString();
	} 
			
	if (path == "/asp/getbackendinfo.aspx") {
		auto bf2resp = bf2web::response{};
		bf2resp.Append(rtype::HEADER, "ver", "now");
		bf2resp.Append(rtype::DATA, "0.1", std::time(nullptr));

		bf2resp.Append(rtype::HEADER, "id", "kit", "name", "descr");
		for (const auto& unlock : g_Unlocks)
			bf2resp.Append(rtype::DATA, unlock.id, unlock.kid_id, unlock.name, unlock.desc);

		co_return bf2resp.ToString();
	} 
	
	if (path == "/asp/getleaderboard.aspx") {
		auto bf2resp = bf2web::response{};
		bf2resp.Append(rtype::HEADER, "size", "asof");
		bf2resp.Append(rtype::DATA, 0, std::time(nullptr));
		// &type=score&id=overall
		//bf2resp.Append(rtype::HEADER, "n", "pid", "nick", "weeklyscore", "totaltime", "date", "playerrank", "countrycode");
		// &type=score&id=commander
		// "n", "pid", "nick", "coscore", "cotime", "playerrank", "countrycode"
		// &type=kill&id=team
		// "n", "pid", "nick", "teamscore", "totaltime", "playerrank", "countrycode"
		// &type=kill&id=combat
		// "n", "pid", "nick", "score", "totalkills", "totaltime", "playerrank", "countrycode"
		// &type=risingstar
		// "n", "pid", "nick", "weeklyscore", "totaltime", "date", "playerrank", "countrycode"
		// &type=kit
		// "n", "pid", "nick", "killswith", "deathsby", "timeplayed", "playerrank", "countrycode"
		// &type=vehicle
		// "n", "pid", "nick", "killswith", "deathsby", "timeused", "playerrank", "countrycode"
		// &type=weapon
		// "n", "pid", "nick", "killswith", "detahsby", "timeused", "accuracy", "playerrank", "countrycode"
		// => if pid == 0 then do not add a data line
		// => if pid != 0 then add data line with all zero data
		co_return bf2resp.ToString();
	} 
	
	if (path == "/asp/getmapinfo.aspx") {
		auto bf2resp = bf2web::response{};
		bf2resp.SetError(true);
		bf2resp.Append(rtype::HEADER, "error");
		bf2resp.Append(rtype::DATA, "Map Data Not Found!");
		co_return bf2resp.ToString();
	}
	
	if (path == "/asp/getplayerid.aspx") {
		auto nick = params.find("nick");
		if (nick == params.end()) {
			auto bf2resp = bf2web::response{};
			bf2resp.SetError(true);
			bf2resp.Append(rtype::HEADER, "error");
			bf2resp.Append(rtype::DATA, "No nick provided!");
			co_return bf2resp.ToString();
		} else {
			if ((*nick).value.length() > 32) {
				auto bf2resp = bf2web::response{};
				bf2resp.SetError(true);
				bf2resp.Append(rtype::HEADER, "error");
				bf2resp.Append(rtype::DATA, "Nick Specified is larger than 32 characters!");
				co_return bf2resp.ToString();
			} else {
				if (!co_await m_PlayerDB.HasPlayer((*nick).value)) {
					auto bf2resp = bf2web::response{};
					bf2resp.SetError(true);
					bf2resp.Append(rtype::HEADER, "asof", "error");
					bf2resp.Append(rtype::DATA, std::time(nullptr), "Player Not Found!");
					co_return bf2resp.ToString();
				} else {
					const auto& player = co_await m_PlayerDB.GetPlayerByName((*nick).value);
					auto bf2resp = bf2web::response{};
					bf2resp.Append(rtype::HEADER, "pid");
					bf2resp.Append(rtype::DATA, player->GetProfileID());
					co_return bf2resp.ToString();
				}
			}
		}
	} 
	
	if (path == "/asp/getplayerinfo.aspx") {
		if (pid == 0) {
			auto bf2resp = bf2web::response{};
			bf2resp.SetError(true, 107);
			bf2resp.Append(rtype::HEADER, "asof", "error");
			bf2resp.Append(rtype::DATA, std::time(nullptr), "Invalid Syntax!");
			co_return bf2resp.ToString();
		}
		
		auto info = params.find("info");
		if (info == params.end()) {
			auto bf2resp = bf2web::response{};
			bf2resp.SetError(true);
			bf2resp.Append(rtype::HEADER, "asof", "error");
			bf2resp.Append(rtype::DATA, std::time(nullptr), "Parameter Error!");
			co_return bf2resp.ToString();
		}
		
		if ((*info).value == "overview") {
			auto bf2resp = bf2web::response{};
			bf2resp.Append(rtype::HEADER, "pid", "nick", "country", "asof");
			bf2resp.Append(rtype::DATA, pid, "PlayerName", "US", std::time(nullptr));
			co_return bf2resp.ToString();
		}
		
		auto bf2resp = bf2web::response{};
		bf2resp.SetError(true);
		bf2resp.Append(rtype::HEADER, "asof", "error");
		bf2resp.Append(rtype::DATA, std::time(nullptr), "Invalid Syntax!");
		co_return bf2resp.ToString();
	}
	
	if (path == "/asp/getrankinfo.aspx") {
		auto bf2resp = bf2web::response{};
		bf2resp.Append(rtype::HEADER, "rank", "chng", "decr");
		bf2resp.Append(rtype::DATA, 0, 0, 0);
		co_return bf2resp.ToString();
	}

	if (path == "/asp/getunlocksinfo.aspx") {				
		auto bf2resp = bf2web::response{};
		bf2resp.Append(rtype::HEADER, "pid", "nick", "asof");
		bf2resp.Append(rtype::DATA, pid, "All_Unlocks", std::time(nullptr));

		bf2resp.Append(rtype::HEADER, "enlisted", "officer");
		bf2resp.Append(rtype::DATA, 0, 0);

		bf2resp.Append(rtype::HEADER, "id", "state");
		for (const auto& unlock : g_Unlocks)
			bf2resp.Append(rtype::DATA, unlock.id, "s");

		co_return bf2resp.ToString();
	}

	co_return "";
}

HttpClient::HttpClient(boost::asio::ip::tcp::socket nSocket, GameDB& gameDB, PlayerDB& playerDB)
	: m_Socket{ std::move(nSocket) }, m_GameDB{ gameDB }, m_PlayerDB{ playerDB }
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
	if (error || request.method() != boost::beast::http::verb::get) {
		co_return;
	}

	auto uri = boost::urls::parse_origin_form(request.target());
	if (!uri) {
		co_return;
	}

	std::string host{ request[boost::beast::http::field::host] };
	std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });

	if (host == "eapusher.dice.se") {
		auto path = uri->encoded_path();
		if (path == "/image_english.png") {
			auto response = boost::beast::http::response<boost::beast::http::file_body>{};
			response.version(request.version());
			response.keep_alive(false);
			response.result(boost::beast::http::status::ok);

			boost::beast::error_code ec;
			response.body().open("http/image_english.png", boost::beast::file_mode::read, ec);
			if (!ec) {
				co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
			}
		}
	}
	else if (host == "bf2web.gamespy.com") {
		auto body = co_await HandleBF2Web(*uri);
		auto response = boost::beast::http::response<boost::beast::http::string_body>{};
		response.version(request.version());
		response.keep_alive(false);
		response.result(boost::beast::http::status::ok);
		response.body() += body;
		co_await boost::beast::http::async_write(m_Socket, response, boost::asio::as_tuple(boost::asio::use_awaitable));
	}
}