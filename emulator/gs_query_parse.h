#pragma once
#include <cstdint>
#include <string_view>
#include <deque>
#include <expected>

namespace gamespy {
	struct Token {
		enum class Type {
			Unknown,
			Value,
			Operator_EQ,
			Operator_NEQ,
			Operator_GE,
			Operator_G,
			Operator_LE,
			Operator_L,
			Operator_AND,
			Operator_OR,
			Operator_LIKE,
			LeftP,
			RightP
		} type;

		std::string_view str;
		std::int8_t precedence;
		bool rightAssociative;
		bool unary;
	};

	enum class ParseError {
		INVALID
	};

	std::expected<std::deque<Token>, ParseError> parse(const std::string_view& str);
}