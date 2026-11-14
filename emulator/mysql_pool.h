#pragma once
#include "asio.h"
#include "task.h"
#include <boost/mysql.hpp>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <stdexcept>

namespace gamespy
{
	inline boost::mysql::pool_params mysql_pool_params(const boost::mysql::connect_params& params)
	{
		boost::mysql::pool_params pool;
		pool.server_address = params.server_address;
		pool.username = params.username;
		pool.password = params.password;
		pool.database = params.database;
		pool.ssl = params.ssl;
		pool.multi_queries = params.multi_queries;
		pool.max_size = 8;
		return pool;
	}

	inline task<boost::mysql::pooled_connection> mysql_connection(boost::mysql::connection_pool& pool)
	{
		// A disconnected database must not keep a client waiting indefinitely.
		co_return co_await pool.async_get_connection(
			boost::asio::cancel_after(std::chrono::seconds(20), boost::asio::use_awaitable));
	}

	inline std::uint64_t mysql_unsigned(boost::mysql::field_view value)
	{
		if (value.is_uint64())
			return value.as_uint64();
		const auto number = value.as_int64();
		if (number < 0)
			throw std::runtime_error{ "negative identifier in MySQL result" };
		return static_cast<std::uint64_t>(number);
	}

	inline std::string_view mysql_text(boost::mysql::field_view value)
	{
		if (value.is_null())
			return {};
		const auto text = value.as_string();
		return { text.data(), text.size() };
	}
}
