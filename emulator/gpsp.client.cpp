#include "gpsp.client.h"
#include "textpacket.h"
#include "playerdb.h"
#include "utils.h"
#include <format>
#include <print>
#include <cctype>
#include <ranges>
using namespace gamespy;

SearchClient::SearchClient(boost::asio::ip::tcp::socket socket, PlayerDB& db)
	: m_Socket(std::move(socket)), m_DB(db)
{

}

SearchClient::~SearchClient()
{

}

boost::asio::awaitable<void> SearchClient::HandleRetrieveProfiles(const TextPacket& packet)
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
		std::string password = utils::passdecode(std::string{ passEncIter->second });
		passwordMD5 = utils::md5(passIter->second);
	}

	if (passwordMD5.empty()) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}

	const auto email = emailIter->second | std::views::transform((int(*)(int))std::tolower) | std::ranges::to<std::string>();
	auto players = co_await m_DB.GetPlayerByMailAndPassword(email, passwordMD5);
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

boost::asio::awaitable<void> SearchClient::HandleProfileExists(const TextPacket& packet)
{
	auto nickIter = packet.values.find("nick");
	auto uniqueNickIter = packet.values.find("uniquenick");
	std::string_view name;
	if (nickIter != packet.values.end())
		name = nickIter->second;
	else if (uniqueNickIter != packet.values.end())
		name = uniqueNickIter->second;

	if (name.empty()) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}
	
	if (co_await m_DB.HasPlayer(name)) {
		auto playerData = co_await m_DB.GetPlayerByName(name);
		auto response = std::format(R"(\cur\0\pid\{}\final\)", playerData->GetProfileID());
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
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, buff, TextPacket::PACKET_END, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			break;
		
		auto theSize = buff.size();
		auto packet = TextPacket::parse(std::span{ boost::asio::buffer_cast<const char*>(buff.data()), buff.size() });
		if (!packet) {
			std::println("[search] failed to parse packet");
			break;
		}

		if (packet->type == "nicks")
			co_await HandleRetrieveProfiles(*packet);
		else if (packet->type == "check")
			co_await HandleProfileExists(*packet);
		else {
			std::println("[search] received unknown packet of type {}: {}", packet->type, packet->str());
			co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		}

		buff.consume(buff.size());
	}
}