cmake_minimum_required(VERSION 3.22)
project(emulator_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable C++20 coroutines (required for boost asio co_await)
if (MSVC)
    add_compile_options(/await)
else()
    add_compile_options(-fcoroutines)
endif()

# Find all .cpp files in the current directory
file(GLOB SOURCES "*.cpp")

add_executable(emulator ${SOURCES})

# Force static linking of runtime and libraries
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

# Find Boost packages
find_package(boost-asio REQUIRED)
find_package(boost-mysql REQUIRED)
find_package(boost-crc REQUIRED)
find_package(boost-iterator REQUIRED)
find_package(boost-serialization REQUIRED)

target_link_libraries(emulator PRIVATE
    Boost::asio
    Boost::mysql
    Boost::crc
    Boost::iterator
    Boost::serialization
)

# Also force static linking on the executable
if (MSVC)
    # On MSVC: Use /MT for static CRT (release)
    foreach(flag_var
        CMAKE_C_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELEASE)
        string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach()
else()
    # On GCC/Clang: link statically against stdlib and libc
    target_link_options(emulator PRIVATE -static -static-libgcc -static-libstdc++)
endif()
