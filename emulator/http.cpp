#include "http.h"
#include "http.client.h"
#include <print>
#include "gamedb.h"

using namespace gamespy;

HttpServer::HttpServer(boost::asio::io_context& context, GameDB& gameDB, PlayerDB& playerDB)
	: m_Acceptor{ context, boost::asio::ip::tcp::endpoint{ boost::asio::ip::tcp::v4(), 80 } }, m_GameDB{ gameDB }, m_PlayerDB{ playerDB }
{
	std::println("[http] starting up (battlefield 2 unlocker)");
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
			break;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> HttpServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		auto client = HttpClient{ std::move(socket), m_GameDB, m_PlayerDB };
		co_await client.Run();
	}
	catch (const std::exception& e) {
		std::println("[http] connection failed {}", e.what());
	}
}