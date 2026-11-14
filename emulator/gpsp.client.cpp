#include "gpsp.client.h"
#include "playerdb.h"
#include "utils.h"
#include <format>
#include <print>
#include <cctype>
using namespace gamespy;

namespace {
	// cap the read buffer so a client that never sends the final terminator
	// cannot grow it without bound (memory-exhaustion DoS)
	constexpr std::size_t max_packet_buffer_size = 2048;
}

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
		passwordMD5 = utils::md5(password);
	}

	if (!email || passwordMD5.empty()) {
		co_await SendError(0, "Invalid Query!", true);
		co_return;
	}

	// note: we should not send and error but those results: GameSpy/GP/gp.h:253
	const auto emailNormalized = *email
		| std::views::transform([](unsigned char c) { return static_cast<char>(std::tolower(c)); })
		| std::ranges::to<std::string>();
	const auto& players = co_await m_DB.GetPlayerByMailAndPassword(emailNormalized, passwordMD5);
	if (players.empty())
		co_await SendError(551, "Unable to get any associated profiles.", true);
	else {
		auto response = std::string{ "\\nr\\0" };

		for (const auto& player : players)
			response += "\\nick\\" + player.name + "\\uniquenick\\" + player.name;

		response += R"(\ndone\\final\)";
		co_await boost::asio::async_write(m_Socket, boost::asio::buffer(response), boost::asio::use_awaitable);
	}	
}

boost::asio::awaitable<void> SearchClient::HandleCheck(const std::span<const char>& packet)
{
	// GP/gpiSearch.c GPI_SEARCH_CHECK validates all three account credentials.
	auto nick = utils::value_for_key(packet, "\\nick\\");
	auto email = utils::value_for_key(packet, "\\email\\");
	auto passEnc = utils::value_for_key(packet, "\\passenc\\");
	std::uint32_t result = 0;
	std::uint64_t profile = 0;
	if (!nick || nick->empty()) result = 0x0E02; // GP_CHECK_BAD_NICK
	else if (!email || email->empty()) result = 0x0E01; // GP_CHECK_BAD_EMAIL
	else if (!passEnc) result = 0x0E03; // GP_CHECK_BAD_PASSWORD
	else {
		auto player = co_await m_DB.GetPlayerByName(*nick);
		if (!player) result = 0x0E02;
		else if (!std::ranges::equal(*email, player->email,
			[](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); }))
			result = 0x0E01;
		else if (utils::md5(utils::passdecode(std::string(*passEnc))) != player->password)
			result = 0x0E03;
		else profile = player->GetProfileID();
	}

	auto response = std::format(R"(\cur\{}\pid\{}\final\)", result, profile);
	co_await boost::asio::async_write(m_Socket, boost::asio::buffer(response), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> SearchClient::Process()
{
	auto buffer = std::string{};
	while (m_Socket.is_open()) {
		auto [error, length] = co_await boost::asio::async_read_until(m_Socket, boost::asio::dynamic_buffer(buffer, max_packet_buffer_size), "\\final\\", boost::asio::as_tuple(boost::asio::use_awaitable));
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
			co_await HandleCheck(packet);
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
	co_await boost::asio::async_write(m_Socket, boost::asio::buffer(response), boost::asio::use_awaitable);
	if (fatal) m_Socket.close();
}
