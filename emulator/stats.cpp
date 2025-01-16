#include "stats.h"
#include "stats.client.h"
#include <print>
#include <utility>
using namespace gamespy;

StatsServer::StatsServer(boost::asio::io_context& context, GameDB& db)
	: m_Acceptor{ context, boost::asio::ip::tcp::endpoint{ boost::asio::ip::tcp::v4(), PORT } }, m_DB{ db }
{
	std::println("[stats] starting up: {} TCP", PORT);
	std::println("[stats] (*.gamestats.gamespy.com)");
}

StatsServer::~StatsServer()
{
	std::println("[stats] shutting down");
}

boost::asio::awaitable<void> StatsServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> StatsServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	auto addr = socket.remote_endpoint().address().to_string();
	try {
		StatsClient client(std::move(socket), m_DB);
		co_await client.Process();
	}
	catch (const std::exception& e) {
		std::println("[stats][error]{} - {}", addr, e.what());
	}
}