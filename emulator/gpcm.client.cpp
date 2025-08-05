#include "gpcm.client.h"
#include "utils.h"
#include <boost/crc.hpp>
#include <GameSpy/GP/gpi.h>
#include <GameSpy/GP/gpiOperation.h>
#include <format>
#include <print>
using namespace gamespy;
using namespace std::string_literals;

LoginClient::LoginClient(boost::asio::ip::tcp::socket socket, PlayerDB& playerDB)
	: m_Socket(std::move(socket)), m_HeartBeatTimer(m_Socket.get_executor()), m_PlayerDB(playerDB)
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

		auto heartBeat = std::format(R"(\lt\{}__\final\)", m_LoginTicket);
		auto keepAlive = R"(\ka\\final\)";

		co_await m_Socket.async_send(boost::asio::buffer(heartBeat + keepAlive), boost::asio::use_awaitable);
	}
}

boost::asio::awaitable<void> LoginClient::SendChallenge()
{
	m_ServerChallenge = utils::random_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ", sizeof(GPIConnectData::serverChallenge) - 1);
	m_LoginTicket = utils::random_string("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ][", sizeof(GPIConnection::loginTicket) - 3) + "__";

	auto response = std::format(R"(\lc\1\challenge\{}\id\1\final\)", m_ServerChallenge);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);

	m_State = STATES::AUTHENTICATING;
}

boost::asio::awaitable<void> LoginClient::HandleLogin(const std::span<const char>& packet)
{
	constexpr std::uint32_t requestId = 1;
	if (m_State != STATES::AUTHENTICATING) {
		std::println("[login] received login package in non-authenticating state");
		co_return;
	}

	auto uniqueNick = utils::value_for_key(packet, "\\uniquenick\\");
	auto user = utils::value_for_key(packet, "\\user\\");
	auto clientChallenge = utils::value_for_key(packet, "\\challenge\\");
	auto challengeResponse = utils::value_for_key(packet, "\\response\\");
	if ((!uniqueNick && !user) || !clientChallenge || !challengeResponse) {
		co_await SendError(requestId, 0, "Invalid Query!", true);
		co_return;
	}

	std::string_view playerName;
	if (uniqueNick)
		playerName = *uniqueNick;
	else if (user)
		playerName = *user; // <nick>@<email>

	if (playerName.empty()) {
		co_await SendError(requestId, 0, "Invalid Query!", true);
		co_return;
	}

	if (!co_await m_PlayerDB.HasPlayer(playerName)) {
		co_await SendError(requestId, 265, std::format("Username [{}] doesn't exist!", playerName), true);
		std::println("[login] unknown user: {}", playerName);
		co_return;
	}

	const auto& player = co_await m_PlayerDB.GetPlayerByName(playerName);
	if (*challengeResponse != utils::generate_challenge(playerName, player->password, *clientChallenge, m_ServerChallenge)) {
		co_await SendError(requestId, 260, "The password provided is incorrect.", true);
		std::println("[login] invalid password for user: {}", playerName);
		co_return;
	}
	
	m_PlayerData = player;

	boost::crc_16_type session;
	session.process_bytes(playerName.data(), playerName.length());
	m_PlayerData->session = session.checksum();

	auto proof = utils::generate_challenge(m_PlayerData->name, player->password, m_ServerChallenge, *clientChallenge);
	auto response = std::format(R"(\lc\2\sesskey\{}\userid\{}\profileid\{}\uniquenick\{}\lt\{}\proof\id\{}\final\)",
		m_PlayerData->session, m_PlayerData->GetUserID(), m_PlayerData->GetProfileID(), player->name, m_LoginTicket, proof, requestId
	);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);

	m_State = STATES::AUTHENTICATED;
	std::println("[login] authenticated user: {}", playerName);

	// currently not doing keepalive / heartbeat handling as it seems unnecessary (there is no timeout in the default gamespy implementation)
	//boost::asio::co_spawn(m_Socket.get_executor(), KeepAliveClient(), boost::asio::detached);
}

boost::asio::awaitable<void> LoginClient::HandleNewUser(const std::span<const char>& packet)
{
	constexpr std::uint32_t requestId = 1;
	if (m_State != STATES::AUTHENTICATING) {
		std::print("[login] received newuser package in non-authenticating state");
		co_await SendError(requestId, 0, "Invalid Query!", true);
		co_return;
	}
	
	auto email = utils::value_for_key(packet, "\\email\\");
	auto nick = utils::value_for_key(packet, "\\nick\\");
	auto passwordEnc = utils::value_for_key(packet, "\\passwordenc\\");
	// other keys in this packet, but unused: productid, gamename, namespaceid, uniquenick, cdkeyenc, partnerid
	if (!email || !nick || !passwordEnc) {
		co_await SendError(requestId, 0, "Invalid Query!", true);
		co_return;
	}

	if (co_await m_PlayerDB.HasPlayer(*nick)) {
		co_await SendError(requestId, 516, "This account name is already in use!", true);
		co_return;
	}

	const auto password = utils::passdecode(std::string{ passwordEnc->begin(), passwordEnc->end() });
	if (password.length() < 3)
		co_await SendError(requestId, 0, "The password is too short, must be 3 characters at least!", true);
	else if (password.length() > 30)
		co_await SendError(requestId, 0, "The password is too long, must be 30 characters at most!", true);
	else {
		m_PlayerData.emplace(*nick, *email, utils::md5(password), "??");
		co_await m_PlayerDB.CreatePlayer(*m_PlayerData);
		auto response = std::format(R"(\nur\\userid\{}\profileid\{}\id\1\final\)", m_PlayerData->GetUserID(), m_PlayerData->GetProfileID());
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
		std::println("[login] created new user: {}", *nick);
	}

#if 0
	auto game = utils::value_for_key(packet, "\\gamename\\");
	auto cdkey = utils::value_for_key(packet, "\\cdkeyenc\\");
	if (game && cdkey) {
		const auto cdkey = utils::passdecode(*cdkey);
		// TODO: Add cd-key to the player's profile
	}
#endif
}

boost::asio::awaitable<void> LoginClient::HandleGetProfile(const std::span<const char>& packet)
{
	auto requestId = utils::value_for_key<std::uint32_t>(packet, "\\id\\");
	if (!requestId) {
		co_await SendError(0, 0, "Invalid Query!", true);
		co_return;
	}

	if (m_State != STATES::AUTHENTICATED) {
		std::println("[login] received getprofile package in non-authenticated state");
		co_await SendError(*requestId, 0, "Invalid Query!", true);
		co_return;
	}

	co_await SendPlayerData(*requestId);
}

boost::asio::awaitable<void> LoginClient::HandleUpdateProfile(const std::span<const char>& packet)
{
	if (m_State != STATES::AUTHENTICATED) {
		std::println("[login] received updatepro package in non-authenticated state");
		co_await SendError(-1, 0, "Invalid Query!", true);
		co_return;
	}

	auto countryCode = utils::value_for_key<std::string>(packet, "\\countrycode\\");
	if (countryCode) {
		std::transform(countryCode->begin(), countryCode->end(), countryCode->begin(), ::toupper);
		m_PlayerData->country = *countryCode;
		co_await m_PlayerDB.UpdatePlayer(*m_PlayerData);
	}
}

boost::asio::awaitable<void> LoginClient::HandleLogout(const std::span<const char>& packet)
{
	m_Socket.close();
	co_return;
}

boost::asio::awaitable<void> LoginClient::Process()
{
	co_await SendChallenge();

	boost::asio::streambuf buff;
	while (m_Socket.is_open()) {
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, buff, "\\final\\", boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) break;

		auto packet = std::span<const char>{ boost::asio::buffer_cast<const char*>(buff.data()), buff.size() };
		auto textPacket = std::string_view{ packet.begin(), packet.end() };
		// all requests (gpiConnect.c):
		// login (challenge, authtoken?, uniquenick|user[nick@email]), userid?, profileid?, partnerid, response, firewall=1?, port, productid, gamename, namespaceid, sdkrevision, quiet, id=1) 
		// newuser (email, nick, passwordenc, productid, gamename, namespaceid, uniquenick, cdkeyenc?, partnerid, id=1)*
		// getprofile (sesskey, profileid, id)
		// updatepro (sesskey, (zipcode|sex[0=male, 1=female, 2=pat]|icquin|pic|occ|ind|inc|mar|chc|i1)?, partnernid) "local info"
		// updateui (sesskey, (cpubrandid|cpuspeed|memory|videocard1ram|videocard2ram|connectionid|connectionspeed|hasnetwork)*) "user info"
		// logout (sesskey)
		// * TODO: check if cdkey or cdkeyenc or both are sent (newer gamespy clients only send cdkeyenc)

		if (textPacket.starts_with("\\login\\"))
			co_await HandleLogin(packet);
		else if (textPacket.starts_with("\\newuser\\"))
			co_await HandleNewUser(packet);
		else if (textPacket.starts_with("\\getprofile\\"))
			co_await HandleGetProfile(packet);
		else if (textPacket.starts_with("\\updatepro\\"))
			co_await HandleUpdateProfile(packet);
		else if (textPacket.starts_with("\\logout\\"))
			co_await HandleLogout(packet);
		else {
			std::println("[login] unhandled packet: {}", textPacket);
			co_await SendError(0, 0, "Invalid Query!");
		}

		buff.consume(packet.size());
	}

	if (m_PlayerData) {
		std::println("[login] user disconnected: {}", m_PlayerData->name);
	}
}

boost::asio::awaitable<void> LoginClient::SendError(std::uint32_t requestId, std::uint32_t errorCode, const std::string_view& errorMessage, bool fatal)
{
	auto response = std::format(R"(\error\\err\{}\errmsg\{})", errorCode, errorMessage);
	if (fatal) {
		response += R"(\fatal\)";
	}

	response += std::format(R"(\id\{}\final\)", requestId);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> LoginClient::SendPlayerData(std::uint32_t requestId)
{
	auto response = std::format(R"(\pi\\profileid\{}\nick\{}\userid\{}\email\{}\sig\{}\uniquenick\{}\firstname\\lastname\\countrycode\{}\birthday\1401530400\lon\0.000000\lat\0.000000\loc\\id\{}\final\)",
		m_PlayerData->GetProfileID(), m_PlayerData->name, m_PlayerData->GetUserID(), m_PlayerData->email, m_LoginTicket, m_PlayerData->name, m_PlayerData->country, requestId);
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
}