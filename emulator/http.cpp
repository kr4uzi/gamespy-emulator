#include "http.h"
#include "http.client.h"
#include <print>
#include "gamedb.h"

using namespace gamespy;

HttpServer::HttpServer(boost::asio::io_context& context)
	: m_Acceptor{ context, boost::asio::ip::tcp::endpoint{ boost::asio::ip::tcp::v4(), 80 } }
{
	std::println("[http] starting up: {} TCP", 80);
}

HttpServer::~HttpServer()
{
	std::println("[http] shutting down");
}

boost::asio::awaitable<void> HttpServer::AcceptClients(/*const Config::MySQLParameters& params*/)
{
	/*auto sslContext = boost::asio::ssl::context{boost::asio::ssl::context::tls_client};
	auto conn = boost::mysql::tcp_ssl_connection{ m_Acceptor.get_executor(), sslContext };

	auto resolver = boost::asio::ip::tcp::resolver{ m_Acceptor.get_executor() };
	const auto& endpoints = co_await resolver.async_resolve(params.host, std::to_string(params.port), boost::asio::use_awaitable);

	auto handshakeParams = boost::mysql::handshake_params{ params.username, params.password, params.database };
	handshakeParams.set_ssl(boost::mysql::ssl_mode::enable);
	std::println("[http] connecting to {}:{} db={}", params.host, params.port, params.database);
	co_await conn.async_connect(*endpoints, handshakeParams, boost::asio::use_awaitable);
	*/
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> HttpServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		auto client = HttpClient{ std::move(socket) };
		co_await client.Run();
	}
	catch (const std::exception& e) {
		std::println("[http] connection failed {}", e.what());
	}
}