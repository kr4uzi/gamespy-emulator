#include "gpcm.client.h"
#include "utils.h"
#include "textpacket.h"
#include <boost/crc.hpp>
#include <format>
#include <print>
#include <cctype>
#include <ranges>
using namespace gamespy;

using namespace std::string_literals;

LoginClient::LoginClient(boost::asio::ip::tcp::socket socket, Database& db)
	: m_Socket(std::move(socket)), m_HeartBeatTimer(m_Socket.get_executor()), m_DB(db)
{

}

LoginClient::~LoginClient()
{

}

boost::asio::awaitable<void> LoginClient::KeepAliveClient()
{
	m_HeartBeatTimer = boost::asio::deadline_timer(co_await boost::asio::this_coro::executor);
	while (true) {
		m_HeartBeatTimer.expires_from_now(boost::posix_time::seconds(60));
		auto [error] = co_await m_HeartBeatTimer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) {
			std::println("{}", error.what());
			break;
		}

		auto heartBeat = std::format(R"(\lt\{}__\final\)", utils::random_string("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ][", 22));
		auto keepAlive = R"(\ka\\final\)";

		co_await m_Socket.async_send(boost::asio::buffer(heartBeat + keepAlive), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> LoginClient::SendChallenge()
{
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10);

	auto response = std::format(R"(\lc\1(\challenge\{}\id\1\final\)", m_ServerChallenge);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);

	m_State = STATES::AUTHENTICATING;
}

boost::asio::awaitable<void> LoginClient::HandleLogin(const TextPacket& packet)
{
	if (m_State != STATES::AUTHENTICATING) {
		std::print("[login] received login package in non-authenticating state");
		co_return;
	}

	auto nameIter = packet.values.find("uniquenick");
	auto clientChallengeIter = packet.values.find("challenge");
	auto responseIter = packet.values.find("response");
	if (nameIter == packet.values.end() || clientChallengeIter == packet.values.end() || responseIter == packet.values.end()) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"s), boost::asio::use_awaitable);
		co_return;
	}
		
	if (!m_DB.HasPlayer(nameIter->second)) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\265\fatal\\errmsg\Username [)" + std::string{ nameIter->second } + R"(] doesn't exist!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}

	auto player = m_DB.GetPlayerByName(nameIter->second);
	if (responseIter->second != utils::generate_challenge(nameIter->second, player.password, clientChallengeIter->second, m_ServerChallenge)) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\260\fatal\\errmsg\The password provided is incorrect.\id\1\final\)"s), boost::asio::use_awaitable);
		co_return;
	}
	
	m_PlayerData = player;

	boost::crc_16_type session;
	session.process_bytes(nameIter->second.data(), nameIter->second.length());
	m_PlayerData->session = session.checksum();

	auto proof = utils::generate_challenge(m_PlayerData->name, player.password, m_ServerChallenge, clientChallengeIter->second);
	auto lt = utils::random_string("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ][", 22);
	auto response = std::format(R"(\lc\2\sesskey\{}\proof\{}\userid\{}\profileid\{}\uniquenick\{}\lt\{}__\id\1\final\)", m_PlayerData->session, proof, player.GetUserID(), player.GetProfileID(), player.name, lt);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);

	//boost::asio::co_spawn(m_Socket.get_executor(), KeepAliveClient(), boost::asio::detached);
	m_State = STATES::AUTHENTICATED;
	//m_HeartBeatTimer.cancel();
}

boost::asio::awaitable<void> LoginClient::HandleNewUser(const TextPacket& packet)
{
	if (m_State != STATES::AUTHENTICATING) {
		std::print("[login] received newuser package in non-authenticating state");
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		co_return;
	}
	
	auto nameIter = packet.values.find("nick");
	auto emailIter = packet.values.find("email");
	auto passwordEncIter = packet.values.find("passwordenc");
	if (nameIter == packet.values.end() || emailIter == packet.values.end() || passwordEncIter == packet.values.end()) {
		std::println("[login] unexpected newuser packet (missing nick|email or password: {}", packet.str());
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
	}

	if (m_DB.HasPlayer(nameIter->second)) {
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\516\fatal\\errmsg\This account name is already in use!\id\1\final\)"s), boost::asio::use_awaitable);
		co_return;
	}

	std::string password = utils::passdecode(std::string{ passwordEncIter->second });
	if (password.length() < 3)
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\The password is too short, must be 3 characters at least!\id\1\final\)"), boost::asio::use_awaitable);
	else if (password.length() > 30)
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\The password is too long, must be 30 characters at most!\id\1\final\)"), boost::asio::use_awaitable);
	else {
		m_PlayerData.emplace(nameIter->second, emailIter->second, utils::md5(password), "??");
		m_DB.CreatePlayer(*m_PlayerData);
		if (!m_DB.HasError()) {
			auto response = std::format(R"(\nur\\userid\{}\profileid\{}\id\1\final\)", m_PlayerData->GetUserID(), m_PlayerData->GetProfileID());
			co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
		}
		else
			co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Error creating account!\id\1\final\)"s), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> LoginClient::HandleGetProfile(const TextPacket& packet)
{
	if (m_State == STATES::AUTHENTICATED) {
		co_await m_Socket.async_send(boost::asio::buffer(GeneratePlayerData()), boost::asio::use_awaitable);

		// when testing get profile while the official gamespy server was still running,
		// i noticed that getprofile returns slightly different messages (first time id=2, second time id=5)
		m_ProfileDataSent = true;
	}
	else {
		std::println("[login] received getprofile package in non-authenticated state");
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> LoginClient::HandleUpdateProfile(const TextPacket& packet)
{
	if (m_State == STATES::AUTHENTICATED) {
		auto countryIter = packet.values.find("countrycode");
		if (countryIter != packet.values.end()) {
			auto oldCountry = m_PlayerData->country;
			m_PlayerData->country = countryIter->second | std::views::transform((int(*)(int))std::toupper) | std::ranges::to<std::string>();
			m_DB.UpdatePlayer(*m_PlayerData);
			if (m_DB.HasError()) {
				m_PlayerData->country = oldCountry;
				co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Error updating account!\id\1\final\)"s), boost::asio::use_awaitable);
			}
		}
	}
	else {
		std::println("[login] received updatepro package in non-authenticated state");
		co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> LoginClient::HandleLogout(const TextPacket& packet)
{
	m_Socket.close();
	co_return;
}

boost::asio::awaitable<void> LoginClient::Process()
{
	co_await SendChallenge();

	boost::asio::streambuf buff;
	while (m_Socket.is_open()) {
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, buff, TextPacket::PACKET_END, boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) {
			break;
		}

		auto packet = TextPacket::parse(std::span{ boost::asio::buffer_cast<const char*>(buff.data()), buff.size() });
		if (!packet) {
			std::println("[login] failed to parse packet");
			break;
		}

		if (packet->type == "login")
			co_await HandleLogin(*packet);
		else if (packet->type == "newuser")
			co_await HandleNewUser(*packet);
		else if (packet->type == "getprofile")
			co_await HandleGetProfile(*packet);
		else if (packet->type == "updatepro")
			co_await HandleUpdateProfile(*packet);
		else if (packet->type == "logout")
			co_await HandleLogout(*packet);
		else {
			std::println("[login] received unknown packet of type {}: {}", packet->type, packet->str());
			co_await m_Socket.async_send(boost::asio::buffer(R"(\error\\err\0\fatal\\errmsg\Invalid Query!\id\1\final\)"), boost::asio::use_awaitable);
		}

		buff.consume(buff.size());
	}
}

std::string LoginClient::GeneratePlayerData() const
{
	return std::format(R"(\pi\\profileid\{}\nick\{}\userid\{}\email\{}\sig\{}\uniquenick\{}\firstname\\lastname\\countrycode\{}\birthday\16844722\lon\0.000000\lat\0.000000\loc\\id\{}\final\)",
		m_PlayerData->GetProfileID(), m_PlayerData->name, m_PlayerData->GetUserID(), m_PlayerData->email, utils::random_string("0123456789abcdef", 32), m_PlayerData->name, m_PlayerData->country, m_ProfileDataSent ? 5 : 2);
}