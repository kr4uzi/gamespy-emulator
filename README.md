GameSpy Server Emulator
- login + retrieve profile
- sqlite3 and mysql database support
- server browsing
- stats endpoint

GameSpy Redirector
- redirect all calls to localhost
- fix the long startup process

Used libraries / techniques:
- C++ Coroutines
- Boost::Asio
- Microsoft Detours (to inject the redirector into the BF2 process)

Other resources:
- BF2 Statistics (weapon unlocks)
- BF2 CD Key Changer: https://github.com/art567/bf2keyman

Battlefield 2:
- Note: this emulator has native support for bf2stats
- Punktbuster Update: resources/bf2_punkbuster.zip
- CD-Key (Registry + KeyGen): resources/bf2_cdkey.zip
  -> Source for KeyGen: https://github.com/art567/bf2keyman
- edit and use the start_bf2.bat to make your client connect to a configurable gamespy server

Developing:
Windows: execute the configure.bat, then open the .sln and build the solution using the Visual Studio GUI

Linux:
On Linux you probably need to install vcpkg first:
cd ~/source
wget https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz
tar -xzf boost_1_88_0.tar.gz
cd boost_1_88_0
./bootstrap.sh --with-libraries=asio,serialization,crc,charconv,iterator,mysql
./b2 link=static runtime-link=static

cd ~/source
git clone https://github.com/nlohmann/json.git --depth 1

cd ~/source/gamespy-emulator
./configure.sh
cd emulator
export CXX=g++
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/source/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux-static

