# Prism (vendored)

[Prism](https://github.com/ethindp/prism) is the Platform-agnostic Reader Interface
for Speech and Messages: one API in front of NVDA, JAWS, SAPI, OneCore, UI Automation,
ZoomText, SenseReader, PC-Talker, ZDSR and BoyPC Reader. The mod speaks through it
instead of talking to the NVDA controller client directly.

`prism32.dll` is a 32-bit build, because the mod runs inside Game.exe, which is 32-bit.
Upstream publishes binaries for x64 and ARM64 only, so this one is built here.

## How this copy was produced

- Source: https://github.com/ethindp/prism, commit `98a2b8b3`, version 0.18.2
- Toolchain: Visual Studio 2022 (MSVC 19.44), CMake 4.1, target Win32

```
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32 ^
      -DPRISM_ENABLE_TESTS=OFF -DPRISM_ENABLE_DEMOS=OFF ^
      -DPRISM_ENABLE_GDEXTENSION=OFF -DPRISM_ENABLE_SHIMS=OFF ^
      -DPRISM_LIB_TOOL="<path to MSVC lib.exe for x86>"
cmake --build build-x86 --config Release
```

One change to the upstream tree is needed first: `cmake/PrismOptions.cmake` defaults
`CMAKE_C_STANDARD` to 23, which MSVC does not offer. Setting it to 17 is enough,
because the only hard requirement in `cmake/PrismTargets.cmake` is `c_std_17`.
`PRISM_LIB_TOOL` is needed because the build looks for `lib.exe` on PATH to generate
import libraries for the ZDSR, BoyPC Reader, PC-Talker and bridge DLLs.

Build output `build-x86/Release/prism.dll` is copied here as `prism32.dll`, the header
comes from `include/prism.h` and `prism_version.h` from `build-x86/generated/include`.

## Licence

Mozilla Public License 2.0, see `LICENSE`. Third-party code carried by Prism is listed
in `NOTICE`.
