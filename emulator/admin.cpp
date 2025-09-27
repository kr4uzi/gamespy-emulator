#include "gamedb.h"
#include "game.h"
#include "playerdb.h"
#include "admin.h"
#include <print>
#include <utility>
#include <map>
#include <ranges>
#include <optional>
#include <filesystem>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/url.hpp>
#include <nlohmann/json.hpp>

using namespace gamespy;
namespace beast = boost::beast; 
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace {
	auto parseInt(const std::string_view& str) -> std::optional<std::uint32_t>
	{
		auto begin = str.data();
		auto end = begin + str.size();
		std::uint32_t value;
		if (std::from_chars(begin, end, value).ptr != end)
			return std::nullopt;
		return value;
	}

	beast::string_view mimeType(beast::string_view path)
	{
		using beast::iequals;
		auto const ext = [&path] {
			auto const pos = path.rfind(".");
			if (pos == beast::string_view::npos)
				return beast::string_view{};
			return path.substr(pos);
		}();
		if (iequals(ext, ".htm"))  return "text/html";
		if (iequals(ext, ".html")) return "text/html";
		if (iequals(ext, ".php"))  return "text/html";
		if (iequals(ext, ".css"))  return "text/css";
		if (iequals(ext, ".txt"))  return "text/plain";
		if (iequals(ext, ".js"))   return "application/javascript";
		if (iequals(ext, ".json")) return "application/json";
		if (iequals(ext, ".xml"))  return "application/xml";
		if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
		if (iequals(ext, ".flv"))  return "video/x-flv";
		if (iequals(ext, ".png"))  return "image/png";
		if (iequals(ext, ".jpe"))  return "image/jpeg";
		if (iequals(ext, ".jpeg")) return "image/jpeg";
		if (iequals(ext, ".jpg"))  return "image/jpeg";
		if (iequals(ext, ".gif"))  return "image/gif";
		if (iequals(ext, ".bmp"))  return "image/bmp";
		if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
		if (iequals(ext, ".tiff")) return "image/tiff";
		if (iequals(ext, ".tif"))  return "image/tiff";
		if (iequals(ext, ".svg"))  return "image/svg+xml";
		if (iequals(ext, ".svgz")) return "image/svg+xml";
		return "application/text";
	}
}

class AdminClient
{
	tcp::socket m_Socket;
	GameDB& m_GameDB;
	PlayerDB& m_PlayerDB;

public:
	AdminClient(tcp::socket socket, GameDB& gameDB, PlayerDB &playerDB)
		: m_Socket(std::move(socket)), m_GameDB(gameDB), m_PlayerDB(playerDB)
	{

	}

	~AdminClient()
	{

	}

	boost::asio::awaitable<void> SendResponse(const http::request<http::dynamic_body>& request, http::status status, std::optional<nlohmann::json> body = {})
	{
		auto response = http::response<http::string_body>{};
		response.result(status);
		response.version(request.version());
		response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		response.keep_alive(false);
		if (body) {
			response.set(http::field::content_type, "application/json");
			response.body() = body->dump();
		}

		co_await http::async_write(m_Socket, response, boost::asio::use_awaitable);
	}

	net::awaitable<bool> HandleAPI(const http::request<http::dynamic_body>& request, const boost::urls::string_view& path, const boost::urls::params_view& params)
	{
		auto method = request.method();
		if (path == "/api/games") {
			if (method != http::verb::get) {
				co_await SendResponse(request, http::status::bad_request, { {"error", "invalid http method"} });
				co_return false;
			}

			nlohmann::json games = nlohmann::json::array();
			for (const auto& game : co_await m_GameDB.GetGames()) {
				using StoreType = GameData::GameKey::Store;
				using SendType = GameData::GameKey::Send;
				nlohmann::json params = nlohmann::json::array();

				for (const auto& param : game->keys())
					params.push_back({
						{"name", param.name},
						{"type", param.type},
						{"label", param.label},
						{"description", param.description}
					});

				games.push_back(nlohmann::json{
					{"name", game->name()},
					{"keys", params}
				});
			}

			co_await SendResponse(request, http::status::ok, games);
			co_return true;
		}

		// all other api calls require a game parameter
		if (!params.contains("game")) {
			co_await SendResponse(request, http::status::bad_request, { {"error", "missing game parameter"} });
			co_return false;
		}

		auto game = co_await m_GameDB.GetGame((*params.find("game"))->value);
		if (!game) {
			co_await SendResponse(request, http::status::bad_request, { {"error", "unknown game"} });
			co_return false;
		}

		if (path == "/api/servers") {
			if (request.method() != http::verb::get) {
				co_await SendResponse(request, http::status::bad_request, { {"error", "invalid http method"} });
				co_return false;
			}

			auto query = params.contains("query") ? (*params.find("query"))->value : "";
			auto limit = params.contains("limit") ? *parseInt((*params.find("limit"))->value) : 100;
			auto skip = params.contains("skip") ? *parseInt((*params.find("skip"))->value) : 0;

			std::vector<std::string> fields;
			if (auto _fields = params.find("fields"); _fields != params.end() && (*_fields)->has_value)
				fields = (*_fields)->value | std::views::split(',') | std::ranges::to<std::vector<std::string>>();

			nlohmann::json servers = nlohmann::json::array();
			for (const auto& server : co_await game->GetServers(query, fields | std::ranges::to<std::vector<std::string_view>>(), limit, skip)) {
				nlohmann::json data;
				for (const auto& [name, key] : server.data)
					data[name] = key;
				
				data["ip"] = server.public_ip;
				data["port"] = server.public_port;
				servers.push_back(data);
			}

			co_await SendResponse(request, http::status::ok, servers);
			co_return true;
		}
		else if (path == "/api/server") {
			if (!params.contains("ip") || !params.contains("port")) {
				co_await SendResponse(request, http::status::bad_request, { {"error", "missing ip or port parameter"} });
				co_return false;
			}

			auto port = parseInt((*params.find("port"))->value);
			if (!port) {
				co_await SendResponse(request, http::status::bad_request, { {"error", "invalid port parameter"} });
				co_return false;
			}

			if (request.method() == http::verb::post) {
				auto values = std::map<std::string, std::string>{};
				auto valuesView = std::map<std::string_view, std::string_view>{};
				for (const auto& p : params) {
					if (p.key.empty() || p.key == "game" || p.key == "ip" || p.key == "port" || !p->has_value) continue;

					auto pair = values.emplace(p.key, p.value);
					valuesView.emplace(pair.first->first, pair.first->second);
				}

				auto ip = (*params.find("ip"))->value;
				auto server = Game::IncomingServer{
					.public_ip = ip,
					.public_port = static_cast<std::uint16_t>(*port),
					.data = valuesView
				};

				co_await game->AddOrUpdateServer(server);
				co_await SendResponse(request, http::status::ok, nlohmann::json{ {"status", "success"}, {"message", "added server"} });
				co_return true;
			}
			else if (request.method() == http::verb::delete_) {
				co_await game->RemoveServers({ { (*params.find("ip"))->value, static_cast<std::uint16_t>(*port) } });
				co_await SendResponse(request, http::status::ok, nlohmann::json{ {"status", "success"}, {"message", "removed server"} });
				co_return true;
			}
			
			co_await SendResponse(request, http::status::bad_request, { {"error", "invalid http method"} });
			co_return false;
		}

		co_await SendResponse(request, http::status::not_found);
		co_return false;
	}

	net::awaitable<bool> HandleFileLookup(const http::request<http::dynamic_body>& request, const std::string_view& path)
	{
		auto filePath = std::filesystem::path("http") / (path == "/" ? "index.html" : path.substr(1));
		if (!std::filesystem::exists(filePath)) {
			co_await SendResponse(request, http::status::not_found);
			co_return false;
		}

		beast::error_code ec;
		http::file_body::value_type body;
		body.open(filePath.string().c_str(), boost::beast::file_mode::scan, ec);
		if (ec) {
			co_await SendResponse(request, http::status::internal_server_error, { {"error", "failed to open file"} });
			co_return false;
		}

		auto size = body.size();
		http::response<http::file_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(http::status::ok, request.version())
		};
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, ::mimeType(filePath.string()));
		res.content_length(size);
		res.keep_alive(false);
		co_await http::async_write(m_Socket, res, net::use_awaitable);
		co_return true;
	}

	net::awaitable<void> Run()
	{
		auto buffer = beast::flat_buffer{ 8192 };
		auto request = http::request<http::dynamic_body>{};
		const auto& [error, length] = co_await http::async_read(m_Socket, buffer, request, net::as_tuple);
		if (error) co_return;

		auto uri = boost::urls::parse_origin_form(request.target());
		auto path = uri->path();
		auto params = uri->params();
		
		if (path.starts_with("/api/"))
			co_await HandleAPI(request, path, params);
		else
			co_await HandleFileLookup(request, path);
	}
};

AdminServer::AdminServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB, boost::asio::ip::port_type port)
	: m_Acceptor(context, tcp::endpoint(boost::asio::ip::make_address("::1"), port)), m_GameDB(gameDB), m_PlayerDB(playerDB)
{
	std::println("[admin] listening on port {}", port);
}

AdminServer::~AdminServer()
{

}

boost::asio::awaitable<void> AdminServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(net::as_tuple);
		if (error)
			break;

		net::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), net::detached);
	}
}

boost::asio::awaitable<void> AdminServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		auto client = AdminClient{ std::move(socket), m_GameDB, m_PlayerDB };
		co_await client.Run();
	}
	catch (const std::exception& e) {
		std::println("[admin] connection failed {}", e.what());
	}
}