#include "dns.h"
#include "dns_details.h"
#include <array>
#include <print>
using boost::asio::ip::udp;
using namespace gamespy;

void HandlePacket(dns::dns_packet& packet)
{
	for (const auto& q : packet.questions) {
		if ((q.type == dns::dns_question::QTYPE::A || q.type == dns::dns_question::QTYPE::AAAA) && q.klass == dns::dns_question::QCLASS::INTERNET) {
			if (q.name.ends_with("gamespy.com") || q.name.ends_with("dice.se")) {
				auto data = std::vector<std::uint8_t>{};
				if (q.type == dns::dns_question::QTYPE::A)
					data.append_range(boost::asio::ip::address_v4::loopback().to_bytes());
				else
					data.append_range(boost::asio::ip::address_v6::loopback().to_bytes());

				packet.answers.push_back(dns::dns_resource{
					{
						.name = q.name,
						.type = q.type,
						.klass = q.klass,
					},
					std::chrono::seconds(180),
					std::move(data)
				});
			}

			packet.authoritative_answer = true;
		}
		else {
			packet.response_type = dns::dns_packet::RCODE::NXDomain;
			break;
		}
	}

	packet.response = true;
	packet.recursion_available = false;
}

DNSServer::DNSServer(boost::asio::io_context& context, GameDB& db)
	: m_Socket(context, udp::endpoint(udp::v6(), PORT)), m_DB(db)
{
	std::println("[dns] listening on {} for *.gamespy.com", PORT);
}

DNSServer::~DNSServer()
{

}

boost::asio::awaitable<void> DNSServer::AcceptConnections()
{
	auto buff = std::array<std::uint8_t, 512>{};
	while (m_Socket.is_open()) {
		udp::endpoint client;
		const auto& [error, length] = co_await m_Socket.async_receive_from(boost::asio::buffer(buff), client, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;
		else if (length == 0)
			continue;

		auto packet = dns::dns_packet::from_bytes(buff);
		if (!packet || packet->questions.size() == 0)
			continue;

		HandlePacket(*packet);
		co_await m_Socket.async_send_to(boost::asio::buffer(packet->to_bytes()), client, boost::asio::use_awaitable);
	}
}