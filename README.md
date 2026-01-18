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

## Limitations

- MBC1 only (no MBC2, MBC3, MBC5)
- No save RAM support
- Serial transfer not functional
