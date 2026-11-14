#include "asio.h"
#include "gamedb.h"
#include "game.h"
#include "gpcm.client.h"
#include "gpsp.client.h"
#include "ms.client.h"
#include "playerdb.sqlite.h"
#include "stats.client.h"
#include "sessions.h"
#include "utils.h"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <array>
#include <bit>
#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>

namespace net = boost::asio;
using tcp = net::ip::tcp;
using namespace gamespy;
using namespace boost::asio::experimental::awaitable_operators;

namespace {
void require(bool value, const char* message)
{
	if (!value) throw std::runtime_error(message);
}

auto socket_pair(net::io_context& context)
{
	tcp::acceptor acceptor(context, { net::ip::address_v4::loopback(), 0 });
	tcp::socket client(context);
	client.connect(acceptor.local_endpoint());
	auto server = acceptor.accept();
	server.set_option(net::socket_base::send_buffer_size(1024));
	return std::pair{ std::move(server), std::move(client) };
}

template<class Client, class... Args>
task<void> run_session(tcp::socket socket, Args&... args)
{
	// Match production ownership: destroy the socket as soon as Process returns.
	Client client(std::move(socket), args...);
	co_await client.Process();
}

task<std::string> read_text(tcp::socket& socket, std::string& pending)
{
	auto size = co_await net::async_read_until(socket, net::dynamic_buffer(pending, 1024 * 1024), "\\final\\", net::use_awaitable);
	auto result = pending.substr(0, size);
	pending.erase(0, size);
	co_return result;
}

task<std::vector<std::uint8_t>> read_bytes(tcp::socket& socket, std::size_t size)
{
	std::vector<std::uint8_t> bytes(size);
	co_await net::async_read(socket, net::buffer(bytes), net::use_awaitable);
	co_return bytes;
}

task<void> expect_closed(tcp::socket& socket)
{
	std::array<char, 1> byte;
	auto [error, size] = co_await socket.async_read_some(net::buffer(byte), net::as_tuple(net::use_awaitable));
	require(error == net::error::eof || error == net::error::connection_reset, "malformed input must close the session");
}

task<void> search_requests(tcp::socket socket, std::string expected)
{
	std::string pending;
	auto request = std::format("\\nicks\\\\email\\TEST@EXAMPLE.COM\\passenc\\{}\\final\\", "YpeHhgarjd8KeBGYVw__");
	// Split a request across writes, then verify a response larger than the send buffer.
	co_await net::async_write(socket, net::buffer(request.data(), 3), net::use_awaitable);
	co_await net::async_write(socket, net::buffer(request.data() + 3, request.size() - 3), net::use_awaitable);
	require(co_await read_text(socket, pending) == expected, "encoded password lookup or complete TCP response failed");

	// Captured field layout and error codes follow GP/gpiSearch.c and gp.h.
	const std::string correct = R"(\check\\nick\player0000\email\TEST@EXAMPLE.COM\passenc\YpeHhgarjd8KeBGYVw__\final\)";
	const std::string wrong = R"(\check\\nick\player0000\email\test@example.com\passenc\ZpOHgVy0nsg_\final\)";
	const auto coalesced = correct + wrong;
	co_await net::async_write(socket, net::buffer(coalesced), net::use_awaitable);
	require((co_await read_text(socket, pending)).starts_with("\\cur\\0\\pid\\"), "coalesced request lost its first response");
	require(co_await read_text(socket, pending) == "\\cur\\3587\\pid\\0\\final\\", "check accepted an incorrect password or lost its second response");

	const std::string oversized(4096, 'x');
	co_await net::async_write(socket, net::buffer(oversized), net::as_tuple(net::use_awaitable));
	co_await expect_closed(socket);
}

task<void> test_search(net::io_context& context)
{
	PlayerDBSQLite db(":memory:");
	std::string expected = "\\nr\\0";
	for (int index = 0; index < 2000; ++index) {
		auto name = std::format("player{:04}", index);
		PlayerData player(name, "test@example.com", utils::md5("test-password"), "xx");
		co_await db.CreatePlayer(player);
		expected += "\\nick\\" + name + "\\uniquenick\\" + name;
	}
	expected += "\\ndone\\\\final\\";
	auto [serverSocket, clientSocket] = socket_pair(context);
	co_await (run_session<SearchClient>(std::move(serverSocket), db) && search_requests(std::move(clientSocket), std::move(expected)));
}

task<void> login_requests(tcp::socket socket, bool legacyUser)
{
	std::string pending;
	auto hello = co_await read_text(socket, pending);
	auto challenge = utils::value_for_key(hello, "\\challenge\\");
	require(challenge.has_value(), "login challenge missing");
	// GP/gpiConnect.c includes partnerID@ and nick@email in both challenge hashes.
	auto identity = legacyUser ? "42@alice@ALICE@EXAMPLE.COM" : "alice";
	auto proof = utils::generate_challenge(identity, utils::md5("password"), "client-challenge", *challenge);
	auto userFields = legacyUser ? "\\user\\alice@ALICE@EXAMPLE.COM\\partnerid\\42" : "\\uniquenick\\alice";
	auto request = std::format("\\login\\{}\\challenge\\client-challenge\\response\\{}\\final\\\\ka\\\\final\\\\getprofile\\\\id\\7\\final\\", userFields, proof);
	co_await net::async_write(socket, net::buffer(request), net::use_awaitable);
	auto loggedIn = co_await read_text(socket, pending);
	require(loggedIn.starts_with("\\lc\\2\\"), "login authentication failed");
	auto serverProof = utils::value_for_key(loggedIn, "\\proof\\");
	require(serverProof && *serverProof == utils::generate_challenge(identity, utils::md5("password"), *challenge, "client-challenge"), "server login proof mismatch");
	auto profile = co_await read_text(socket, pending);
	require(profile.contains("\\nick\\alice\\") && profile.contains("\\id\\7\\"), "profile response mismatch");
	const std::string logout = "\\logout\\\\final\\";
	co_await net::async_write(socket, net::buffer(logout), net::use_awaitable);
	co_await expect_closed(socket);
}

task<void> test_login(net::io_context& context)
{
	PlayerDBSQLite db(":memory:");
	PlayerData player("alice", "alice@example.com", utils::md5("password"), "xx");
	co_await db.CreatePlayer(player);
	for (bool legacyUser : { false, true }) {
		auto [serverSocket, clientSocket] = socket_pair(context);
		co_await (run_session<LoginClient>(std::move(serverSocket), db) && login_requests(std::move(clientSocket), legacyUser));
	}
}

class TestGames : public GameDB {
public:
	std::shared_ptr<Game> game = std::make_shared<Game>(GameData{
		.name = "sessiontest", .secretKey = "secret", .keys = GameData::common_keys()
	});
	std::shared_ptr<Game> source = std::make_shared<Game>(GameData{
		.name = "clienttest", .secretKey = "source-secret", .backend = GameData::BackendOptions::none
	});
	task<void> Connect() override { co_await game->Connect(); }
	task<void> Disconnect() override { co_await game->Disconnect(); }
	task<bool> HasGame(const std::string_view& name) override { co_return name == game->name() || name == source->name(); }
	task<std::shared_ptr<Game>> GetGame(const std::string_view& name) override {
		if (name == source->name()) co_return source;
		if (name != game->name()) throw std::out_of_range("unknown game");
		co_return game;
	}
	task<std::vector<std::shared_ptr<Game>>> GetGames() override { co_return std::vector{ game }; }
};

task<std::vector<std::uint8_t>> read_encrypted(tcp::socket& socket, sapphire& cipher, std::size_t size)
{
	auto bytes = co_await read_bytes(socket, size);
	for (auto& byte : bytes) byte = cipher.decrypt(byte);
	co_return bytes;
}

task<void> browser_updates(tcp::socket socket, std::shared_ptr<Game> game, std::shared_ptr<Game> source, std::uint8_t options)
{
	std::vector<std::uint8_t> request{ 0, 0, 0, 1, 3, 1, 0, 0, 0 };
	for (auto selected : { game, source }) {
		request.append_range(selected->name());
		request.push_back(0);
	}
	std::array<std::uint8_t, 8> key{ '1', '2', '3', '4', '5', '6', '7', '8' };
	request.append_range(key);
	request.push_back(0); // no server filter
	if (options != 2) request.append_range(std::string_view("\\hostname\\mapname\\gamever\\gamemode"));
	request.push_back(0);
	request.append_range(std::array<std::uint8_t, 4>{ 0, 0, 0, options }); // SDK options use network byte order
	request[0] = static_cast<std::uint8_t>(request.size() >> 8);
	request[1] = static_cast<std::uint8_t>(request.size());
	co_await net::async_write(socket, net::buffer(request), net::use_awaitable);

	auto keyLength = (co_await read_bytes(socket, 1)).front() ^ 0xEC;
	auto cryptoHeader = co_await read_bytes(socket, keyLength);
	auto backend = std::to_underlying(game->backend());
	require(cryptoHeader.size() >= 2 && cryptoHeader[0] == (backend >> 8) && cryptoHeader[1] == (backend & 255), "crypto header must describe the target game");
	auto challengeLength = (co_await read_bytes(socket, 1)).front() ^ 0xEA;
	auto challenge = co_await read_bytes(socket, challengeLength);
	auto secret = source->secretKey();
	for (std::size_t i = 0; i < challenge.size(); ++i)
		key[(i * secret[i % secret.size()]) % key.size()] ^= key[i % key.size()] ^ challenge[i];
	sapphire cipher(key);
	auto parsed = ServerListRequest::Parse(std::span(request).subspan(3));
	require(parsed.has_value(), "test browser request malformed");
	auto expectedHeader = parsed->GetResponseHeaderBytes(*game, net::ip::address_v4::loopback());
	if (options != 2) expectedHeader.append_range(std::array<std::uint8_t, 5>{ 0, 255, 255, 255, 255 });
	require(co_await read_encrypted(socket, cipher, expectedHeader.size()) == expectedHeader, "browser list header mismatch");
	if (options != 4) { socket.close(); co_return; }
	while (game->OnServerAdded.num_slots() == 0) co_await net::post(net::use_awaitable);

	Game::IncomingServer server{
		.public_ip = "127.0.0.1", .public_port = 6500,
		.data = { { "hostname", "alpha" }, { "mapname", "valley" }, { "gamever", "1.50" }, { "gamemode", "gpm_cq" } }
	};
	std::vector<std::uint8_t> expected{ 65, 127, 0, 0, 1 };
	for (std::string_view value : { "alpha", "valley", "1.50", "gpm_cq" }) {
		expected.append_range(value);
		expected.push_back(0);
	}
	const std::array<std::uint8_t, 3> keepalive{ 0, 3, 3 };
	co_await net::async_write(socket, net::buffer(keepalive.data(), 1), net::use_awaitable);
	for (int update = 0; update < 2; ++update) {
		game->OnServerAdded(server);
		auto header = co_await read_encrypted(socket, cipher, 3);
		require(header[2] == 2 && ((header[0] << 8) | header[1]) == expected.size() + 3, "push header mismatch");
		require(co_await read_encrypted(socket, cipher, expected.size()) == expected, "pushed fields lost their storage");
		if (update == 0)
			co_await net::async_write(socket, net::buffer(keepalive.data() + 1, 2), net::use_awaitable);
	}
	game->OnServerRemoved("127.0.0.1", 6500);
	const std::vector<std::uint8_t> removed{ 0, 9, 4, 127, 0, 0, 1, 0x19, 0x64 };
	require(co_await read_encrypted(socket, cipher, removed.size()) == removed, "removal after successive pushes failed");
	socket.close();
}

task<void> malformed_browser_request(tcp::socket socket, std::array<std::uint8_t, 3> packet)
{
	co_await net::async_write(socket, net::buffer(packet), net::use_awaitable);
	co_await expect_closed(socket);
}

task<void> test_browser(net::io_context& context)
{
	TestGames games;
	co_await games.Connect();
	for (std::uint8_t options : { 4, 2, 32 }) {
		auto [serverSocket, clientSocket] = socket_pair(context);
		co_await (run_session<BrowserClient>(std::move(serverSocket), games) && browser_updates(std::move(clientSocket), games.game, games.source, options));
	}
	require(games.game->OnServerAdded.num_slots() == 0 && games.game->OnServerRemoved.num_slots() == 0, "closed session retained signal subscriptions");
	for (auto packet : { std::array<std::uint8_t, 3>{ 0, 2, 0 }, { 0x10, 1, 0 }, { 0, 3, 255 } }) {
		auto [serverSocket, clientSocket] = socket_pair(context);
		co_await (run_session<BrowserClient>(std::move(serverSocket), games) && malformed_browser_request(std::move(clientSocket), packet));
	}
}

task<std::string> read_stats(tcp::socket& socket, std::string& pending)
{
	auto packet = co_await read_text(socket, pending);
	auto encoded = std::span(packet.data(), packet.size() - 7);
	utils::gs_xor(encoded, utils::xor_types::GameSpy3D);
	co_return packet;
}

task<void> stats_authentication(tcp::socket socket)
{
	std::string pending;
	auto hello = co_await read_stats(socket, pending);
	auto challenge = utils::value_for_key(hello, "\\challenge\\");
	require(challenge.has_value(), "stats challenge missing");
	std::uint32_t hash = 0;
	for (unsigned char c : *challenge) hash = hash * 0x9CCF9319U + c;
	auto response = utils::md5(std::format("{}secret", std::bit_cast<std::int32_t>(hash)));
	auto auth = std::format("\\auth\\\\gamename\\sessiontest\\response\\{}\\id\\1", response);
	utils::gs_xor(auth, utils::xor_types::GameSpy3D);
	auth += "\\final\\";
	co_await net::async_write(socket, net::buffer(auth), net::use_awaitable);
	require((co_await read_stats(socket, pending)).starts_with("\\lc\\2\\sesskey\\"), "stats wrapping challenge hash mismatch");
	std::string requests;
	for (int local = 1; local <= 2; ++local) {
		auto packet = std::format(R"(\authp\\pid\999\resp\invalid\lid\{})", local);
		utils::gs_xor(packet, utils::xor_types::GameSpy3D);
		requests += packet + "\\final\\";
	}
	co_await net::async_write(socket, net::buffer(requests), net::use_awaitable);
	for (int local = 1; local <= 2; ++local)
		require(co_await read_stats(socket, pending) == std::format(R"(\pauthr\-1\lid\{}\final\)", local), "coalesced stats request corrupted its next packet");
	const std::string oversized(512 * 1024 + 1, 'x');
	co_await net::async_write(socket, net::buffer(oversized), net::as_tuple(net::use_awaitable));
	co_await expect_closed(socket);
}

task<void> test_stats(net::io_context& context)
{
	TestGames games;
	PlayerDBSQLite players(":memory:");
	auto [serverSocket, clientSocket] = socket_pair(context);
	co_await (run_session<StatsClient>(std::move(serverSocket), games, players, std::nullopt) && stats_authentication(std::move(clientSocket)));
}

task<void> waiting_session(bool& started, bool& finished)
{
	started = true;
	struct Completion { bool& flag; ~Completion() { flag = true; } } completion{ finished };
	net::steady_timer timer(co_await net::this_coro::executor, net::steady_timer::time_point::max());
	co_await timer.async_wait(net::use_awaitable);
}

task<void> test_session_shutdown(net::io_context& context)
{
	Sessions sessions(context.get_executor(), 1);
	bool started = false, finished = false;
	sessions.Spawn(waiting_session(started, finished));
	while (!started) co_await net::post(net::use_awaitable);
	bool excessStarted = false, excessFinished = false;
	sessions.Spawn(waiting_session(excessStarted, excessFinished));
	co_await net::post(net::use_awaitable);
	sessions.Stop();
	sessions.Stop();
	co_await sessions.Wait();
	require(finished, "shutdown returned before the session was destroyed");
	require(!excessStarted, "active-session limit was exceeded");
	started = false;
	sessions.Spawn(waiting_session(started, finished));
	co_await net::post(net::use_awaitable);
	require(!started, "session accepted after shutdown");

	Sessions immediate(context.get_executor());
	bool immediateStarted = false, immediateFinished = false;
	immediate.Spawn(waiting_session(immediateStarted, immediateFinished));
	immediate.Stop();
	co_await immediate.Wait();
	require(!immediateStarted || immediateFinished, "immediate shutdown leaked a starting session");
}

void test_gp_challenges()
{
	// Frozen vectors from GP/gpiConnect.c gpiSendLogin/gpiProcessConnect:
	// MD5(password hash + 48 spaces + user + first challenge + second + hash).
	const std::string_view passwordHash = "5f4dcc3b5aa765d61d8327deb882cf99";
	require(utils::generate_challenge("alice", passwordHash, "client", "server") == "95e81e0f8334ed94e5c7378dfac43caa", "GP login challenge vector mismatch");
	require(utils::generate_challenge("alice", passwordHash, "server", "client") == "afa92423fe97d218c58875c366e9f6e5", "GP server proof vector mismatch");
	require(utils::generate_challenge("42@alice@alice@example.com", passwordHash, "client", "server") == "4beb59e467dbf2e606bae86a8a9e64e4", "GP partner login vector mismatch");
	require(utils::generate_challenge("42@alice@alice@example.com", passwordHash, "server", "client") == "6384986d7377e551cd4df6c26ceedf30", "GP partner server proof vector mismatch");
}

task<void> run_tests(net::io_context& context)
{
	test_gp_challenges();
	std::clog << "Testing search sessions\n";
	co_await test_search(context);
	std::clog << "Testing login sessions\n";
	co_await test_login(context);
	std::clog << "Testing browser sessions\n";
	co_await test_browser(context);
	std::clog << "Testing stats sessions\n";
	co_await test_stats(context);
	std::clog << "Testing session_shutdown sessions\n";
	co_await test_session_shutdown(context);
}
}

int main()
{
	try {
		net::io_context context;
		net::steady_timer deadline(context, std::chrono::seconds(15));
		bool timedOut = false;
		deadline.async_wait([&](auto error) {
			if (!error) { timedOut = true; context.stop(); }
		});
		std::exception_ptr failure;
		net::co_spawn(context, run_tests(context), [&](std::exception_ptr error) {
			failure = error;
			deadline.cancel();
		});
		context.run();
		require(!timedOut, "session tests timed out");
		if (failure) std::rethrow_exception(failure);
		std::cout << "Session tests passed\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
