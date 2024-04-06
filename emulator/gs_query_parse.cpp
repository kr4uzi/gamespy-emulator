#include "gs_query_parse.h"
#include <cctype>
#include <algorithm>
#include <ranges>
#include <array>
using gamespy::Token;

// bf2_anticheat = 1 and (bf2_mapsize = 16 or bf2_mapsize = 32 or bf2_mapsize = 64) and gamever = '1.5.3153-802.0' and gamevariant = 'bf2'
// bf2_anticheat = 1 and (bf2_mapsize = 16 or bf2_mapsize = 32 or bf2_mapsize = 64) and gamever = '1.5.3153-802.0' and gamevariant = 'bf2' and hostname like '%test%' """\ 23%' and gametype like '%gpm_cq%'
std::expected<std::deque<Token>, gamespy::ParseError> gamespy::parse(const std::string_view& str)
{
	std::deque<Token> tokens;
	bool hasVariable = false;

	for (auto i = str.begin(), end = str.end(); i != end; ++i) {
		if (std::isblank(*i))
			continue;

		if (std::isalnum(*i)) {
			auto sEnd = std::find_if_not(i + 1, end, std::isalnum);
			std::string_view s(i, sEnd);
			i = sEnd;
			continue;
		}
		else if (*i == '\'') {
			auto sEnd = std::find(i + 1, end, '\'');
			while (sEnd != end) {

			}
		}

		auto t = Token::Type::Unknown;
		std::int8_t precedence = -1;
		bool rightAssociative = false;
		bool unary = false;
		/*
		using namespace std::string_view_literals;
		static constexpr std::array ops = {}

		auto strOff = std::ranges::subrange(i, end);
		if (std::ranges::starts_with(strOff, "("))
			t = Token::Type::LeftP;
		else if (std::ranges::starts_with(strOff, ")"))
			t = Token::Type::RightP;
		else if (std::ranges::starts_with(strOff, "="))
			t = Token::Type::Operator_EQ;*/
	}

	return std::unexpected(gamespy::ParseError::INVALID);
}