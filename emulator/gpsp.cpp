#include "sessions.h"
#include "gpsp.h"
#include "gpsp.client.h"
#include <print>
#include <utility>
using namespace gamespy;

SearchServer::SearchServer(boost::asio::io_context& context, PlayerDB& db, Sessions& sessions)
	: m_Acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), PORT)), m_Sessions(sessions), m_DB(db)
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
		if (error == boost::asio::error::operation_aborted) co_return;
		if (error) throw boost::system::system_error(error);

		m_Sessions.Spawn(HandleIncoming(std::move(socket)));
	}
}

boost::asio::awaitable<void> SearchServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	// A peer may reset the connection before its session starts.
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