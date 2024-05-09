#include "ms.h"
#include "ms.client.h"
#include <print>
#include <utility>
using namespace gamespy;

BrowserServer::BrowserServer(boost::asio::io_context& context, GameDB& db)
	: m_Acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), PORT)),  m_DB(db)
{
	std::println("[browser] starting up: {} TCP", PORT);
	std::println("[browser] (%s.ms%d.gamespy.com)");
}

BrowserServer::~BrowserServer()
{
	std::println("[browser] shutting down");
}

boost::asio::awaitable<void> BrowserServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto socket = co_await m_Acceptor.async_accept(boost::asio::use_awaitable);
		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> BrowserServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	try {
		BrowserClient client(std::move(socket), m_DB);
		co_await client.Process();
	}
	catch (std::exception& e) {
		std::println("[ms]error: {}", e.what());
	}
}