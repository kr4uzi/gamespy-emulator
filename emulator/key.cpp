#include "key.h"
#include "utils.h"
#include <print>
using namespace gamespy;
using boost::asio::ip::udp;

CDKeyServer::CDKeyServer(boost::asio::io_context& context)
	: m_Socket{ context, udp::endpoint{ udp::v4(), PORT } }
{
	std::println("[cd-key] starting up: {} UDP", PORT);
}

CDKeyServer::~CDKeyServer()
{
	std::println("[cd-key] shutting down");
}

boost::asio::awaitable<void> CDKeyServer::AcceptConnections()
{
	std::array<char, 1400> buff;
	while (m_Socket.is_open()) {
		udp::endpoint client;
		const auto& [error, length] = co_await m_Socket.async_receive_from(boost::asio::buffer(buff), client, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;
		else if (length == 0)
			continue;

		auto message = std::span{ buff }.subspan(0, length);
		utils::gs_xor(message, utils::xor_types::gamespy);
		auto packet = std::string_view{ buff.data(), length };
		if (packet.starts_with("\\ka\\")) {
			// ignore keep alive
			continue;
		}
		else if (packet.starts_with("\\disc\\")) {
			// ignore disconnects
			continue;
		} else if (packet.starts_with("\\auth\\")) {
			auto cdKey = utils::value_for_key(packet, "\\skey\\");
			auto challenge = utils::value_for_key(packet, "\\resp\\");
			if (cdKey && challenge) {
				auto response = std::format(R"(\uok\\cd\{}\skey\{})", challenge->substr(0, 32), *cdKey);
				utils::gs_xor(response, utils::xor_types::gamespy);
				co_await m_Socket.async_send_to(boost::asio::buffer(response), client, boost::asio::use_awaitable);
			}
		}
	}
}