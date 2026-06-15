#include "gpsp.h"
#include "gpsp.client.h"
#include <print>
#include <utility>
using namespace gamespy;

SearchServer::SearchServer(boost::asio::io_context& context, PlayerDB& db)
	: m_Acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), PORT)), m_DB(db)
{
	std::println("[search] starting up: {} TCP", PORT);
	std::println("[search] (gpsp.gamespy.com)");
}

SearchServer::~SearchServer()
{
	std::println("[search] shutting down");
}

boost::asio::awaitable<void> SearchServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> SearchServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	// remote_endpoint() throws if the peer already reset the connection; since this
	// coroutine is spawned detached, an escaping exception would terminate the whole
	// io_context, so query it without throwing instead.
	boost::system::error_code ec;
	auto remote = socket.remote_endpoint(ec);
	auto addr = ec ? std::string{ "<unknown>" } : remote.address().to_string();
	try {
		SearchClient client(std::move(socket), m_DB);
		co_await client.Process();
	}
	catch (const std::exception& e) {
		std::println("[gpsp][error]{} - {}", addr, e.what());
	}
}