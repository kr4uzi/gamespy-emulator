#include "dns_details.h"
#include <stdexcept>
#include <optional>
#include <ranges>
using namespace dns;

std::vector<std::uint8_t> dns_question::to_bytes(std::map<std::string, std::size_t>& nameMap, std::size_t currentOffset) const
{
	if (name.empty())
		throw std::runtime_error("empty name is not allowed");

	auto bytes = std::vector<std::uint8_t>{};
	if (nameMap.contains(name)) {
		auto pos = nameMap[name];
		bytes.push_back(0b11000000 | ((pos >> 8) & 0xFF));
		bytes.push_back(pos & 0xFF);
	}
	else {
		nameMap.emplace(name, currentOffset);
		for (const auto& domain : name | std::views::split('.')) {
			if (domain.size() > 63) throw std::overflow_error("domain too long");
			bytes.push_back(domain.size() & 0xFF);
			bytes.append_range(domain);
		}

		bytes.push_back(0x00);
	}

	auto _type = std::to_underlying(type);
	bytes.push_back((_type >> 8) & 0xFF);
	bytes.push_back(_type & 0xFF);

	auto _klass = std::to_underlying(klass);
	bytes.push_back((_klass >> 8) & 0xFF);
	bytes.push_back(_klass & 0xFF);

	return bytes;
}

std::expected<dns_question, ParseError> dns_question::from_bytes(const std::span<std::uint8_t>& bytes, std::span<std::uint8_t>::const_iterator& i)
{
	//auto i = std::vector<std::uint8_t>::iterator{ begin };
	if (std::ranges::distance(i, bytes.end()) < 2)
		return std::unexpected(ParseError::INCOMPLETE);

	auto typeBegin = std::optional<std::span<std::uint8_t>::const_iterator>{};
	auto name = std::vector<std::uint8_t>{};
	for (std::size_t len = *i++; len;) {
		if ((len & 0b11000000) == 0b11000000) {
			// compression: pos stores the offset (relative to bytes::begin)
			// -> the i-iterator is set to the offset position
			// -> to "return" back to the parsing position, the current cursor needs to be saved (in typeBegin)
			auto pos = ((len & 0b00111111) << 8) | *i;
			typeBegin = ++i;

			i = std::ranges::next(bytes.begin(), pos, bytes.end());
			if (i == bytes.end())
				return std::unexpected(ParseError::INVALID);

			len = *i++;
		}

		auto domainEnd = std::ranges::next(i, len, bytes.end());
		if (domainEnd == bytes.end())
			return std::unexpected(ParseError::INCOMPLETE);

		name.insert(name.end(), i, domainEnd);
		i = domainEnd;
		len = *i++; // note: we're guaranteed to not dereference bytes.end() here!
		if (len != 0)
			name.push_back('.');
	}

	// in case of compression the i-iterator has not been traversed and needs to be restored 
	if (typeBegin)
		i = *typeBegin;

	if (std::ranges::distance(i, bytes.end()) < 4)
		return std::unexpected(ParseError::INCOMPLETE);

	if (name.empty())
		return std::unexpected(ParseError::INVALID);

	std::uint16_t qType = (*i++ << 8) | *i++;
	std::uint16_t qClass = (*i++ << 8) | *i++;

	return dns_question{
		.name = std::string{name.begin(), name.end()},
		.type = static_cast<QTYPE>(qType),
		.klass = static_cast<QCLASS>(qClass)
	};
}

std::vector<std::uint8_t> dns_resource::to_bytes(std::map<std::string, std::size_t>& nameMap, std::size_t currentOffset) const
{
	auto time_to_live = ttl.count();
	if (time_to_live > std::numeric_limits<std::uint16_t>::max())
		throw std::overflow_error("ttl does not fit in uint16_t");

	auto rdLength = data.size();
	if (data.size() > std::numeric_limits<std::uint16_t>::max())
		throw std::overflow_error("dns_answer data is too large");

	auto bytes = dns_question::to_bytes(nameMap, currentOffset);
	bytes.push_back((time_to_live >> 24) & 0xFF);
	bytes.push_back((time_to_live >> 16) & 0xFF);
	bytes.push_back((time_to_live >> 8) & 0xFF);
	bytes.push_back(time_to_live & 0xFF);
	bytes.push_back((rdLength >> 8) & 0xFF);
	bytes.push_back(rdLength & 0xFF);
	bytes.append_range(data);
	return bytes;
}

std::expected<dns_resource, ParseError> dns_resource::from_bytes(const std::span<std::uint8_t>& bytes, std::span<std::uint8_t>::const_iterator& i)
{
	auto question = dns_question::from_bytes(bytes, i);
	if (question) {
		if (std::ranges::distance(i, bytes.end()) < 4)
			return std::unexpected(ParseError::INCOMPLETE);

		auto _ttl = static_cast<std::uint16_t>((*i++ << 8) | *i++);
		auto size = static_cast<std::uint16_t>((*i++ << 8) | *i++);
		if (std::ranges::distance(i, bytes.end()) < size)
			return std::unexpected(ParseError::INCOMPLETE);

		auto dataEnd = std::ranges::next(i, size, bytes.end());
		auto data = std::vector<std::uint8_t>{ i, dataEnd };
		i = dataEnd;

		return dns_resource{
			{
				.name = question->name,
				.type = question->type,
				.klass = question->klass
			},
			std::chrono::seconds(_ttl),
			std::move(data)
		};
	}

	return std::unexpected(question.error());
}

std::vector<std::uint8_t> dns_packet::to_bytes() const
{
	auto bytes = std::vector<std::uint8_t>{};
	bytes.append_range(std::array{ (id >> 8) & 0xFF, id & 0xFF });
	bytes.push_back(
		static_cast<std::uint8_t>(response) << 7
		| std::to_underlying(query_type) << 3
		| static_cast<std::uint8_t>(authoritative_answer) << 2
		| static_cast<std::uint8_t>(truncated) << 1
		| static_cast<std::uint8_t>(recursion_desired)
	);
	bytes.push_back(
		recursion_available << 7
		| authentic_data << 5
		| checking_disabled << 4
		| std::to_underlying(response_type)
	);
	bytes.push_back((questions.size() >> 8) & 0xFF);
	bytes.push_back(questions.size() & 0xFF);
	bytes.push_back((answers.size() >> 8) & 0xFF);
	bytes.push_back(answers.size() & 0xFF);
	bytes.append_range(std::array{ 0x00, 0x00 }); // name server count
	bytes.append_range(std::array{ 0x00, 0x00 }); // additional resource count

	std::map<std::string, std::size_t> nameMap;
	for (const auto& q : questions)
		bytes.append_range(q.to_bytes(nameMap, bytes.size()));
	for (const auto& a : answers)
		bytes.append_range(a.to_bytes(nameMap, bytes.size()));

	return bytes;
}

std::expected<dns_packet, ParseError> dns_packet::from_bytes(const std::span<std::uint8_t>& bytes)
{
	if (bytes.size() < 12)
		return std::unexpected(ParseError::INCOMPLETE);

	std::uint16_t qdCount = (bytes[4] << 8) | bytes[5];
	std::uint16_t anCount = (bytes[6] << 8) | bytes[7];
	std::uint16_t nsCount = (bytes[8] << 8) | bytes[9];
	std::uint16_t arCount = (bytes[10] << 8) | bytes[11];

	std::vector<dns_question> questions;
	auto i = bytes.cbegin() + 12;
	for (std::uint16_t q = 0; q < qdCount; q++) {
		auto question = dns_question::from_bytes(bytes, i);
		if (question)
			questions.push_back(*question);
		else
			return std::unexpected(question.error());
	}

	std::vector<dns_resource> answers;
	for (std::uint16_t a = 0; a < anCount; a++) {
		auto answer = dns_resource::from_bytes(bytes, i);
		if (answer)
			answers.push_back(*answer);
		else
			return std::unexpected(answer.error());
	}

	return dns_packet{
		.id = static_cast<std::uint16_t>(bytes[0] << 8 | bytes[1]),
		.response             = static_cast<bool>(  bytes[2] & 0b10000000),
		.query_type           = static_cast<OPCODE>(bytes[2] & 0b01111000),
		.authoritative_answer = static_cast<bool>(  bytes[2] & 0b00000100),
		.truncated            = static_cast<bool> ( bytes[2] & 0b00000010),
		.recursion_desired    = static_cast<bool>(  bytes[2] & 0b00000001),
		.recursion_available  = static_cast<bool>(  bytes[3] & 0b10000000),
		.authentic_data       = static_cast<bool>(  bytes[3] & 0x00100000),
		.checking_disabled    = static_cast<bool>(  bytes[3] & 0x00010000),
		.response_type        = static_cast<RCODE>( bytes[3] & 0b00001111),
		.questions = std::move(questions),
		.answers = std::move(answers)
	};
}