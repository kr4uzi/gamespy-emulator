GameSpy Server Emulator
- create + login + retrieve profile
- sqlite3 and mysql database support (use --help for command line options)
- mysql support via command line args:\
  -playerdb-host=localhost -playerdb-username=bf2stats -playerdb-password=bf2stats -playerdb-database=bf2stats
- server browsing (full standalone implementation, in contrast to other emulators which use some assembly dump)
- stats endpoint for snapshot processing (with bf2 stats support):\
  -stats-host=10.10.0.1\
  in official ranked mode ("sv.ranked 1") this will redirect snapshots to the classic (unofficial) bf2stats http://10.10.0.1/ASP/bf2statistics.php endpoint
- native support for bf2 (enables official ranking system)
- bf2 standalone unlocks (all unlocks) with "sv.useGlobalUnlocks 1"\
  -http-enabled (enables the builtin http server which has primitive "all unlocks enabled" responses)

Usage:
Use the -help flag to see all available options

Other resources:
- BF2 Statistics 4.0.0 (PHP8 support and official ranked server files - see my bf2stats repo)
- BF2 CD Key Changer: https://github.com/art567/bf2keyman
- Punkbuster Update: Download & execute official [PB Setup](https://www.evenbalance.com/downloads/W/gui/pbsetup.zip) and then overwrite the pbgame.htm with the the one in [research/pbgame.htm](https://github.com/kr4uzi/gamespy-emulator/blob/main/research/pbgame.htm). You can then manually add & update Battlefield 2

Battlefield 2:
- Note: this emulator has native support for bf2stats
- Punktbuster Update (latest): resources/bf2_punkbuster.zip
- CD-Key (Registry + KeyGen): resources/bf2_cdkey.zip
  -> Source for KeyGen: https://github.com/art567/bf2keyman
- edit and use the start_bf2.bat to make your client connect to a configurable gamespy server
- alternatively use the dbghelp.dll and dbghelp_original.dll in v.2.1.0 release files

## Build

The emulator uses one root `CMakeLists.txt` for Windows and Linux on x64 and ARM64.
Visual Studio and VS Code can open the repository folder directly. The old manually maintained
solution/project files are replaced by CMake-generated projects.

Requirements: CMake 3.30+, C++23 (GCC 15+ or a recent MSVC), Boost 1.88+, OpenSSL,
SQLite, nlohmann-json, and Python 3 for the integration tests. Visual Studio 2026 requires CMake 4.2+.
C++23 supplies the ranges, coroutine, formatting and printing facilities used here without opting
into a future language mode. Neither the GameSpy SDK nor Detours is needed to build the emulator.

### Using vcpkg

Install [vcpkg](https://github.com/microsoft/vcpkg) and set `VCPKG_ROOT` to its directory.
The root manifest pins the dependency baseline and the presets select the platform's static triplet.

On Windows, install Visual Studio's C++ workload, including the target architecture's build tools:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64
```

For Windows ARM64, replace `windows-x64` with `windows-arm64`. That preset lets CMake select the
installed Visual Studio version. The executable is `build/<preset>/Release/emulator.exe`.
Windows executables use the static C++ runtime and static dependencies.

On Linux, install GCC 15+, CMake, Ninja, pkg-config, and the usual vcpkg bootstrap prerequisites:

```sh
export VCPKG_ROOT=/path/to/vcpkg
export CC=gcc-15
export CXX=g++-15
cmake --preset linux-x64
cmake --build --preset linux-x64
ctest --preset linux-x64
```

Use `linux-arm64` on an ARM64 host. These Linux presets build natively; they do not set up a cross compiler.
The executable is `build/<preset>/emulator`. Its compiler runtime and vcpkg dependencies are static;
it still requires the host's compatible C library and operating-system libraries.

With development dependencies already installed on Linux, vcpkg is optional:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Run from the `emulator/` source directory to use its `http/` files, or create an application
folder (executable, HTTP files and this README) with:

```sh
cmake --install build/<preset> --config Release --prefix dist/package
```

The server creates `players.sqlite3` in its working directory by default. The source `config.json` is
an example enabling a local BF2 MySQL database; adapt it before passing `-gamedb=config.json`.

### GitHub Actions downloads

[Build and test](.github/workflows/build.yml) runs on pushes to `main`, pull requests, and manual dispatch.
It builds and tests four native targets: Windows x64, Windows ARM64, Linux x64, and Linux ARM64.
Linux x64 also runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer.

Open a successful run under **Actions → Build and test** and download the matching
`gamespy-emulator-<platform>-<architecture>` artifact. Extract the Windows ZIP and run `emulator.exe`
from that folder. Linux artifacts contain a `.tar.gz`; extract that archive too, preserving its executable
permissions, then run `./emulator`. The Linux packages are built on Ubuntu 24.04 and require a compatible
glibc (2.39 or newer). Downloads are retained for 30 days. A local compiler or vcpkg installation is not
required to use an artifact.

### Tests and memory checks

CTest covers malformed/truncated packets, wire-format handling, temporary buffer and database lifetimes,
SQLite failures and repeated use, asynchronous TCP sessions, HTTP input validation, and process shutdown.
The tests use disposable SQLite data and local sockets; a live MySQL server or game installation is not required.
They do not replace testing with actual game clients or a configured MySQL/BF2 statistics database.

Local validation used GCC 16.2.1 and Boost 1.92 on Linux x64: all five suites pass in both the normal
Debug build and the ASan/UBSan build with leak detection enabled. Installation, archive permissions,
and launching the packaged executable were also checked. Windows and ARM64 builds are configured
in CI but have not been executed locally.

For a Linux memory-check build, add `-DGAMESPY_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug` to configure,
then build and run CTest. Compiler runtime libraries remain dynamic in this configuration so the sanitizers
can instrument allocations correctly.

### SDK compatibility checks

The protocol changes were checked against the bundled GameSpy SDK at
`d1deb2d1a951cf77933dda040b8d311cc09815a7`: GP connection/search authentication,
`common/gsAvailable.c` availability probes, `qr2/qr2.c` heartbeat and shutdown packets,
`serverbrowsing/sb_serverlist.c` browser fields and push frames, `sb_crypt.c` encryption vectors,
and `gstats/gstats.c` statistics challenge framing. Regression tests include byte-for-byte SDK
cipher fixtures as well as fragmented and combined TCP requests.

This remains an implementation of the supported emulator features, not the whole SDK. Browser group
requests return an empty list because there is no group backend. The alternate source IP field is parsed,
but does not replace the peer address observed on the socket.

### Windows game helpers

The `redirect/` and `bf2-dlls/` folders contain separate Windows game helpers. Their existing scripts and
Detours requirements are independent of the emulator's CMake build and CI artifacts. `configure.ps1`
initializes their submodules and builds Detours; it is not needed for the emulator. The GameSpy SDK
submodule is retained as a protocol reference. These helpers are not covered by the emulator tests;
the redirector still performs DNS/library work during DLL loading and needs separate validation with the game.
