#include "utils.h"
#include "md5.h"
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>
#include <random>
#include <sstream>
#include <ranges>
#include <charconv>
using namespace gamespy;

namespace {
	// the random_string and this utility function are mostly
	// taken from https://stackoverflow.com/a/444614
	// Konrad Rudolp - Nov 11, 2020 13:03
	template <typename T = std::mt19937>
	auto random_generator() -> T {
		auto constexpr seed_bytes = sizeof(typename T::result_type) * T::state_size;
		auto constexpr seed_len = seed_bytes / sizeof(std::seed_seq::result_type);
		auto seed = std::array<std::seed_seq::result_type, seed_len>();
		auto dev = std::random_device();
		std::generate_n(std::begin(seed), seed_len, std::ref(dev));
		auto seed_seq = std::seed_seq(std::begin(seed), std::end(seed));
		return T{ seed_seq };
	}

	//http://stackoverflow.com/a/28471421
	std::string base64_decode(const std::string& data) {
		using namespace boost::archive::iterators;
		using It = transform_width<binary_from_base64<std::string::const_iterator>, 8, 6>;
		return boost::algorithm::trim_right_copy_if(std::string(It(std::begin(data)), It(std::end(data))), [](char c) {
			return c == '\0';
		});
	}

	std::string base64_encode(const std::string& data) {
		using namespace boost::archive::iterators;
		using It = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;
		auto tmp = std::string(It(std::begin(data)), It(std::end(data)));
		return tmp.append((3 - data.size() % 3) % 3, '=');
	}

	std::string gspassenc(const std::string& password) {
		auto rnd = std::minstd_rand0{ 0x79707367 }; // "gspy"

		auto encoded = std::string{ password };
		for (auto& c : encoded)
			c ^= (rnd() % 0xFF);

		return encoded;
	}
}

std::string utils::random_string(const std::string& table, std::string::size_type len)
{
	thread_local auto rng = random_generator<>();
	auto dist = std::uniform_int_distribution{ {}, table.length() - 1 };
	auto result = std::string(len, '\0');
	std::generate_n(begin(result), len, [&]() { return table[dist(rng)]; });
	return result;
}

std::string utils::encode(const std::string_view& key, std::string message)
{
	// like RC4 algorithm, but:
	// - inplace
	// - PRGA modified (see comment below)
	std::array<std::uint8_t, 256> sbox;
	for (decltype(sbox)::size_type i = 0; i < sbox.size(); i++)
		sbox[i] = static_cast<std::uint8_t>(i);

	auto keyLength = key.length();
	for (std::uint16_t i = 0, j = 0; i < sbox.size(); i++) {
		j = (j + sbox[i] + key[i % keyLength]) % sbox.size();
		std::swap(sbox[i], sbox[j]);
	}

	std::uint16_t i = 0, j = 0;
	for (auto& c : message) {
		i = (i + c + 1) % sbox.size(); // deviation to RC4
		j = (j + sbox[i]) % sbox.size();
		std::swap(sbox[i], sbox[j]);
		c ^= sbox[(sbox[i] + sbox[j]) % sbox.size()];
	}

	return base64_encode(message);
}

std::string utils::passencode(const std::string& password)
{
	auto base64 = base64_encode(gspassenc(password));
	std::replace(base64.begin(), base64.end(), '=', '_');
	std::replace(base64.begin(), base64.end(), '+', '[');
	std::replace(base64.begin(), base64.end(), '/', ']');
	return base64;
}

std::string utils::passdecode(std::string password)
{
	std::replace(password.begin(), password.end(), '_', '=');
	std::replace(password.begin(), password.end(), '[', '+');
	std::replace(password.begin(), password.end(), ']', '/');
	return gspassenc(base64_decode(password));
}

std::string utils::generate_challenge(const std::string_view& name, const std::string_view& md5Password, const std::string_view& localChallenge, const std::string_view& remoteChallenge)
{
	std::string challenge(md5Password);
	challenge += std::string(48, ' ');
	challenge += name;
	challenge += localChallenge;
	challenge += remoteChallenge;
	challenge += md5Password;

	return md5(challenge);
}

std::string utils::md5(const std::string_view& text)
{
	//const ::md5 hash(text.begin(), text.end());
	const ::md5 hash(std::begin(text), std::end(text));
	return hash.hex_digest<std::string>();
}

template<>
std::optional<std::string_view> utils::value_for_key(const std::span<const char>& _textPacket, const std::string_view& key)
{
	auto packet = std::string_view{ _textPacket.data(), _textPacket.size() };
	auto start = packet.find(key);
	if (start == std::string_view::npos)
		return std::nullopt;

	start += key.length();
	auto end = start;
	auto delim = key[0];
	while (end < _textPacket.size() && _textPacket[end] != delim && _textPacket[end] != '\0')
		end++;

	if (end == _textPacket.size())
		return std::nullopt;

	return std::string_view{ _textPacket.begin() + start, _textPacket.begin() + end };
}

template<>
std::optional<std::string> utils::value_for_key(const std::span<const char>& _textPacket, const std::string_view& key)
{
	auto value = value_for_key<std::string_view>(_textPacket, key);
	if (!value)
		return std::nullopt;

	return std::string{ value->begin(), value->end() };
}

template<>
std::optional<std::uint32_t> utils::value_for_key(const std::span<const char>& _textPacket, const std::string_view& key)
{
	auto value = value_for_key<std::string_view>(_textPacket, key);
	if (!value)
		return std::nullopt;

	std::uint32_t result;
	auto end = value->data() + value->size();
	auto [ptr, ec] = std::from_chars(value->data(), end, result);
	if (ec != std::errc{} || ptr != end)
		return std::nullopt;

	return result;
}

std::uint32_t utils::to_date(const Clock::time_point& timepoint)
{
	auto ymd = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(timepoint) };
	auto year = static_cast<int>(ymd.year());
	if (year < 0)
		year = 2014; // gamespy death: 2014-05-31

	return (static_cast<std::uint32_t>(ymd.day()) << 24)
		| (static_cast<std::uint32_t>(ymd.month()) << 16)
		| static_cast<std::uint16_t>(year);
}

Clock::time_point utils::from_date(std::uint32_t gsDate)
{
	auto ymd = std::chrono::year_month_day{
		std::chrono::year { int(gsDate &     0xFFFF)       },
		std::chrono::month{    (gsDate &   0xFF0000) >> 16 },
		std::chrono::day  {    (gsDate & 0xFF000000) >> 24 }
	};

	return std::chrono::sys_days{ ymd };
}