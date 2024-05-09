#include "http.h"
#include "http.client.h"
#include <print>
#include "gamedb.h"

using namespace gamespy;

HttpServer::HttpServer(boost::asio::io_context& context, GameDB& db)
	: m_Acceptor{ context, boost::asio::ip::tcp::endpoint{ boost::asio::ip::tcp::v4(), 80 } }, m_DB{ db }
{
	std::println("[http] starting up: {} TCP", 80);
}

HttpServer::~HttpServer()
{
	std::println("[http] shutting down");
}

boost::asio::awaitable<void> HttpServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			continue;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> HttpServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		auto client = HttpClient{ std::move(socket), m_DB };
		co_await client.Run();
	}
	catch (const std::exception& e) {
		std::println("[http] connection failed {}", e.what());
	}
}