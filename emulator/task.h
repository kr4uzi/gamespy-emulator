#pragma once
#ifndef _GAMESPY_TASK_H_
#define _GAMESPY_TASK_H_

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
# if !defined(_WIN32_WINNT) && !defined(_WIN32_WINDOWS)
#  if defined(_MSC_VER) || (defined(__BORLANDC__) && !defined(__clang__))
#   include <sdkddkver.h>
#  endif // !defined(_WIN32_WINNT) && !defined(_WIN32_WINDOWS)
# endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#endif

#include <boost/asio/awaitable.hpp>
namespace gamespy {
	template<typename T>
	using task = boost::asio::awaitable<T>;
}

#endif