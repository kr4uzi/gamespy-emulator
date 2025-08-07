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
- Linux: ./configure.sh && cd emulator && make
- Windows: execute the configure.bat, then open the .sln and build the solution using the Visual Studio GUI