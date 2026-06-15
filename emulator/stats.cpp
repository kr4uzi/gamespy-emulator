#include "stats.h"
#include "stats.client.h"
#include <print>
#include <utility>
using namespace gamespy;
namespace net = boost::asio;
using tcp = net::ip::tcp;

StatsServer::StatsServer(net::io_context& context, GameDB& gameDB, PlayerDB& playerDB, std::optional<boost::asio::ip::tcp::endpoint> snapshotEndpoint)
	: m_Acceptor{ context, tcp::endpoint{ tcp::v4(), PORT } }, m_GameDB{ gameDB }, m_PlayerDB{ playerDB }, m_SnapshotEndpoint{ std::move(snapshotEndpoint) }
{
	std::println("[stats] starting up: {} TCP", PORT);
	std::println("[stats] (*.gamestats.gamespy.com)");

	if (m_SnapshotEndpoint)
		std::println("[stats] snapshot forwarding enabled to {}:{}", snapshotEndpoint->address().to_string(), snapshotEndpoint->port());
	else
		std::println("[stats] snapshot forwarding disabled");
}

StatsServer::~StatsServer()
{
	std::println("[stats] shutting down");
}

boost::asio::awaitable<void> StatsServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(net::as_tuple(net::use_awaitable));
		if (error)
			break;

		net::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), net::detached);
	}
}

boost::asio::awaitable<void> StatsServer::HandleIncoming(tcp::socket socket)
{
	// remote_endpoint() throws if the peer already reset the connection; since this
	// coroutine is spawned detached, an escaping exception would terminate the whole
	// io_context, so query it without throwing instead.
	boost::system::error_code ec;
	auto remote = socket.remote_endpoint(ec);
	auto addr = ec ? std::string{ "<unknown>" } : remote.address().to_string();
	try {
		StatsClient client(std::move(socket), m_GameDB, m_PlayerDB, m_SnapshotEndpoint);
		co_await client.Process();
	}
	catch (const std::exception& e) {
		std::println("[stats][error]{} - {}", addr, e.what());
	}
}