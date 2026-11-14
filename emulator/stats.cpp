#include "sessions.h"
#include "stats.h"
#include "stats.client.h"
#include <print>
#include <utility>
using namespace gamespy;
namespace net = boost::asio;
using tcp = net::ip::tcp;

StatsServer::StatsServer(net::io_context& context, GameDB& gameDB, PlayerDB& playerDB, std::optional<boost::asio::ip::tcp::endpoint> snapshotEndpoint, Sessions& sessions)
	: m_Acceptor{ context, tcp::endpoint{ tcp::v4(), PORT } }, m_Sessions(sessions), m_SnapshotEndpoint{ std::move(snapshotEndpoint) }, m_GameDB{ gameDB }, m_PlayerDB{ playerDB }
{
	std::println("[stats] starting up: {} TCP", PORT);
	std::println("[stats] (*.gamestats.gamespy.com)");

	if (m_SnapshotEndpoint)
		std::println("[stats] snapshot forwarding enabled to {}:{}", m_SnapshotEndpoint->address().to_string(), m_SnapshotEndpoint->port());
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
		if (error == boost::asio::error::operation_aborted) co_return;
		if (error) throw boost::system::system_error(error);

		m_Sessions.Spawn(HandleIncoming(std::move(socket)));
	}
}

boost::asio::awaitable<void> StatsServer::HandleIncoming(tcp::socket socket)
{
	// A peer may reset the connection before its session starts.
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
