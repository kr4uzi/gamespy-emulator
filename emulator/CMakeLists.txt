cmake_minimum_required (VERSION 3.25)

project(emulator)
set(CMAKE_CXX_STANDARD 26)

file(GLOB SOURCE_FILES *.cpp)

find_package(boost_asio CONFIG REQUIRED)
find_package(boost_crc CONFIG REQUIRED)
find_package(boost_serialization CONFIG REQUIRED)
find_package(boost_mysql 1.88 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(emulator ${SOURCE_FILES})

if(UNIX)
    # this is required for gamespy headers
    target_compile_definitions(emulator PRIVATE _UNIX)
endif()

target_include_directories(emulator PRIVATE "../dependencies/GameSpy/src")

target_link_libraries(emulator PRIVATE Boost::asio)
target_link_libraries(emulator PRIVATE Boost::crc)
target_link_libraries(emulator PRIVATE Boost::serialization)
target_link_libraries(emulator PRIVATE Boost::mysql)
target_link_libraries(emulator PRIVATE OpenSSL::SSL)
target_link_libraries(emulator PRIVATE OpenSSL::Crypto)
target_link_libraries(emulator PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(emulator PRIVATE unofficial::sqlite3::sqlite3)
