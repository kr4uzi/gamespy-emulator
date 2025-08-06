#pragma once
#ifndef _GAMESPY_SQLITE_H_
#define _GAMESPY_SQLITE_H_
#include <cstdint>
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <algorithm>
#include <format>
#include <type_traits>
#include <functional>
#include <map>
#include <tuple>
#include <ranges>

namespace sqlite
{
	struct error : public std::runtime_error
	{
		using std::runtime_error::runtime_error;
	};

	enum class auth_action
	{
		SQLITE_CREATE_INDEX = 1,
		SQLITE_CREATE_TABLE = 2,
		SQLITE_CREATE_TEMP_INDEX = 3,
		SQLITE_CREATE_TEMP_TABLE = 4,
		SQLITE_CREATE_TEMP_TRIGGER = 5,
		SQLITE_CREATE_TEMP_VIEW = 6,
		SQLITE_CREATE_TRIGGER = 7,
		SQLITE_CREATE_VIEW = 8,
		SQLITE_DELETE = 9,
		SQLITE_DROP_INDEX = 10,
		SQLITE_DROP_TABLE = 11,
		SQLITE_DROP_TEMP_INDEX = 12,
		SQLITE_DROP_TEMP_TABLE = 13,
		SQLITE_DROP_TEMP_TRIGGER = 14,
		SQLITE_DROP_TEMP_VIEW = 15,
		SQLITE_DROP_TRIGGER = 16,
		SQLITE_DROP_VIEW = 17,
		SQLITE_INSERT = 18,
		SQLITE_PRAGMA = 19,
		SQLITE_READ = 20,
		SQLITE_SELECT = 21,
		SQLITE_TRANSACTION = 22,
		SQLITE_UPDATE = 23,
		SQLITE_ATTACH = 24,
		SQLITE_DETACH = 25,
		SQLITE_ALTER_TABLE = 26,
		SQLITE_REINDEX = 27,
		SQLITE_ANALYZE = 28,
		SQLITE_CREATE_VTABLE = 29,
		SQLITE_DROP_VTABLE = 30,
		SQLITE_FUNCTION = 31,
		SQLITE_SAVEPOINT = 32,
		SQLITE_COPY = 0,
		SQLITE_RECURSIVE = 33
	};

	enum class auth_res
	{
		SQLITE_OK     = 0,
		SQLITE_DENY   = 1,
		SQLITE_IGNORE = 2
	};

	class stmt;
	class db
	{
		friend class stmt;
		static void close(void* db);

		std::unique_ptr<void, decltype(&db::close)> m_DB;
		std::function<auth_res(auth_action action, const std::string_view& detail1, const std::string_view& detail2, const std::string_view& dbName, const std::string_view& trigger)> m_Authorizer;

	public:
		using rowid_t = std::int64_t;

		db(const std::string& memoryName, bool shared);
		db(const std::filesystem::path& dbFile);

		void exec(const std::string& sql);
		void exec(const std::string& sql, std::function<bool(const std::map<std::string_view, std::string_view>&)> handler);

		rowid_t last_insert_rowid() noexcept;

		void set_authorizer(decltype(m_Authorizer) authorizer);

	private:
		class scoped_authorizer
		{
			friend class db; db& m_DB;
			scoped_authorizer(db& db) : m_DB{ db } {}

		public:
			~scoped_authorizer() { m_DB.set_authorizer(nullptr); }
		};

	public:
		[[nodiscard]]
		scoped_authorizer set_scoped_authorizer(decltype(m_Authorizer) authorizer) { set_authorizer(authorizer); return scoped_authorizer{ *this }; }
	};

	namespace detail {
		// stmt_format is used to check if the number of bound values matches the placeholder (? - char) count
		template<typename... T>
		struct basic_stmt_format
		{
			template<class S> requires std::convertible_to<S, std::string_view>
			consteval basic_stmt_format(const S& s)
				: m_Str(s)
			{
				if (sizeof...(T) && std::ranges::count(m_Str, '?') != sizeof...(T))
					throw std::format_error{ "invalid format" };
			}

			constexpr std::string_view get() const noexcept { return m_Str; }

		private:
			std::string_view m_Str;
		};

		template<typename... T>
		using stmt_format = basic_stmt_format<std::type_identity_t<T>...>;
	}

	class stmt
	{
		static void finalize(void* stmt);
		typedef std::unique_ptr<void, decltype(&stmt::finalize)> stmt_ptr_t;
		static stmt_ptr_t prepare(void* db, const std::string_view& sql);

		void* m_DB;
		stmt_ptr_t m_Stmt;

	public:
		template<typename... T>
		stmt(db& db, const detail::stmt_format<T...> sql, T&&... t)
			: m_Stmt(prepare(db.m_DB.get(), sql.get()))
		{
			bind(std::forward<T>(t)...);
		}

		stmt(db& db, const std::string_view& str)
			: m_Stmt(prepare(db.m_DB.get(), str))
		{

		}

		std::size_t columns();
		std::string_view column_name(std::size_t pos);

		template<std::size_t I = 0, typename T, typename... R>
		inline void bind(T t, R... r)
		{
			bind_at(I + 1, t);
			bind<I + 1, R...>(r...);
		}

		template<std::size_t I = 0>
		inline void bind()
		{

		}

		void reset();
		void insert();
		void update() { insert(); }
		
		bool query();

		template<typename... T>
		bool query(std::tuple<T...>& row)
		{
			if (query()) {
				set_from_columns(row, std::index_sequence_for<T...>{});
				return true;
			}

			return false;
		}

		void bind_at(std::size_t pos, const std::string& str);
		void bind_at(std::size_t pos, const std::string_view& str);
		void bind_at(std::size_t pos, std::int32_t val);
		void bind_at(std::size_t pos, std::int64_t val);

		template<typename T>
		T column_at(std::size_t pos)
		{

		}

		template<typename S>
		S column_at(std::size_t pos) requires std::ranges::contiguous_range<S>
		{
			auto str = column_at<const char*>(pos);
			return S{ str ? str : "" };
		}

		template<typename T>
		std::enable_if_t<(sizeof(T) > sizeof(int)) && !std::is_same_v<T, std::int64_t>, T> column_at(std::size_t pos) requires std::integral<T>
		{
			return static_cast<T>(column_at<std::int64_t>(pos));
		}

		template<typename T>
		std::enable_if_t<(sizeof(T) <= sizeof(int)) && !std::is_same_v<T, std::int32_t>, T> column_at(std::size_t pos) requires std::integral<T>
		{
			return static_cast<T>(column_at<std::int32_t>(pos));
		}

		template<>
		const char* column_at(std::size_t pos);

		template<>
		std::int32_t column_at(std::size_t pos);

		template<>
		std::int64_t column_at(std::size_t pos);

		template<typename T>
		std::enable_if_t<!std::is_same_v<T, double>, T> column_at(std::size_t pos) requires std::floating_point<T>
		{
			return static_cast<T>(column_at<double>(pos));
		}

		template<>
		double column_at(std::size_t pos);

		/*template<>
		std::uint32_t column_at(std::size_t pos);

		template<>
		std::uint64_t column_at(std::size_t pos);*/

	private:
		template<typename... T, std::size_t... I>
		void set_from_columns(std::tuple<T...>& row, std::index_sequence<I...> index)
		{
			row = std::make_tuple(column_at<T>(I)...);
		}
	};
}
#endif _GAMESPY_SQLITE_H_