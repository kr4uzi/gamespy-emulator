#include "gpsp.client.h"
#include "playerdb.h"
#include "utils.h"
#include <format>
#include <print>
using namespace gamespy;

SearchClient::SearchClient(boost::asio::ip::tcp::socket socket, PlayerDB& db)
	: m_Socket(std::move(socket)), m_DB(db)
{

}

SearchClient::~SearchClient()
{

}

boost::asio::awaitable<void> SearchClient::HandleSearchNicks(const std::span<const char>& packet)
{
	auto email = utils::value_for_key(packet, "\\email\\");
	auto pass = utils::value_for_key(packet, "\\pass\\");
	auto passEnc = utils::value_for_key(packet, "\\passenc\\");
	std::string passwordMD5;
	if (pass) {
		passwordMD5 = utils::md5(*pass);
	}
	else if (passEnc && !passEnc->empty()) {
		std::string password = utils::passdecode(std::string{ *passEnc });
		passwordMD5 = utils::md5(*passEnc);
	}

	if (!email || passwordMD5.empty()) {
		co_await SendError(0, "Invalid Query!", true);
		co_return;
	}

	// note: we should not send and error but those results: GameSpy/GP/gp.h:253
	const auto emailNormalized = *email | std::views::transform((int(*)(int))std::tolower) | std::ranges::to<std::string>();
	const auto& players = co_await m_DB.GetPlayerByMailAndPassword(emailNormalized, passwordMD5);
	if (players.empty())
		co_await SendError(551, "Unable to get any associated profiles.", true);
	else {
		auto response = std::string{ "\\nr\\0" };

		for (const auto& player : players)
			response += "\\nick\\" + player.name + "\\uniquenick\\" + player.name;

		response += R"(\ndone\\final\)";
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
	}	
}

boost::asio::awaitable<void> SearchClient::HandleProfileExists(const std::span<const char>& packet)
{
	auto nick = utils::value_for_key(packet, "\\nick\\");
	auto uniqueNick = utils::value_for_key(packet, "\\uniquenick\\");
	std::string_view name;
	if (nick)
		name = *nick;
	else if (uniqueNick)
		name = *uniqueNick;

	if (name.empty()) {
		co_await SendError(0, "Invalid Query!", true);
		co_return;
	}
	
	if (co_await m_DB.HasPlayer(name)) {
		auto playerData = co_await m_DB.GetPlayerByName(name);
		auto response = std::format(R"(\cur\0\pid\{}\final\)", playerData->GetProfileID());
		co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
	}
	else {
		auto response = std::format("Username [{}] doesn't exist!", name);
		co_await SendError(265, "Invalid Query!", true);
	}
}

boost::asio::awaitable<void> SearchClient::Process()
{
	auto buffer = std::string{};
	while (m_Socket.is_open()) {
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, boost::asio::dynamic_buffer(buffer), "\\final\\", boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error) break;

		auto packet = std::span{ buffer.data(), length };
		// all requests (gpiSearch.c):
		// search (sesskey, profileid, namespaceid, partnerid, nick?, uniquenick?, email?, firstname?, lastname?, icquin?, skip?, gamename)
		// searchunique (sesskey, profileid, uniquenick, namespaces[,separated], gamename)
		// valid (email, partnerid, gamename)
		// nicks (email, passenc, namespaceid, partnerid, gamename)
		// pmatch (sesskey, profileid, productid, gamename)
		// check (nick, email, partnerid, passenc, gamename) 
		// newuser (nick, email, passenc, productID, namespaceid, uniquenick, cdkey?, partnerid, gamename)
		// others (sesskey, profileid, namespaceid, gamename)
		// otherslist (sesskey, profileid, numopids, opids[|separated], gamename)
		// uniquesearch (preferrednick, namespaceid, gamename)

		// requests used by bf2:
		if (buffer.starts_with("\\nicks\\"))
			co_await HandleSearchNicks(packet);
		else if (buffer.starts_with("\\check\\"))
			co_await HandleProfileExists(packet);
		else {
			std::println("[search] unhandled packet: {}", buffer);
			co_await SendError(0, "Invalid Query!");
		}

		buffer.erase(0, length);
	}
}

boost::asio::awaitable<void> SearchClient::SendError(std::uint32_t errorCode, const std::string_view& errorMessage, bool fatal)
{
	auto response = std::format(R"(\error\\err\{}\errmsg\{})", errorCode, errorMessage);
	if (fatal) {
		response += "\\fatal\\";
	}

	response += "\\final\\";
	co_await m_Socket.async_send(boost::asio::buffer(response), boost::asio::use_awaitable);
}