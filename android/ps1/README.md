# PS1 BIOS: OpenBIOS

`openbios.bin` is PCSX-Redux's **OpenBIOS**, an open replacement for the PlayStation BIOS (MIT licence, "Copyright (C)
2019-2026 PCSX-Redux authors", as the file itself says). It is the BIOS the PS1 emulator (SwanStation) uses unless the
player picks another file, so no Sony BIOS is needed or shipped.

- Size 524,288 bytes, SHA-256 `fabe498fbf224e4721f12f31b6f5fe0659205e341dc4e5c5f91b9bd1a1011c57`.
- Taken from `github.com/RetroPortingToolKit/psxrecomp` (`bios/openbios.bin`), a copy of the PCSX-Redux build. Source:
  `github.com/pcsx-redux/pcsx-redux`, `src/mips/openbios` (build `make -C src/mips/openbios`). Not yet rebuilt from source.
- SwanStation recognises it by the `OpenBIOS` magic at offset 0x78 (`src/core/bios.cpp`).
