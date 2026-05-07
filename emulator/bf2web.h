#pragma once
#ifndef _GAMESPY_BF2WEB_H_
#define _GAMESPY_BF2WEB_H_

#include <vector>
#include <string_view>
#include <string>
#include <ranges>
#include <type_traits>

namespace gamespy
{
	namespace bf2web
	{
		class response
		{
			bool error = false;
			std::uint32_t error_code;

		public:
			void SetError(bool error, std::uint32_t code = 0)
			{
				this->error = error;
				this->error_code = code;
			}

			std::string ToString() const
			{
				std::string::size_type errlen = 0;
				auto res = std::string{};
				if (!error)
					res += "O\n";
				else {
					res += "E";
					
					if (error_code) {
						res += '\t';

						auto code = std::to_string(error_code);
						errlen += code.length();
						res += code;
					}

					res += '\n';
				}

				return res + str + "$\t" + std::to_string(1 + length + errlen) + "\t$";
			}

			enum class type
			{
				HEADER,
				DATA
			};

			template<typename ...V>
			void Append(type t, V... args)
			{
				length += 1;
				if (t == type::HEADER)
					str += "H\t";
				else
					str += "D\t";

				(AppendElement(args), ...);

				str += '\n';
			}

		private:
			std::string str{};
			std::string::size_type length{ 0 };

			template<typename T>
			std::enable_if_t<std::is_integral_v<T>> AppendElement(const T& _elem)
			{
				const auto& elem = std::to_string(_elem);
				length += elem.length();
				str += elem;
				str += '\t';
			}

			template<typename T>
			std::enable_if_t<std::is_convertible_v<T, std::string_view>> AppendElement(const T& elem)
			{
				AppendElement<std::string_view>(elem);
			}
		};
	}
}

template<>
void gamespy::bf2web::response::AppendElement(const std::string_view& elem);

#endif