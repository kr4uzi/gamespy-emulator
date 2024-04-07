GameSpy Server Emulator
- login + retrieve profile
- sqlite3 and mysql database support
- server browsing (not fully implemented)

GameSpy Redirector (prototype) for Battlefield 2
- redirect all calls to localhost
- fix the long startup process

Used libraries / techniques:
- C++ Coroutines
- Boost::Asio
- Microsoft Detours (to inject the redirector into the BF2 process)
