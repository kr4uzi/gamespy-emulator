#include "gpsp.client.h"
#include "utils.h"
#include "database.h"
#include <format>
#include <print>
using namespace gamespy;

SearchClient::SearchClient(boost::asio::ip::tcp::socket socket, Database& db)
	: m_Socket(std::move(socket)), m_DB(db)
{

}

SearchClient::~SearchClient()
{

}

boost::asio::awaitable<void> SearchClient::HandleRetrieveProfiles(const utils::TextPacket& packet)
{
	auto emailIter = packet.values.find("email");
	auto passIter = packet.values.find("pass");
	auto passEncIter = packet.values.find("passenc");
	if (emailIter == packet.values.end() || (passIter == packet.values.end() && passEncIter == packet.values.end())) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}

	std::string passwordMD5;
	if (passIter != packet.values.end()) {
		passwordMD5 = utils::md5(passIter->second);
	}
	else if (passEncIter != packet.values.end()) {
		std::string password = utils::passdecode(passEncIter->second);
		passwordMD5 = utils::md5(passIter->second);
	}

	if (passwordMD5.empty()) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}

	std::string email = emailIter->second;
	//std::transform(email.begin(), email.end(), email.begin(), std::tolower);

	auto players = m_DB.GetPlayerByMailAndPassword(email, passwordMD5);
	if (players.empty())
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\551\fatal\\errmsg\Unable to get any associated profiles.\id\1\final\)"), boost::asio::use_awaitable);
	else {
		auto response = std::format(R"(\nr\{})", players.size());

		for (const auto& player : players)
			response += R"(\nick\)" + player.name + R"(\uniquenick\)" + player.name;

		response += R"(\ndone\final\)";
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
	}	
}

boost::asio::awaitable<void> SearchClient::HandleProfileExists(const utils::TextPacket& packet)
{
	auto nickIter = packet.values.find("nick");
	auto uniqueNickIter = packet.values.find("uniquenick");
	std::string name;
	if (nickIter != packet.values.end())
		name = nickIter->second;
	else if (uniqueNickIter != packet.values.end())
		name = uniqueNickIter->second;

	if (name.empty()) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}
	
	if (m_DB.HasPlayer(name)) {
		auto playerData = m_DB.GetPlayerByName(name);
		auto response = std::format(R"(\cur\0\pid\{}\final\)", playerData.GetProfileID());
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
	}
	else {
		auto response = std::format(R"(\error\\err\265\fatal\\errmsg\Username [{}] doesn't exist!\id\1\final\)", name);
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> SearchClient::Process()
{
	boost::asio::streambuf buff;
	while (m_Socket.is_open()) {
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, buff, utils::TextPacket::PACKET_END, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) {
			std::println("[search] error: {}", error.what());
			break;
		}

		auto incoming = utils::TextPacket::parse(boost::asio::buffer_cast<const char*>(buff.data()));
		while (!incoming.empty()) {
			const auto& packet = incoming.front();

			if (packet.type == "nicks")
				co_await HandleRetrieveProfiles(packet);
			else if (packet.type == "check")
				co_await HandleProfileExists(packet);
			else
				std::print("[search] received unknown packet of type {}: {}", packet.type, packet.str());

			incoming.pop();
		}

		buff.consume(length);
	}
}