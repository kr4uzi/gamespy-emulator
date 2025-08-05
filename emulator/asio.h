#pragma once
#ifndef _GAMESPY_EMULATOR_ASIO_H_
#define _GAMESPY_EMULATOR_ASIO_H_

// For whatever reason the sdkddkver.h include is missing in asio
// The following conditions are copied from /boost/asio/detail/config.hpp
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
# if !defined(_WIN32_WINNT) && !defined(_WIN32_WINDOWS)
#  if defined(_MSC_VER) || (defined(__BORLANDC__) && !defined(__clang__))
#   include <sdkddkver.h>
#  endif // !defined(_WIN32_WINNT) && !defined(_WIN32_WINDOWS)
# endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#endif

#include <boost/asio.hpp>

#endif // _GAMESPY_EMULATOR_ASIO_H_