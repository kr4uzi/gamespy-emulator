#include "textpacket.h"
#include <ranges>
#include <vector>
using namespace gamespy;

std::string TextPacket::str() const {
	auto out = std::string{};
	if (!type.empty()) {
		out += "\\";
		out += type;
		out += "\\";
	}

	for (const auto& keyValue : values) {
		out += "\\";
		out += keyValue.first;
		out += "\\";
		out += keyValue.second;
	}

	out += TextPacket::PACKET_END;
	return out;
}

std::expected<TextPacket, TextPacket::ParseError> TextPacket::parse(const std::span<const char>& buffer)
{
	if (buffer.front() != '\\' || buffer.back() != '\\')
		return std::unexpected(ParseError::INVALID);

	const auto values = buffer
		| std::views::drop(1) // ignore first backslash
		| std::views::take(buffer.size() - 1 - (buffer.back() == '\0')) // ignore null-terminator (if present)
		| std::views::split('\\')
		| std::views::chunk(2)
		| std::views::transform(
			[](const auto& chunk) {
				auto iter = chunk.begin();
				auto key = std::string_view{ (*iter).begin(), (*iter).end() };
				if (++iter != chunk.end())
					return std::make_pair(key, std::string_view{ (*iter).begin(), (*iter).end() });

				return std::make_pair(key, std::string_view{});
			})
		| std::ranges::to<std::vector>()
	;

	if (values.size() < 1)
		return std::unexpected(ParseError::INCOMPLETE);

	return TextPacket{
		.type = values.front().first,
		.values = values
			| std::views::drop(1)
			| std::ranges::to<std::map>()
	};
}