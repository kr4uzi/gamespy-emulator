#include "gamedb.h"
#include "game.h"
#include "playerdb.h"
#include "admin.h"
#include "sessions.h"
#include "utils.h"
#include <print>
#include <utility>
#include <map>
#include <ranges>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <system_error>
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
	beast::tcp_stream m_Stream;
	GameDB& m_GameDB;
	PlayerDB& m_PlayerDB;
	std::string m_Auth;

public:
	AdminClient(tcp::socket socket, GameDB& gameDB, PlayerDB &playerDB, std::string_view auth)
		: m_Stream(std::move(socket)), m_GameDB(gameDB), m_PlayerDB(playerDB), m_Auth(auth)
	{

	}

	~AdminClient()
	{

	}

	boost::asio::awaitable<void> SendResponse(const http::request<http::string_body>& request, http::status status, std::optional<nlohmann::json> body = {})
	{
		auto response = http::response<http::string_body>{};
		response.result(status);
		response.version(request.version());
		response.keep_alive(false);
		if (body) {
			response.set(http::field::content_type, "application/json");
			response.body() = body->dump();
		}

		response.prepare_payload();
		co_await http::async_write(m_Stream, response, boost::asio::use_awaitable);
	}

	net::awaitable<void> SendError(const http::request<http::string_body>& request, http::status status, std::string message)
	{
		auto body = nlohmann::json{{"error", std::move(message)}};
		co_await SendResponse(request, status, std::move(body));
	}

	net::awaitable<bool> HandleAPI(const http::request<http::string_body>& request, const boost::core::string_view& path, const boost::urls::params_view& params)
	{
		auto method = request.method();
		if (path == "/api/games") {
			if (method != http::verb::get) {
				co_await SendError(request, http::status::bad_request, "invalid http method");
				co_return false;
			}

			nlohmann::json games = nlohmann::json::array();
			for (const auto& game : co_await m_GameDB.GetGames()) {
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
			co_await SendError(request, http::status::bad_request, "missing game parameter");
			co_return false;
		}

		auto game = co_await m_GameDB.GetGame((*params.find("game"))->value);
		if (!game) {
			co_await SendError(request, http::status::bad_request, "unknown game");
			co_return false;
		}

		if (path == "/api/servers") {
			if (request.method() != http::verb::get) {
				co_await SendError(request, http::status::bad_request, "invalid http method");
				co_return false;
			}

			auto query = params.contains("query") ? (*params.find("query"))->value : "";
			auto limit = params.contains("limit") ? utils::parse_uint32((*params.find("limit"))->value) : std::optional<std::uint32_t>{100};
			auto skip = params.contains("skip") ? utils::parse_uint32((*params.find("skip"))->value) : std::optional<std::uint32_t>{0};
			if (!limit || *limit == 0 || *limit > 1000 || !skip) {
				co_await SendError(request, http::status::bad_request, "limit must be 1..1000 and skip a nonnegative integer");
				co_return false;
			}

			std::vector<std::string> fields;
			if (auto _fields = params.find("fields"); _fields != params.end() && (*_fields)->has_value)
				fields = (*_fields)->value | std::views::split(',') | std::ranges::to<std::vector<std::string>>();

			nlohmann::json servers = nlohmann::json::array();
			for (const auto& server : co_await game->GetServers(query, fields | std::ranges::to<std::vector<std::string_view>>(), *limit, *skip)) {
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
				co_await SendError(request, http::status::bad_request, "missing ip or port parameter");
				co_return false;
			}

			auto port = utils::parse_uint32((*params.find("port"))->value);
			if (!port || *port == 0 || *port > 65535) {
				co_await SendError(request, http::status::bad_request, "invalid port parameter");
				co_return false;
			}

			boost::system::error_code addressError;
			boost::asio::ip::make_address_v4(std::string{(*params.find("ip"))->value}, addressError);
			if (addressError) {
				co_await SendError(request, http::status::bad_request, "invalid IPv4 address");
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
				auto response = nlohmann::json{{"status", "success"}, {"message", "added server"}};
				co_await SendResponse(request, http::status::ok, std::move(response));
				co_return true;
			}
			else if (request.method() == http::verb::delete_) {
				const std::string ip{(*params.find("ip"))->value};
				const std::vector<std::pair<std::string_view, std::uint16_t>> servers{{ip, static_cast<std::uint16_t>(*port)}};
				co_await game->RemoveServers(servers);
				auto response = nlohmann::json{{"status", "success"}, {"message", "removed server"}};
				co_await SendResponse(request, http::status::ok, std::move(response));
				co_return true;
			}
			
			co_await SendError(request, http::status::bad_request, "invalid http method");
			co_return false;
		}

		co_await SendResponse(request, http::status::not_found);
		co_return false;
	}

	net::awaitable<bool> HandleFileLookup(const http::request<http::string_body>& request, const std::string_view& path)
	{
		std::error_code fsErr;
		const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("http"), fsErr);
		auto filePath = std::filesystem::weakly_canonical(root / (path == "/" ? "index.html" : path.substr(1)), fsErr);

		// prevent path traversal (e.g. "/../../etc/passwd"): the resolved file must stay within root
		auto match = std::mismatch(root.begin(), root.end(), filePath.begin(), filePath.end());
		if (fsErr || root.empty() || match.first != root.end()) {
			co_await SendResponse(request, http::status::not_found);
			co_return false;
		}

		if (!std::filesystem::exists(filePath)) {
			co_await SendResponse(request, http::status::not_found);
			co_return false;
		}

		beast::error_code fileErr;
		http::file_body::value_type body;
		body.open(filePath.string().c_str(), boost::beast::file_mode::scan, fileErr);
		if (fileErr) {
			co_await SendError(request, http::status::internal_server_error, "failed to open file");
			co_return false;
		}

		auto size = body.size();
		http::response<http::file_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(http::status::ok, request.version())
		};
		res.set(http::field::content_type, ::mimeType(filePath.string()));
		res.content_length(size);
		res.keep_alive(false);
		co_await http::async_write(m_Stream, res, net::use_awaitable);
		co_return true;
	}

	net::awaitable<void> Run()
	{
		auto addr = m_Stream.socket().remote_endpoint().address();

		m_Stream.expires_after(std::chrono::seconds(10));
		auto buffer = beast::flat_buffer{8192};
		http::request_parser<http::string_body> parser;
		parser.header_limit(8192);
		parser.body_limit(8192);
		auto [error, length] = co_await http::async_read(m_Stream, buffer, parser, net::as_tuple(net::use_awaitable));
		if (error) co_return;
		auto request = parser.release();

		// Browser-originated writes must come from this admin UI. CLI clients may omit Origin.
		if (request.method() != http::verb::get && request.method() != http::verb::head) {
			const auto origin = request.find(http::field::origin);
			const auto expectedOrigin = std::string{"http://"} + std::string{request[http::field::host]};
			if (beast::iequals(request["Sec-Fetch-Site"], "cross-site")
				|| (origin != request.end() && !beast::iequals(origin->value(), expectedOrigin))) {
				co_await SendError(request, http::status::forbidden, "cross-origin writes are not allowed");
				co_return;
			}
		}

		// only host itself is allowed to bypass authentication
		if (!addr.is_loopback() && addr != m_Stream.socket().local_endpoint().address()) {
			auto auth = request.find(http::field::authorization);
			if (auth == request.end() || auth->value() != m_Auth) {
				std::println("[admin] unauthorized access attempt from {}", addr.to_string());
				auto response = http::response<http::string_body>{};
				response.result(http::status::unauthorized);
				response.set(http::field::www_authenticate, "Basic realm=\"Authentication required\"");
				response.version(request.version());
				response.keep_alive(false);
				response.prepare_payload();
				co_await http::async_write(m_Stream, response, boost::asio::use_awaitable);
				co_return;
			}

			std::println("[admin] authorized access from {}", addr.to_string());
		}

		auto uri = boost::urls::parse_origin_form(request.target());
		if (!uri) {
			co_await SendError(request, http::status::bad_request, "invalid request target");
			co_return;
		}
		auto path = uri->path();
		auto params = uri->params();
		
		if (path.starts_with("/api/"))
			co_await HandleAPI(request, path, params);
		else
			co_await HandleFileLookup(request, path);
	}
};

AdminServer::AdminServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB, const std::string& username, const std::string& password, boost::asio::ip::port_type port, Sessions& sessions)
	: m_Acceptor(context, tcp::v6()), m_GameDB(gameDB), m_PlayerDB(playerDB), m_Sessions(sessions)
{
	m_Acceptor.set_option(tcp::acceptor::reuse_address(true));
	if (username.empty() || password.empty()) {
		m_Acceptor.bind(tcp::endpoint(boost::asio::ip::make_address("::1"), port));
		std::println("[admin] listening on port {} (loopback only, no authentication)", port);
	}
	else {
		m_Auth = "Basic " + utils::base64_encode(std::format("{}:{}", username, password));
		m_Acceptor.bind(tcp::endpoint(tcp::v6(), port));

		auto shownpw = std::string(password.size(), '*');
		std::println("[admin] listening on port {}, username={}, password={} (internet)", port, username, shownpw);
	}

	m_Acceptor.listen();
}

AdminServer::~AdminServer()
{

}

boost::asio::awaitable<void> AdminServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(net::as_tuple);
		if (error == boost::asio::error::operation_aborted) co_return;
		if (error == boost::asio::error::connection_aborted) continue;
		if (error) throw boost::system::system_error{error};

		m_Sessions.Spawn(HandleIncoming(std::move(socket)));
	}
}

boost::asio::awaitable<void> AdminServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		auto client = AdminClient{ std::move(socket), m_GameDB, m_PlayerDB, m_Auth };
		co_await client.Run();
	}
	catch (const std::exception& e) {
		std::println("[admin] connection failed {}", e.what());
	}
}