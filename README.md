# Ges - Game Boy Emulator

A single-file Game Boy (DMG) emulator written in C++ using SDL2.

## Building

Requires SDL2. Build with:

```bash
make
```

Output: `bin/ges`

## Usage

```bash
./bin/ges [rom_file] [-b boot_rom] [-c cycles] [-br breakpoint]
```

- `rom_file`: Game Boy ROM file (.gb)
- `-b`: Optional boot ROM file
- `-c`: Cycles per frame (default: 69905)
- `-br`: Set breakpoint at hex address (e.g., `-br 0100`)

## Controls

- **Arrow keys**: D-pad
- **Z**: A button
- **X**: B button
- **Enter**: Start
- **Right Shift**: Select
- **ESC**: Quit

## Features

- CPU: Full Z80-like instruction set, interrupts, HALT bug
- Memory: ROM banking (MBC1), VRAM, WRAM, I/O registers
- Display: Background, window, sprites (8x8, 8x16), palettes
- Audio: Square wave channels 1, 2, 4 (envelope, sweep, length)
- Timers: DIV, TIMA with configurable clock
- Input: Joypad emulation

## Limitations

- MBC1 only (no MBC2, MBC3, MBC5)
- No save RAM support
- Sound channel 3 (wave) not implemented
- Serial transfer not functional
