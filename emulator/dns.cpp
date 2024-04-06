#include "dns.h"
#include "dns_details.h"
#include "database.h"
#include <array>
#include <print>
using boost::asio::ip::udp;
using namespace gamespy;

void HandlePacket(dns::dns_packet& packet)
{
	for (const auto& q : packet.questions) {
		if ((q.type == dns::dns_question::QTYPE::A || q.type == dns::dns_question::QTYPE::AAAA) && q.klass == dns::dns_question::QCLASS::INTERNET) {
			if (q.name.ends_with("gamespy.com")) {
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

			//packet.is_authoritative_answer = true;
		}
		else {
			packet.response_type = dns::dns_packet::RCODE::NXDomain;
			break;
		}
	}

	packet.response = true;
	packet.recursion_available = 0;
}

DNSServer::DNSServer(boost::asio::io_context& context, Database& db)
	: m_Socket(context, udp::endpoint(udp::v6(), PORT)), m_DB(db)
{
	std::println("[dns] listening on {}", PORT);
}

DNSServer::~DNSServer()
{

}

#include <span>
boost::asio::awaitable<void> DNSServer::AcceptConnections()
{
	std::array<std::uint8_t, 512> buff;
	while (m_Socket.is_open()) {
		udp::endpoint client;
		const auto& [error, length] = co_await m_Socket.async_receive_from(boost::asio::buffer(buff), client, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error || length == 0)
			continue;

		auto packet = dns::dns_packet::from_bytes(buff);
		if (!packet) {
			std::println("[dns] failed to parse packet");
			continue;
		}
		else if (packet->questions.size() == 0) {
			std::println("[dns] no questions");
			continue;
		}

		for (const auto& q : packet->questions)
			std::println("dns query ({}): {} - {}", client.protocol() == udp::v6() ? "v6" : "v4", q.name, (int)std::to_underlying(q.type));

		HandlePacket(*packet);
		co_await m_Socket.async_send_to(boost::asio::buffer(packet->to_bytes()), client, boost::asio::use_awaitable);
	}
}