cmake_minimum_required(VERSION 3.30)
project(gamespy_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(GAMESPY_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Boost 1.88 CONFIG REQUIRED COMPONENTS charconv url)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(gamespy_core STATIC
    emulator/admin.cpp
    emulator/bf2.cpp
    emulator/bf2web.cpp
    emulator/emulator.cpp
    emulator/game.cpp
    emulator/gamedb.cpp
    emulator/gpcm.cpp
    emulator/gpcm.client.cpp
    emulator/gpsp.cpp
    emulator/gpsp.client.cpp
    emulator/http.cpp
    emulator/http.client.cpp
    emulator/key.cpp
    emulator/master.cpp
    emulator/ms.cpp
    emulator/ms.client.cpp
    emulator/playerdb.cpp
    emulator/playerdb.mysql.cpp
    emulator/playerdb.sqlite.cpp
    emulator/qr.cpp
    emulator/sapphire.cpp
    emulator/sb_request.cpp
    emulator/sqlite.cpp
    emulator/stats.cpp
    emulator/stats.client.cpp
    emulator/utils.cpp
)
target_include_directories(gamespy_core PUBLIC emulator)
target_link_libraries(gamespy_core PUBLIC
    Boost::headers Boost::charconv Boost::url OpenSSL::SSL OpenSSL::Crypto
    SQLite3::SQLite3 nlohmann_json::nlohmann_json Threads::Threads ${CMAKE_DL_LIBS}
)

if(WIN32)
    target_compile_definitions(gamespy_core PUBLIC
        NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 BOOST_ALL_NO_LIB)
    target_link_libraries(gamespy_core PUBLIC ws2_32 mswsock crypt32)
endif()
if(MSVC)
    target_compile_options(gamespy_core PUBLIC /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8)
else()
    target_compile_options(gamespy_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR "GCC 14 or newer is required for the C++23 library features")
    endif()
    # GCC 14 supplies std::print's platform support in this library.
    target_link_libraries(gamespy_core PUBLIC stdc++exp)
    if(NOT GAMESPY_ENABLE_SANITIZERS)
        target_link_options(gamespy_core PUBLIC -static-libstdc++ -static-libgcc)
    endif()
endif()
if(GAMESPY_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "The sanitizer configuration requires GCC or Clang")
    endif()
    target_compile_options(gamespy_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gamespy_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(emulator emulator/main.cpp)
target_link_libraries(emulator PRIVATE gamespy_core)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name IN ITEMS protocol database session utils)
        add_executable(${test_name}_test tests/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE gamespy_core)
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_test.py $<TARGET_FILE:emulator>)
    set_tests_properties(integration PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
endif()

install(TARGETS emulator RUNTIME DESTINATION .)
install(DIRECTORY emulator/http DESTINATION .)
install(FILES README.md DESTINATION .)
