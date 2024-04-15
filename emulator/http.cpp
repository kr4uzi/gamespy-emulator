#include "http.h"
#include <print>
//#include <boost/beast/http.hpp>
using namespace gamespy;

HttpServer::HttpServer(boost::asio::io_context& context)
	: m_Acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 80))
{
	std::println("[http] starting up: {} TCP", 80);
}

HttpServer::~HttpServer()
{
	std::println("[http] shutting down");
}

boost::asio::awaitable<void> HttpServer::AcceptClients()
{
	while (m_Acceptor.is_open()) {
		auto [error, socket] = co_await m_Acceptor.async_accept(boost::asio::as_tuple(boost::asio::use_awaitable));
		if (error)
			continue;

		boost::asio::co_spawn(m_Acceptor.get_executor(), HandleIncoming(std::move(socket)), boost::asio::detached);
	}
}

boost::asio::awaitable<void> HttpServer::HandleIncoming(boost::asio::ip::tcp::socket socket)
{
	auto buffer = std::array<char, 1024>{};
	const auto &[error, length] = co_await socket.async_read_some(boost::asio::buffer(buffer), boost::asio::as_tuple(boost::asio::use_awaitable));
	std::println("#{}#", std::string_view(buffer.data(), length));
	co_return;
}