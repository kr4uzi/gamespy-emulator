#include "sessions.h"
#include "ms.h"
#include "ms.client.h"
#include <print>
#include <utility>
using namespace gamespy;

BrowserServer::BrowserServer(boost::asio::io_context& context, GameDB& db, Sessions& sessions)
	: m_Acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), PORT)), m_Sessions(sessions), m_DB(db)
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
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error == boost::asio::error::operation_aborted) co_return;
		if (error) throw boost::system::system_error(error);

		m_Sessions.Spawn(HandleIncoming(std::move(socket)));
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