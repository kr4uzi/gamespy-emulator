#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <span>
#include <map>
#include <expected>
#include <chrono>

namespace dns
{
	// https://www.rfc-editor.org/rfc/rfc6895.html
	enum class ParseError
	{
		INCOMPLETE,
		INVALID
	};

	struct dns_question {
		std::string name;

		// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml#dns-parameters-4
		enum class QTYPE : std::uint16_t {
			A = 1,  // Host Address
			NS = 2,  // Authoritative Name Server
			MD = 3,  // Mail Destination (OBSOLETE - use MX)
			MF = 4,  // MAIL Forwarder (OBSOLETE - use MX)
			CNAME = 5,  // Canonical Name for an Alias
			SOA = 6,  // Start of a Zone of Authoritiy
			MB = 7,  // Mailbox Domain Name (EXPERIMENTAL)
			MG = 8,  // Mial Group Member (EXPERIMENTAL)
			MR = 9,  // Mail Rename Domain Name (EXPERIMENTAL)
			NUL = 10, // null RR (EXPERIMENTAL)
			WKS = 11, // Well Known Service Descriptor
			PTR = 12, // Domain Name Pointer
			HINFO = 13, // Host Information
			MINFO = 14, // Mailbox or Mail List Information
			MX = 15, // Mail Exchange
			TEXT = 16, // Text
			AAAA = 28, // IPv6 Address
			SRV = 33, // Service Record
			NAPTR = 35, // Naming AUthority Pointer
		} type;

		// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml#dns-parameters-2
		enum class QCLASS : std::uint16_t {
			// 0 - assignment requires an IETF Standards Action.
			INTERNET = 1, // IN
			// 2 - available for assignment by IETF Consensus as a data CLASS.
			CHAOS = 3, // Moon 1981
			HESIOD = 4, // Dyer 1987
			// (5 - 127) available for assignment by IETF Consensus as data CLASSes only.
			// (128 - 253) available for assignment by IETF Consensus as QCLASSes only.
			NONE = 254,
			ANY = 255,
			// 256 - 32767 assigned by IETF Consensus.
			// 32768 - 65280 assigned based on Specification Required as defined in[RFC 2434].
			// 65280 - 65534 Private Use.
			// 65535 can only be assigned by an IETF Standards Action.
		} klass;

		std::vector<std::uint8_t> to_bytes(std::map<std::string, std::size_t>& nameMap, std::size_t currentOffset) const;
		static std::expected<dns_question, ParseError> from_bytes(const std::span<std::uint8_t>& bytes, std::span<std::uint8_t>::const_iterator& i);
	};

	struct dns_resource : public dns_question {
		std::chrono::seconds ttl; // time to live
		std::vector<std::uint8_t> data;

		std::vector<std::uint8_t> to_bytes(std::map<std::string, std::size_t>& nameMap, std::size_t currentOffset) const;
		static std::expected<dns_resource, ParseError> from_bytes(const std::span<std::uint8_t>& bytes, std::span<std::uint8_t>::const_iterator& i);
	};

	struct dns_packet {
		std::uint16_t id;
		bool response : 1;

		// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml#dns-parameters-5
		enum class OPCODE : std::uint8_t {
			Query = 0,
			IQuery = 1, // Inverse Query (OBSOLETE)
			Status = 2,
			// 3 unassigned
			Notify = 4,
			UPupdate = 5,
			// 6-15 unassinged
		} query_type : 4;

		bool authoritative_answer : 1;
		bool truncated : 1;
		bool recursion_desired : 1;
		bool recursion_available : 1;
		std::uint8_t : 1; // Z
		bool authentic_data : 1;
		bool checking_disabled : 1;

		// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml#dns-parameters-6
		enum class RCODE : std::uint8_t {
			NoError = 0, // No Error
			FormErr = 1, // Format Error
			ServFail = 2, // Server Failure
			NXDomain = 3, // Non-Existent Domain
			NotImp = 4, // Not Implemented
			Refused = 5, //  Query Refused
			YXDomain = 6, // Name Exists when it should not
			YXRRSet = 7, // RR Set Exists when it should not
			NXRRSet = 8, // RR Set that should exist does not
			NotAuth = 9, // Server Not Authoritative for zone / Not Authorized
			NotZone = 10, // Name not contained in zone
			DSOTYPENI = 11, // DSO-TYPE Not Implemented
			// 12 - 15 Unassigned
			BADVERS = 16, // Bad OPT Version
			BADSIG = 16, // TSIG Signature Failure
			BADKEY = 17, // Key not recognized
			BADTIME = 18, // Signature out of time window
			BADMODE = 19, // Bad TKEY Mode
			BADNAME = 20, // Duplicate key name
			BADALG = 21, // Algorithm not supported
			BADTRUNC = 22, // Bad Truncation
			BADCOOKIE = 23 // Bad/missing Server Cookie
			// 24-3840 Unassigned
		} response_type : 4;

		std::vector<dns_question> questions;
		std::vector<dns_resource> answers;
		// name server records
		// additional records

		std::vector<std::uint8_t> to_bytes() const;
		static std::expected<dns_packet, ParseError> from_bytes(const std::span<std::uint8_t>& bytes);
	};
}