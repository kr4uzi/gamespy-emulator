#include "qr.h"
using namespace gamespy;

std::expected<QRPacket, QRPacket::ParseError> QRPacket::Parse(const std::span<const std::uint8_t>& buffer)
{
	if (buffer.size() <= 4)
		return std::unexpected(ParseError::TOO_SMALL);

	if (buffer[buffer.size() - 1] != '\0')
		return std::unexpected(ParseError::UNEXPECTED_END);

	if (buffer[0] > static_cast<std::uint8_t>(Type::PREQUERY_IP_VERIFY))
		return std::unexpected(ParseError::UNKNOWN_TYPE);

	std::vector<std::pair<std::string, std::string>> values;

	std::string key;
	auto pos = buffer.begin() + 5;
	const auto end = buffer.end();
	while (pos < end) {
		auto localEnd = std::find(pos, end, '\0');
		if (key.empty())
			key = std::string(pos, localEnd);
		else {
			values.emplace_back(key, std::string(pos, localEnd));
			key.clear();
		}

		pos = localEnd + 1;
	}

	if (!key.empty())
		values.emplace_back(key, "");

	return QRPacket{
		.type = static_cast<Type>(buffer[0]),
		.instance = { buffer[1], buffer[2], buffer[3], buffer[4] },
		.values = std::move(values)
	};
}