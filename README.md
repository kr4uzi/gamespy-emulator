GameSpy Server Emulator
- login + retrieve profile
- sqlite3 and mysql database support
- mysql support via command line args: -playerdb-host=localhost -playerdb-username=bf2stats -playerdb-password=bf2stats -playerdb-database=bf2stats
- server browsing
- stats endpoint
- native support for bf2 (enables official ranking system)

Usage:
Use the -help flag to see all available options

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
Windows: execute the configure.ps1, then open the .sln and build the solution using the Visual Studio GUI

Note: The configure.ps1 is used mainly to initialize the dependencies and build Microsoft Detours (required for the redirector).

Linux:\
- if you want dependencies to be handled by vcpkg:
```shell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./vcpkg_bootstrap.sh
```

- build the emulator
```bash
sudo apt install build-essential g++ cmake pkg-config
git submodule update --init --recursive --depth 1
cd emulator
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/source/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CXX_COMPILER=/usr/local/bin/g++
cmake --build build
```

MacOS:\
```bash
brew install gcc
export CXX=g++-15
export CC=gcc-15
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=~/Source/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```
