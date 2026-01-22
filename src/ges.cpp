#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Memory and cpu
uint8_t boot_rom[0x100] = {};  // boot rom
uint8_t rom[0x100000] = {};   // cardridge data
uint8_t ram[0x8000] = {};     // cardridge ram / up to 4 x 8kb banks
uint8_t map[0x10000] = {};    // memory space visible by cpu

uint16_t AF = 0; uint8_t& F = *((uint8_t*)&AF); uint8_t& A = *((uint8_t*)&AF + 1);
uint16_t BC = 0; uint8_t& C = *((uint8_t*)&BC); uint8_t& B = *((uint8_t*)&BC + 1);
uint16_t DE = 0; uint8_t& E = *((uint8_t*)&DE); uint8_t& D = *((uint8_t*)&DE + 1);
uint16_t HL = 0; uint8_t& L = *((uint8_t*)&HL); uint8_t& H = *((uint8_t*)&HL + 1);
uint16_t SP = 0;
uint16_t* R16[4] = { &BC, &DE, &HL, &SP };
uint16_t* R16mem[4] = { &BC, &DE, &HL, &HL };
uint16_t* R16stk[4] = { &BC, &DE, &HL, &AF };
uint8_t* R8[8] = { &B, &C, &D, &E, &H, &L, 0, &A };
uint16_t PC = 0;
bool ime = false;  // interrupt master enable
uint8_t ime_true_pending = 0; // enable ime after this number of instructions
bool booting = true;
bool halted = false;

// MBC state
bool mbc_ram_enable = false;
uint8_t mbc_rom_bank = 1;
uint8_t mbc_ram_bank = 0;
uint8_t mbc_banking_mode = 0;
// MBC Cardridge info
uint8_t mbc_type_id = 0;
uint8_t mbc_type = 0;
uint8_t mbc_rom_size_info = 0;
uint8_t mbc_rom_banks = 0;
uint8_t mbc_ram_size_info = 0;
uint8_t mbc_ram_banks = 0;

// Flag masks
#define Fz_mask (1 << 7)
#define Fn_mask (1 << 6)
#define Fh_mask (1 << 5)
#define Fc_mask (1 << 4)
#define Fz ((F & Fz_mask) >> 7)
#define Fn ((F & Fn_mask) >> 6)
#define Fh ((F & Fh_mask) >> 5)
#define Fc ((F & Fc_mask) >> 4)


// Constants
int CYCLES_PR_FRAME = 69905; // close to spec 4194304 cyc/sec at 60hz
int SCREENSCALE = 2;
int LCD_SCANLINES = 154;
int LCD_HEIGHT = 144;
int LCD_WIDTH = 160;
int LCD_CYCLES_PER_SCANLINE = 456;

// Virtual LCD display. 160 x 144 pixels, each 3 bytes for rgb
uint32_t screen[160 * 144] = {};
static const uint32_t palette_colors[4] = {0x00000000, 0xFFAAAAAA, 0xFF555555, 0xFF000000};

// IO Registers
uint8_t& REG_JOYP  = map[0xFF00];
uint8_t& REG_SB    = map[0xFF01];
uint8_t& REG_SC    = map[0xFF02];
uint8_t& REG_DIV   = map[0xFF04];
uint8_t& REG_TIMA  = map[0xFF05];
uint8_t& REG_TMA   = map[0xFF06];
uint8_t& REG_TAC   = map[0xFF07];
uint8_t& REG_IF    = map[0xFF0F];
uint8_t& REG_NR10  = map[0xFF10];
uint8_t& REG_NR11  = map[0xFF11];
uint8_t& REG_NR12  = map[0xFF12];
uint8_t& REG_NR13  = map[0xFF13];
uint8_t& REG_NR14  = map[0xFF14];
uint8_t& REG_NR21  = map[0xFF16];
uint8_t& REG_NR22  = map[0xFF17];
uint8_t& REG_NR23  = map[0xFF18];
uint8_t& REG_NR24  = map[0xFF19];
uint8_t& REG_NR30  = map[0xFF1A];
uint8_t& REG_NR31  = map[0xFF1B];
uint8_t& REG_NR32  = map[0xFF1C];
uint8_t& REG_NR33  = map[0xFF1D];
uint8_t& REG_NR34  = map[0xFF1E];
uint8_t& REG_NR41  = map[0xFF20];
uint8_t& REG_NR42  = map[0xFF21];
uint8_t& REG_NR43  = map[0xFF22];
uint8_t& REG_NR44  = map[0xFF23];
uint8_t& REG_NR51  = map[0xFF25];
uint8_t& REG_NR52  = map[0xFF26];
uint8_t& REG_LCDC  = map[0xFF40];
uint8_t& REG_STAT  = map[0xFF41];
uint8_t& REG_SCY   = map[0xFF42];
uint8_t& REG_SCX   = map[0xFF43];
uint8_t& REG_LY    = map[0xFF44];
uint8_t& REG_LYC   = map[0xFF45];
uint8_t& REG_BGP   = map[0xFF47];
uint8_t& REG_OBP0  = map[0xFF48];
uint8_t& REG_OBP1  = map[0xFF49];
uint8_t& REG_WY    = map[0xFF4A];
uint8_t& REG_WX    = map[0xFF4B];
uint8_t& REG_IE    = map[0xFFFF];

// Timers
uint16_t sys_counter = 0; // System counter for timers
uint16_t tima_timer_cycles = 0;
uint8_t div_apu = 0;     // Running at 512 Hz
uint16_t serial_timer = 0; // Serial transfer timer

// LCD state
uint16_t lcd_window_line = 0;
uint16_t lcd_scanline_cycles = 0;

// Sound channel 1 state
bool sound_ch1_length_enable = false;
uint8_t sound_ch1_length_timer = 0;
uint16_t sound_ch1_period_divider = 0;
uint8_t sound_ch1_envelope_timer = 0;
uint8_t sound_ch1_volume = 0;
// Sweep  (only for channel 1)
uint8_t sound_ch1_frq_sweep_timer = 0;
bool sound_ch1_frq_sweep_enabled = false;

// Sound channel 2 state
bool sound_ch2_length_enable = false;
uint8_t sound_ch2_length_timer = 0;
uint16_t sound_ch2_period_divider = 0;
uint8_t sound_ch2_envelope_timer = 0;
uint8_t sound_ch2_volume = 0;

// Sound channel 3 state
bool sound_ch3_length_enable = false;
uint8_t sound_ch3_length_timer = 0;
uint16_t sound_ch3_period_divider = 0;
uint8_t sound_ch3_volume = 0;

// Sound channel 4 state
bool sound_ch4_length_enable = false;
uint8_t sound_ch4_length_timer = 0;
uint8_t sound_ch4_envelope_timer = 0;
uint8_t sound_ch4_volume = 0;
uint16_t sound_ch4_lfsr = 0;

// Keypad state
uint8_t keys_state = 0x00; // all released
uint8_t dpad_state = 0x00; // all released

bool verbose_logging = false;
#define log_v_printf(x, ...) { if (verbose_logging) printf(x, ##__VA_ARGS__); }

void dump_regs()
{
    printf("Dumping regs:\n");
    printf("AF: %04x\n", AF);
    printf("BC: %04x\n", BC);
    printf("DE: %04x\n", DE);
    printf("HL: %04x\n", HL);
    printf("SP: %04x\n", SP);
    printf("PC: %04x\n", PC);
}


void cpu_boot()
{
    PC = 0;
}

void post_boot_teleport()
{
    printf("Initializing to post-boot..\n");
    booting = false;

    // Registers
    AF = 0x01b0; BC = 0x0013; DE = 0x00d8; HL = 0x014d; SP = 0xfffe; PC = 0x0100;

    // Mapped i/o ports coming out of boot (on pc=0x100)
    static const struct { uint16_t addr; uint8_t val; } io_init[] = {
        {0xff00,0xcf}, {0xff01,0x00}, {0xff02,0x7e}, {0xff04,0xab}, {0xff05,0x00}, {0xff06,0x00}, {0xff07,0xf8},
        {0xff0f,0xe1}, {0xff10,0x80}, {0xff11,0xbf}, {0xff12,0xf3}, {0xff13,0xff}, {0xff14,0xbf},
        {0xff16,0x3f}, {0xff17,0x00}, {0xff18,0xff}, {0xff19,0xbf}, {0xff1a,0x7f}, {0xff1b,0xff}, {0xff1c,0x9f}, {0xff1d,0xff},
        {0xff1e,0xbf}, {0xff20,0xff}, {0xff21,0x00}, {0xff22,0x00}, {0xff23,0xbf}, {0xff24,0x77}, {0xff25,0xf3}, {0xff26,0xf1},
        {0xff40,0x91}, {0xff41,0x85}, {0xff42,0x00}, {0xff43,0x00}, {0xff44,0x00}, {0xff45,0x00}, {0xff46,0xff},
        {0xff47,0xfc}, {0xff48,0xff}, {0xff49,0xff}, {0xff4a,0x00}, {0xff4b,0x00}, {0xffff,0x00}
    };
    for(size_t i = 0; i < sizeof(io_init) / sizeof(io_init[0]); i++)
        map[io_init[i].addr] = io_init[i].val;
}

uint8_t read(uint16_t addr)
{
    // Bank 0 rom
    if(addr <= 0x3FFF) {
        return booting ? boot_rom[addr] : rom[addr];
    }
    else if (addr <= 0x7FFF) {
        uint8_t bank_mask = mbc_rom_banks - 1;
        uint32_t bank_base = (mbc_rom_bank & bank_mask) * 0x4000;
        return rom[bank_base + addr - 0x4000];
    }
    // VRAM
    else if (addr >= 0x8000 && addr <= 0x9FFF) {
        return map[addr];
    }
    // RAM
    else if (addr >= 0xA000 && addr <= 0xBFFF) {
        if(mbc_ram_enable) {
            uint8_t bank = 0;
            if(mbc_banking_mode == 1)
                bank = mbc_ram_bank;
            return ram[addr - 0xA000 + bank * 0x2000];
        }
        else {
            printf("Trying to read from disabled ram at addr %04x\n", addr);
            return 0xFF;
        }
    }
    // WRAM
    else if (addr >= 0xC000 && addr <= 0xDFFF) {
        return map[addr];
    }
    // WRAM (ECHO)
    else if(addr >= 0xE000 && addr <= 0xFDFF) {
        return map[addr - 0x2000];
    }
    // Forbidden range
    else if(addr >= 0xFEA0 && addr <= 0xFEFF) {
        printf("Trying to read from forbidden range %04x\n", addr);
        exit(1);
    }
    // I/O Range and HRAM and Interrupt
    else if (addr >= 0xFF00) {
        return map[addr];
    }
    else {
        printf("Trying to read from unsupported addr %04x\n", addr);
        exit(1);
    }
    return 0;
}


void write(uint16_t addr, uint8_t value)
{
    // Check against writing to rom
    if(addr <= 0x1FFF) {
        log_v_printf("Ram enable %02x (written to %04x)\n", value, addr);
        if((value & 0x0F) == 0x0A)
            mbc_ram_enable = true;
        else
            mbc_ram_enable = false;
    }
    else if (addr <= 0x3FFF) {
        if(mbc_type == 1) {
            mbc_rom_bank = value & 0x1F;
            if(mbc_rom_bank == 0) mbc_rom_bank++;
            log_v_printf("MBC1: Rom bank selected value: %02x bank: %02x\n", value, mbc_rom_bank);
        }
        else {
            printf("Trying to write %02x to rom addr %04x\n", value, addr);
            printf("Unknown MBC type %02x\n", mbc_type);
            exit(1);
        }
    }
    else if (addr <= 0x5FFF) {
        log_v_printf("MBC ram/rom bank select %02x\n", value);
        mbc_ram_bank = value & 0x03;
    }
    else if (addr <= 0x7FFF) {
        log_v_printf("MBC bank mode %02x\n", value);
        mbc_banking_mode = value & 1;
    }
    else if (addr < 0x8000) {
        printf("Trying to write %02x to rom addr %04x\n", value, addr);
        return;
    }
    // VRAM
    else if (addr >= 0x8000 && addr <= 0x9FFF) {
        map[addr] = value;
    }
    else if (addr >= 0xA000 && addr <= 0xBFFF) {
        if(mbc_ram_enable) {
            uint8_t bank = 0;
            if(mbc_banking_mode == 1)
                bank = mbc_ram_bank;
            ram[addr - 0xA000 + bank * 0x2000] = value;
        }
        else {
            printf("Trying to write to disabled ram at addr %04x\n", addr);
        }
    }
    // WRAM
    else if (addr >= 0xC000 && addr <= 0xDFFF) {
        map[addr] = value;
    }
    // ECHO range
    else if(addr >= 0xE000 && addr <= 0xFDFF) {
        map[addr - 0x2000] = value;
    }
    else if (addr >= 0xFE00 && addr <= 0xFE9F) {
        map[addr] = value;
    }
    else if(addr >= 0xFEA0 && addr <= 0xFEFF) {
        //printf("Trying to write %02x to forbidden range %04x (PC=%04x)\n", value, addr, PC);
        //halted=true;
    }
    // Ports
    else if(addr >= 0xFF00 && addr < 0xFF80)
    {
        if(addr == 0xFF00) {
            if((value & 0x30) == 0x10) {
                // NOTE: Inverted logic; Buttons
                map[addr] = 0xC0 | 0x10 | ((~keys_state) & 0x0F);
            }
            else if((value & 0x30) == 0x20) {
                // NOTE: Inverted logic; D-pad
                map[addr] = 0xC0 | 0x20 | ((~dpad_state) & 0x0F);
            }
            else {
                // Neither or both
                map[addr] = 0xC0 | (value & 0x30) | 0x0F;
            }
        }
        else if(addr == 0xFF01) {
            log_v_printf("Serial write: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF02) {
            log_v_printf("Serial control write: %02x\n", value);
            value |= 0x7E;
            map[addr] = value;
        }
        else if(addr == 0xFF04) {
            // DIV register reset
            sys_counter = 0;
            tima_timer_cycles = 0;
            map[addr] = 0;
        }
        else if(addr == 0xFF05) {
            log_v_printf("TIMA: %02x\n", value);
            map[addr] = value;
        }
        else if(addr == 0xFF06) {
            log_v_printf("TMA: %02x\n", value);
            map[addr] = value;
        }
        else if(addr == 0xFF07) {
            log_v_printf("TAC: %02x\n", value);
            value |= 0xF8;
            map[addr] = value;
        }
        else if(addr == 0xFF26) {
            log_v_printf("Turning sound %s. %02x\n", value & 0x80 ? "On": "Off", value);
            map[addr] = (map[addr] & 0x7F) | (value & 0x80);
        }
        else if (addr == 0xFF0F) {
            log_v_printf("IF: %02x\n", value);
            value |= 0xE0;
            map[addr] = value;
        }
        else if (addr == 0xFF10) {
            log_v_printf("NR10: Sound sweep %02x\n", value);
            value |= 0x80;
            map[addr] = value;
        }
        else if (addr == 0xFF11) {
            log_v_printf("NR11: Sound duty %02x length %02x\n", value >> 6, value & 0x3F);
            map[addr] = value;
        }
        else if (addr == 0xFF12) {
            uint8_t vol = value >> 4;
            uint8_t env = (value & 0x8) >> 3;
            uint8_t sweep = value & 0x7;
            log_v_printf("NR12: Chan 1: vol %i, env %i, sweep %i\n", vol, env, sweep);
            map[addr] = value;
        }
        else if (addr == 0xFF13) {
            log_v_printf("NR13: Soundperiod-low %i\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF14) {
            log_v_printf("NR14: Soundperiod-high %02x\n", value);
            value |= 0x38;
            map[addr] = value;
            if(value & 0x80) {
                REG_NR52 |= 0x1; // turn on chan 1
                sound_ch1_length_enable = (value & 0x40);
                if(sound_ch1_length_enable) {
                    sound_ch1_length_timer = (REG_NR11 & 0x3F);
                }
                sound_ch1_period_divider = REG_NR13 | ((REG_NR14 & 0x7) << 8);
                sound_ch1_envelope_timer = 0;
                sound_ch1_volume = REG_NR12 >> 4;
                sound_ch1_frq_sweep_timer = 0;
                sound_ch1_frq_sweep_enabled = (REG_NR10 & 0x77) != 0;
                log_v_printf("  -> Triggering sound on chan 1, length timer: %i\n", sound_ch1_length_timer);
            }
        }
        else if (addr == 0xFF16) {
            log_v_printf("NR21: Sound duty %02x length %02x\n", value >> 6, value & 0x3F);
            map[addr] = value;
        }
        else if (addr == 0xFF17) {
            uint8_t vol = value >> 4;
            uint8_t env = (value & 0x8) >> 3;
            uint8_t sweep = value & 0x7;
            log_v_printf("NR22: Chan 2: vol %i, env %i, sweep %i\n", vol, env, sweep);
            map[addr] = value;
        }
        else if (addr == 0xFF18) {
            log_v_printf("NR23: Chan 2: Soundperiod-low %i\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF19) {
            log_v_printf("NR24: Soundperiod-high %02x\n", value);
            value |= 0x38;
            map[addr] = value;
            if(value & 0x80) {
                log_v_printf("  -> Triggering sound on chan 2\n");
                REG_NR52 |= 0x2; // turn on chan 2
                sound_ch2_length_enable = (value & 0x40);
                if(sound_ch2_length_enable && sound_ch2_length_timer == 0) {
                    sound_ch2_length_timer = REG_NR21 & 0x3F;
                }
                sound_ch2_period_divider = REG_NR23 | ((REG_NR24 & 0x7) << 8);
                sound_ch2_envelope_timer = 0;
                sound_ch2_volume = REG_NR22 >> 4;
            }
        }
        else if (addr == 0xFF1A) {
            log_v_printf("NR30 Sound on/off %02x\n", value);
            value |= 0x7F;
            map[addr] = value;
        }
        else if (addr == 0xFF1B) {
            log_v_printf("NR31 Sound length %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF1C) {
            log_v_printf("NR32 Sound volume %02x\n", value);
            value |= 0x9F;
            map[addr] = value;
        }
        else if (addr == 0xFF1D) {
            log_v_printf("NR33 Sound period low %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF1E) {
            log_v_printf("NR34: Sound period high %02x\n", value);
            value |= 0x38;
            map[addr] = value;
            if(value & 0x80) {
                log_v_printf("  -> Triggering sound on chan 3\n");
                REG_NR52 |= 0x4; // turn on chan 3
                sound_ch3_length_enable = (value & 0x40);
                if(sound_ch3_length_enable) {
                    sound_ch3_length_timer = REG_NR31;
                }
                sound_ch3_period_divider = REG_NR33 | ((REG_NR34 & 0x7) << 8);
                sound_ch3_volume = (REG_NR32 >> 5) & 0x3;
            }
        }
        else if (addr == 0xFF20) {
            log_v_printf("NR41 Sound length %02x\n", value);
            value |= 0xC0;
            map[addr] = value;
        }
        else if (addr == 0xFF21) {
            log_v_printf("NR42 Sound envelope %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF22) {
            log_v_printf("NR43 Freq and randomness %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF23) {
            log_v_printf("NR44 Sound trigger %02x\n", value);
            value |= 0x3F;
            if(value & 0x80) {
                log_v_printf("  -> Triggering sound on chan 4\n");
                REG_NR52 |= 0x8; // turn on chan 4
                sound_ch4_length_enable = (value & 0x40);
                if(sound_ch4_length_enable) {
                    sound_ch4_length_timer = REG_NR41 & 0x3F;
                }
                sound_ch4_envelope_timer = 0;
                sound_ch4_lfsr = 0;
                sound_ch4_volume = REG_NR42 >> 4;
            }
            map[addr] = value;
        }
        else if (addr == 0xFF24) {
            log_v_printf("Master & vin: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF25) {
            log_v_printf("Sound pan %02x\n", value);
            map[addr] = value;
        }
        else if (addr >= 0xFF30 && addr <= 0xFF3F) {
            log_v_printf("Wave pattern %04x : %02x\n", addr, value);
            map[addr] = value;
        }
        else if (addr == 0xFF40) {
            log_v_printf("LCDC: %02x\n", value);
            map[addr] = value;
        }
        else if(addr == 0xFF41) {
            log_v_printf("STAT: %02x\n", value);
            value |= 0x80;
            map[addr] = value;
        }
        else if (addr == 0xFF42) {
            map[addr] = value;
        }
        else if (addr == 0xFF43) {
            log_v_printf("SCX: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF45) {
            log_v_printf("LYC: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF46) {
            log_v_printf("DMA: %02x\n", value);
            map[addr] = value;
            uint16_t source_addr = value << 8;
            for(int i = 0; i < 0xA0; i++) {
                map[0xFE00 + i] = read(source_addr + i);
            }
        }
        else if (addr == 0xFF47) {
            log_v_printf("BG palette %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF48) {
            log_v_printf("OBP0: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF49) {
            log_v_printf("OBP1: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF4A) {
            log_v_printf("WY: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF4B) {
            log_v_printf("WX: %02x\n", value);
            map[addr] = value;
        }
        else if (addr == 0xFF4D) {
            log_v_printf("KEY1: %02x\n", value);
            map[addr] = value;
        }   
        else {
            printf("Trying to write %02x to unsupported port on addr %04x\n", value, addr);
            dump_regs();
            halted=true;
        }
    }
    // HRAM
    else if (addr >= 0xFF80) {
        if(addr == 0xFFFF) {
            log_v_printf("IE: %02x\n", value);
            //value |= 0xE0;
        }
        map[addr] = value;
    }
    else {
        printf("Trying to write to unsupported addr %04x\n", addr);
        halted=true;
    }
}

const int OP_T_STATES[] = {

//   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
        4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4, /* 0x00 */
        4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4, /* 0x10 */
        8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4, /* 0x20 */
        8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4, /* 0x30 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x40 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x50 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x60 */
        8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x70 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x80 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0x90 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0xA0 */
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4, /* 0xB0 */
        8,12,12,16,12,16, 8,16, 8,16,12, 8,12,24, 8,16, /* 0xC0 */
        8,12,12, 0,12,16, 8,16, 8,16,12, 0,12, 0, 8,16, /* 0xD0 */
        12,12,8, 0, 0,16, 8,16,16, 4,16, 0, 0, 0, 8,16, /* 0xE0 */
        12,12,8, 4, 0,16, 8,16,12, 8,16, 4, 0, 0, 8,16  /* 0xF0 */
};

static inline void render_square_channel(float* fstream, int len, uint8_t ch_enable_mask, uint8_t pan_left_mask, uint8_t pan_right_mask, uint8_t duty_reg, uint16_t period_divider, uint8_t volume, float* phase)
{
    static const uint8_t duty_masks[] = { 0x7F, 0x7E, 0x1E, 0x81 };
    float vol = (REG_NR52 & ch_enable_mask) ? 1.0f : 0.0f;
    vol *= volume / 15.0f;
    float panleft = (REG_NR51 & pan_left_mask) ? 1.0f : 0.0f;
    float panright = (REG_NR51 & pan_right_mask) ? 1.0f : 0.0f;
    uint8_t duty = duty_reg >> 6;
    float rate = 1048576.0f / (2048 - period_divider);
    uint8_t duty_mask = duty_masks[duty];
    for(int i = 0, c = len / 4; i < c; i += 2) {
        uint8_t iphase = (uint8_t)*phase & 0x0F;
        bool low = (1 << iphase) & duty_mask;
        float s = low ? -0.25f : 0.25f;
        float l = s * vol * panleft;
        float r = s * vol * panright;
        fstream[i]   += l;
        fstream[i + 1] += r;
        *phase = *phase + rate / 48000.0f;
        while(*phase >= 8.0f) *phase -= 8.0f;
    }
}

// Simple audio callback playing a square wave
void audio_callback(void* /*userdata*/, Uint8* stream, int len)
{
    static float ch1_phase = 0;
    static float ch2_phase = 0;
    static float ch3_phase = 0;
    static float ch4_phase = 0;
    float* fstream = (float*)stream;

    memset(fstream, 0, len); // clear to 0.0 so we can add channel contributions

    // Early return if sound is disabled
    if((REG_NR52 & 0x80) == 0) {
        return;
    }

    // CH1 - square with envelope and frequency sweep
    render_square_channel(fstream, len, 0x01, 0x10, 0x01, REG_NR11, sound_ch1_period_divider, sound_ch1_volume, &ch1_phase);

    // CH2 - square with envelope
    render_square_channel(fstream, len, 0x02, 0x20, 0x02, REG_NR21, sound_ch2_period_divider, sound_ch2_volume, &ch2_phase);   

    // CH3 - waveform
    float ch3_volume = (REG_NR52 & 0x04) ? 1.0f : 0.0f;
    float ch3_panleft = (REG_NR51 & 0x40) ? 1.0f : 0.0f;
    float ch3_panright = (REG_NR51 & 0x04) ? 1.0f : 0.0f;
    float ch3_rate = 2097152.0f / (2048 - sound_ch3_period_divider);
    for(int i = 0, c = len / 4; i < c; i += 2) {
        uint8_t iphase = (uint8_t)ch3_phase & 0x1F;
        uint8_t sample = read(0xFF30 + (iphase >> 1));
        sample = (iphase & 1) == 0 ? sample >> 4 : sample & 0x0F; // Upper nibble first
        float s = 0.5f * sample / 15.0f - 0.25f; //  -0.25f - 0.25f
        static const float volume[] = {0.0f, 1.0f, 0.5f, 0.25f};
        s *= ch3_volume * volume[sound_ch3_volume];
        fstream[i]   += s * ch3_panleft;
        fstream[i + 1] += s * ch3_panright;
        ch3_phase += ch3_rate / 48000.0f;
        while (ch3_phase >= 32.0f) ch3_phase -= 32.0f;
    }

    // CH4 - noise
    float ch4_vol = (REG_NR52 & 0x08) ? 1.0f : 0.0f;
    ch4_vol *= sound_ch4_volume / 15.0f;
    float ch4_panleft = (REG_NR51 & 0x80) ? 1.0f : 0.0f;
    float ch4_panright = (REG_NR51 & 0x08) ? 1.0f : 0.0f;

    float ch4_rate = 262144.0f / (REG_NR43 & 0x7 ? REG_NR43 & 0x7 : 0.5f);
    ch4_rate /= (1 << (REG_NR43 >> 4));
    for(int i = 0, c = len / 4; i < c; i += 2) {
        float s = (sound_ch4_lfsr & 0x1) ? -0.25f : 0.25f;
        float l = s * ch4_vol * ch4_panleft;
        float r = s * ch4_vol * ch4_panright;
        fstream[i]   += l;
        fstream[i + 1] += r;
        ch4_phase = ch4_phase + ch4_rate / 48000.0f;
        while(ch4_phase > 1.0f) {
            ch4_phase -= 1.0f;
            uint16_t feedback = (sound_ch4_lfsr & 1) ^ ((sound_ch4_lfsr & 0x2) >> 1);
            feedback = (~feedback & 0x1);
            sound_ch4_lfsr = (sound_ch4_lfsr & 0x7FFF) | (feedback << 15);
            if(REG_NR43 & 0x4) {
                sound_ch4_lfsr = (sound_ch4_lfsr & 0xFF7F) | (feedback << 7);
            }
            sound_ch4_lfsr >>= 1;
        }
    }
}

uint32_t disassemble = 0;
#define OPCODE(x) {if(disassemble > 0){disassemble--; printf("(%04x) %04x: %02x %s\n", sys_counter, PC - 1, op, x);}}

uint16_t break_at = 0xFFFF;
bool show_profile_bar = false;
bool upscale = true;
uint32_t profile_bar[160] = {};

int cpu_tick()
{
    if(ime_true_pending > 0) {
        log_v_printf("IME pending\n");
        if(--ime_true_pending == 0) {
            ime = true;
            log_v_printf("IME set to true\n");
        }
    }
    bool irequested = REG_IE & REG_IF & 0x1F;
    if(irequested)
    {
        // Wake up from halt on any interrupt request
        halted = false;
    }
    if(halted) {
        return 4;
    }
    if(PC == break_at) {
        printf("Reached %04x\n", PC);
        disassemble=1000000;
    }

    int cycles = 0;
    // Handle interrupts.
    if(ime && irequested) {
        for(int i = 0; i < 5; i++) {
            if( (REG_IF & REG_IE) & (1<<i) ) {
                log_v_printf("Handling interrupt %i. PC=%04x\n", i, PC);
                ime = false;
                log_v_printf("IME set to false\n");
                REG_IF &= ~(1<<i);
                write(--SP, (PC >> 8) & 0xFF);
                write(--SP, PC & 0xFF);
                PC = 0x40 + i * 8;
                cycles += 20;
                break;
            }
        }
    }

    uint8_t op = read(PC);
    cycles += OP_T_STATES[op];
    PC++;

    if(op == 0x00) {
        OPCODE("NOP");
    }
    else if ((op & 0xCF) == 0x1) {
        OPCODE("LD r16, imm16");
        *R16[(op & 0x30) >> 4] = read(PC) + (read(PC + 1) << 8);
        PC += 2;
    }
    else if ((op & 0xCF) == 0x2) {
        OPCODE("LD [r16mem], a");
        uint16_t *reg = R16mem[(op & 0x30) >> 4];
        write(*reg, A);
        if((op & 0x30) == 0x20) HL++;
        else if ((op & 0x30) == 0x30) HL--;
    }
    else if ((op & 0xCF) == 0xA) {
        OPCODE("LD a, [r16mem]");
        uint16_t *reg = R16mem[(op & 0x30) >> 4];
        A = read(*reg);
        if((op & 0x30) == 0x20) HL++;
        else if ((op & 0x30) == 0x30) HL--;
    }
    else if (op == 0x08) {
        OPCODE("LD [imm16], sp");
        uint16_t addr = read(PC);
        addr |= read(PC + 1) << 8;
        PC += 2;
        write(addr, SP & 0xFF);
        write(addr + 1, SP >> 8);
    }
    else if (op == 0xC9) {
        OPCODE("RET");
        PC = read(SP++);
        PC |= read(SP++) << 8;
    }
    else if ((op & 0xE7) == 0xC0) {
        OPCODE("RET cond");
        uint8_t cond = (op & 0x18) >> 3;
        if( ((cond == 0) && !Fz) || // nz
            ((cond == 1) && Fz)  || // z
            ((cond == 2) && !Fc) || // nc
            ((cond == 3) && Fc))    // c
        {
            PC = read(SP++);
            PC |= read(SP++) << 8;
            cycles += 12;
        }
    }
    else if (op == 0xD9) {
        OPCODE("RETI");
        PC = read(SP++);
        PC |= read(SP++) << 8;
        ime = true;
        log_v_printf("IME set to true\n");
    }
    else if ((op & 0xE7) == 0xC2) {
        OPCODE("JP cond, imm16");
        uint8_t cond = (op & 0x18) >> 3;
        uint16_t dst = read(PC++);
        dst |= read(PC++) << 8;
        if( ((cond == 0) && !Fz) || // nz
            ((cond == 1) && Fz)  || // z
            ((cond == 2) && !Fc) || // nc
            ((cond == 3) && Fc))    // c
        {
            PC = dst;
            cycles += 4;
        }
    }
    else if (op == 0xC3) {
        OPCODE("JP imm16");
        uint16_t dst = read(PC++);
        dst |= read(PC++) << 8;
        PC = dst;
    }
    else if (op == 0xE9) {
        OPCODE("JP (hl)");
        PC = HL;
    }
    else if ((op & 0xE7) == 0xC4) {
        OPCODE("CALL cond, imm16");
        uint8_t cond = (op & 0x18) >> 3;
        uint16_t dst = read(PC++);
        dst |= read(PC++) << 8;
        if( ((cond == 0) && !Fz) || // nz
            ((cond == 1) && Fz)  || // z
            ((cond == 2) && !Fc) || // nc
            ((cond == 3) && Fc))    // c
        {
            write(--SP, PC >> 8);
            write(--SP, PC & 0xFF);
            PC = dst;
            cycles += 12;
        }
    }
    else if (op == 0xCD) {
        OPCODE("CALL imm16");
        uint16_t dst = read(PC++);
        dst |= read(PC++) << 8;
        write(--SP, PC >> 8);
        write(--SP, PC & 0xFF);
        PC = dst;
    }
    else if ((op & 0xC7) == 0xC7) {
        OPCODE("RST n");
        uint16_t dst = (op & 0x38);
        write(--SP, PC >> 8);
        write(--SP, PC & 0xFF);
        PC = dst;
    }
    else if ((op & 0xCF) == 0xC1) {
        OPCODE("POP r16");
        uint16_t* r = R16stk[(op>>4)&0x3];
        *r = read(SP++);
        *r |= read(SP++) << 8;
        if(r == &AF) {
            F &= 0xF0;
        }
    }
    else if ((op & 0xCF) == 0xC5) {
        OPCODE("PUSH r16");
        uint16_t* r = R16stk[(op>>4)&0x3];
        write(--SP, *r >> 8);
        write(--SP, *r & 0xFF);
    }
    else if (op == 0x18) {
        OPCODE("JR");
        uint8_t off = read(PC++);
        PC += (int8_t)off;
    }
    else if ((op & 0xE7) == 0x20) {
        OPCODE("JR cond");
        uint8_t cond = (op & 0x18) >> 3;
        uint8_t off = read(PC++);
        if( ((cond == 0) && !Fz) || // nz
            ((cond == 1) && Fz)  || // z
            ((cond == 2) && !Fc) || // nc
            ((cond == 3) && Fc))    // c
        {
            PC += (int8_t)off;
            cycles += 4;
        }
    }
    else if ((op&0xC7) == 0x06) {
        OPCODE("LD r8, imm8");
        uint8_t *reg = R8[op >> 3];
        if(reg)
            *reg = read(PC++);
        else
            write(HL, read(PC++));
    }
    else if ((op&0xC0) == 0x40) {
        OPCODE("LD r8, r8");
        if(op == 0x76) {
            OPCODE("HALT");
            log_v_printf("HALT encountered at PC=%04x\n", PC - 1);
            bool ipending = (map[0xFF0F] & map[0xFFFF] & 0x1F) != 0;
            if(!ime && ipending) {
                log_v_printf("HALT bug triggered! PC set back to %04x\n", PC);
            }
            else {
                // Normal HALT
                log_v_printf("CPU halted.\n");
                halted = true;
            }
        }
        uint8_t *dst = R8[(op>>3)&0x7];
        uint8_t *src = R8[op&0x7];
        uint8_t v = src ? *src : read(HL);
        if(dst)
            *dst = v;
        else
            write(HL, v);
    }
    else if (op == 0xE2) {
        OPCODE("LDH [c], a");
        write(0xFF00 + C, A);
    }    
    else if (op == 0xE0) {
        OPCODE("LDH [imm8], a");
        write(0xFF00 + read(PC), A);
        PC++;
    }    
    else if (op == 0xEA) {
        OPCODE("LD [imm16], a");
        write(read(PC) + (read(PC + 1) << 8), A);
        PC += 2;
    }    
    else if (op == 0xE8) {
        OPCODE("ADD sp, imm8");
        int8_t imm = (int8_t)read(PC++);
        uint16_t newSP = SP + imm;
        F = (((SP ^ imm ^ newSP) & 0x10) ? Fh_mask : 0) | (((SP ^ imm ^ newSP) & 0x100) ? Fc_mask : 0);
        SP = newSP;
    }    
    else if (op == 0xF2) {
        OPCODE("LDH a, [c]");
        A = read(0xFF00 + C);
    }    
    else if (op == 0xF0) {
        OPCODE("LDH a, [imm8]");
        A = read(0xFF00 + read(PC));
        PC++;
    }    
    else if (op == 0xF8) {
        OPCODE("LD hl, sp + imm8");
        int8_t imm = (int8_t)read(PC++);
        uint16_t newHL = SP + imm;
        F = (((SP ^ imm ^ newHL) & 0x10) ? Fh_mask : 0) | (((SP ^ imm ^ newHL) & 0x100) ? Fc_mask : 0);
        HL = newHL;
    }    
    else if (op == 0xF9) {
        OPCODE("LD sp, hl");
        SP = HL;
    }    
    else if (op == 0xFA) {
        OPCODE("LD a, [imm16]");
        A = read(read(PC) + (read(PC + 1) << 8));
        PC += 2;
    }    
    else if ((op & 0xCF) == 0x3) {
        OPCODE("INC r16");
        uint16_t *reg = R16[op>>4];
        ++*reg;
    }
    else if ((op & 0xCF) == 0xB) {
        OPCODE("DEC r16");
        uint16_t *reg = R16[op>>4];
        --*reg;
    }
    else if ((op & 0xCF) == 0x9) {
        OPCODE("ADD hl, r16");
        uint16_t *reg = R16[op>>4];
        uint32_t r = *reg + HL;
        F = (Fz?Fz_mask:0) | 0 | ((r&0xFFFF0000)?Fc_mask:0) | (((r ^ HL ^ *reg) & 0x1000) ? Fh_mask : 0);
        HL = r;
    }
    else if ((op & 0xC7) == 0x4) {
        OPCODE("INC r8");
        uint8_t *reg = R8[op>>3];
        uint8_t v = reg ? *reg : read(HL);
        v+=1;
        F = (v == 0 ? 0x80 : 0) | ((v & 0xF) == 0 ? 0x20 : 0) | (F & Fc_mask);
        if(reg)
            *reg = v;
        else
            write(HL, v);
    }
    else if ((op & 0xC7) == 0x5) {
        OPCODE("DEC r8");
        uint8_t *reg = R8[op>>3];
        uint8_t v = reg ? *reg : read(HL);
        v-=1;
        F = (v == 0 ? Fz_mask : 0) | Fn_mask | ((v & 0xF) == 0xF ? Fh_mask : 0) | (F & Fc_mask);
        if(reg)
            *reg = v;
        else
            write(HL, v);
    }

    else if (op == 0xCB) {
        op = read(PC);
        PC++;
        cycles = 8; // Base cycles for CB ops
        // Extra cycles for (HL) ops
        if( (op&0x7) == 0x6) {
            cycles = (op & 0xC0) == 0x40 ? 12 : 16; // 0x40-0x7F : 12 cycles, others 16 cycles
        }
        uint8_t top = op >> 6;
        if(top) {
            uint8_t bitindex = (op & 0x38) >> 3;
            uint8_t *reg = R8[op&0x7];
            uint8_t v = reg ? *reg : read(HL);
            if(top == 1) {
                OPCODE("BIT");
                F = (((1<<bitindex)&v) ? 0 : Fz_mask) | 0 | 0x20 | (F&Fc_mask);
            }
            else if (top == 2) {
                OPCODE("RES");
                v &= ~(1<<bitindex);
                if (reg)
                    *reg = v;
                else
                    write(HL, v);
            }
            else if (top == 3) {
                OPCODE("SET");
                v |= (1<<bitindex);
                if (reg)
                    *reg = v;
                else
                    write(HL, v);
            }
        }
        else {
            uint8_t* reg = R8[op&0x7];
            uint8_t v = reg ? *reg : read(HL);
            uint8_t top = op >> 3;
            uint8_t r = 0;
            if(top == 0x0) {
                OPCODE("RLC r8");
                r = (v << 1) | (v >> 7);
                F = (r == 0 ? Fz_mask : 0) | (v & 0x80 ? Fc_mask : 0);
            }
            else if (top == 0x1) {
                OPCODE("RRC r8");
                r = (v >> 1) | (v << 7);
                F = (r == 0 ? Fz_mask : 0) | (v & 1 ? Fc_mask : 0);
            }
            else if (top == 0x2) {
                OPCODE("RL r8");
                r = (v << 1) | Fc;
                F = (r == 0 ? Fz_mask : 0) | (v & 0x80 ? Fc_mask : 0);
            }
            else if (top == 0x3) {
                OPCODE("RR r8");
                r = (v >> 1) | (Fc << 7);
                F = (r == 0 ? Fz_mask : 0) | (v & 1 ? Fc_mask : 0);
            }
            else if (top == 0x4) {
                OPCODE("SLA r8");
                r = (v << 1);
                F = (r == 0 ? Fz_mask : 0) | (v & 0x80 ? Fc_mask : 0);
            }
            else if (top == 0x5) {
                OPCODE("SRA r8");
                r = (v >> 1) | (v & 0x80);
                F = (r == 0 ? Fz_mask : 0) | (v & 1 ? Fc_mask : 0);
            }
            else if (top == 0x6) {
                OPCODE("SWAP r8");
                r = (v >> 4) | (v << 4);
                F = (r == 0 ? Fz_mask : 0);
            }
            else if (top == 0x7) {
                OPCODE("SRL r8");
                r = v >> 1;
                F = (r == 0 ? Fz_mask : 0) | (v & 1 ? Fc_mask : 0);
            }
            if (reg)
                *reg = r;
            else
                write(HL, r);
        }
    }
    else if ((op & 0xC0) == 0x80 || ((op & 0xC7) == 0xC6)) {
        // ADD/ADC/SUB/SBC/AND/XOR/OR/CP
        uint8_t *reg = R8[op&0x7];

        // If reg is null we either have [HL] case or imm8 case
        uint8_t v = 0;
        if(reg) v=*reg; 
        else if (op & 0x40) v = read(PC++);
        else v = read(HL);

        uint32_t o = (op & 0x38) >> 3;
        uint32_t r = 0;
        if(o == 0x0) {
            OPCODE("ADD");
            r = A + v;
            //  Z                      N   H                         C
            F = ((r & 0xFF) == 0 ? 0x80 : 0) | 0 | (((A ^ v ^ r) & 0x10) << 1) | ((r & 0xFF00) ? 0x10 : 0);
            A = r & 0xFF;
        }
        else if (o == 0x1) {
            OPCODE("ADC");
            r = A + v + Fc;
            F = ((r & 0xFF) == 0 ? 0x80 : 0) | 0 | (((A ^ v ^ r) & 0x10) << 1) | ((r & 0xFF00) ? 0x10 : 0);
            A = r & 0xFF;
        }
        else if (o == 0x2) {
            OPCODE("SUB");
            r = A - v;
            F = ((r & 0xFF) == 0 ? 0x80 : 0) | Fn_mask | (((A ^ v ^ r) & 0x10) << 1) | ((r & 0xFF00) ? 0x10 : 0);
            A = r & 0xFF;
        }
        else if (o == 0x3) {
            OPCODE("SBC");
            r = A - v - Fc;
            F = ((r & 0xFF) == 0 ? 0x80 : 0) | Fn_mask | (((A ^ v ^ r) & 0x10) << 1) | ((r & 0xFF00) ? 0x10 : 0);
            A = r & 0xFF;
        }
        else if (o == 0x4) {
            OPCODE("AND");
            A = A & v;
            F = (A == 0 ? 0x80 : 0) | 0 | 0x20 | 0;
        }
        else if (o == 0x5) {
            OPCODE("XOR");
            A = A ^ v;
            F = (A == 0 ? 0x80 : 0) | 0 | 0 | 0;
        }
        else if (o == 0x6) {
            OPCODE("OR");
            A = A | v;
            F = (A == 0 ? 0x80 : 0) | 0 | 0 | 0;
        }
        else if (o == 0x7) {
            OPCODE("CP");
            r = A - v;
            F = ((r & 0xFF) == 0 ? 0x80 : 0) | Fn_mask | (((A ^ v ^ r) & 0x10) << 1) | ((r & 0xFF00) ? 0x10 : 0);
        }
    }
    else if (op == 0x07) {
        OPCODE("RLCA");
        A = (A << 1) | (A >> 7);
        F = A & 1 ? Fc_mask : 0;
    }
    else if (op == 0x0F) {
        OPCODE("RRCA");
        A = (A >> 1) | (A << 7);
        F = A & 0x80 ? Fc_mask : 0;
    }
    else if (op == 0x17) {
        OPCODE("RLA");
        uint8_t r = (A << 1) | Fc;
        F = A & 0x80 ? Fc_mask : 0;
        A = r;
    }
    else if (op == 0x1F) {
        OPCODE("RRA");
        uint8_t r = (A >> 1) | (Fc << 7);
        F = A & 1 ? Fc_mask : 0;
        A = r;
    }
    else if (op == 0x27) {
        OPCODE("DAA");
        uint8_t adj = 0;
        if(Fn) {
            if(Fh) adj+=0x6;
            if(Fc) adj+=0x60;
            A-=adj;
        }
        else {
            if(Fh || (A&0xF) > 0x9) adj+=0x6;
            if(Fc || A>0x99) adj+=0x60, F|=Fc_mask;
            A += adj;
        }
        F &= ~(Fz_mask | Fh_mask);
        F |= (A == 0 ? Fz_mask : 0);
    }
    else if (op == 0x2F) {
        OPCODE("CPL");
        A = ~A;
        F |= Fn_mask | Fh_mask;
    }
    else if (op == 0x37) {
        OPCODE("SCF");
        F &= ~ (Fn_mask | Fh_mask);
        F |= Fc_mask;
    }
    else if (op == 0x3F) {
        OPCODE("CCF");
        F &= ~ (Fn_mask | Fh_mask);
        F ^= Fc_mask;
    }
    else if (op == 0xF3) {
        OPCODE("DI");
        ime = false;
        log_v_printf("IME set to false\n");
    }
    else if (op == 0xFB) {
        OPCODE("EI");
        ime_true_pending = 1;
    }
    else if (op == 0x10) {
        OPCODE("STOP");
        REG_DIV = 0;
        //halted = true;
    }
    else {
        printf("Invalid opcode %02x\n", op);
        dump_regs();
        exit(1);
    }
    return cycles;
}

void load_rom(uint8_t* dst, uint32_t size, const char* filename)
{
    FILE* f = fopen(filename, "rb");
    if(!f) {
        printf("Failed to load rom from %s\n", filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long filesize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(filesize > size) {
        printf("Rom too big (%ld bytes)\n", filesize);
        exit(1);
    }
    fread(dst, filesize, 1, f);
    fclose(f);
}

static inline uint32_t get_tile_pixel(int tilex, int subtilex, int tiley, int subtiley, uint8_t* tilemap, uint8_t palette)
{
    uint8_t tileidx = tilemap[tilex + tiley * 32];
    uint8_t *tiledata = (REG_LCDC & 0x10) ? (map + 0x8000 + tileidx * 16) : (map + 0x9000 + ((int8_t)tileidx) * 16);
    tiledata += subtiley * 2;
    uint8_t mask = 0x80 >> subtilex;
    int paletteidx = (*tiledata & mask ? 1 : 0) + (*(tiledata + 1) & mask ? 2 : 0);
    return palette_colors[(palette >> (paletteidx * 2)) & 0x3];
}

int main(int argc, char* argv[]) {
    char* rom_file = NULL;
    char* boot_rom_file = NULL;

    // Parse command line arguments
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            CYCLES_PR_FRAME = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            boot_rom_file = argv[++i];
        } else if(strcmp(argv[i], "-v") == 0) {
            verbose_logging = true;
        } else if(strcmp(argv[i], "-br") == 0 && i + 1 < argc) {
            break_at = (uint16_t)strtol(argv[++i], NULL, 16);
            printf("Breakpoint set at %04x\n", break_at);
        } else {
            rom_file = argv[i];
        }
    }

    uint64_t timer_freq = SDL_GetPerformanceFrequency();

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL audio initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "Ges emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        //160*SCREENSCALE, 144*SCREENSCALE,
        240 * SCREENSCALE, 320 * SCREENSCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS
    );

    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Setup audio
    SDL_AudioSpec spec = {}, obtained = {};
    spec.freq = 48000;
    spec.format = AUDIO_F32SYS;  
    spec.channels = 2;
    spec.samples = 512;
    spec.callback = audio_callback;
    spec.userdata = NULL;
    SDL_AudioDeviceID audio_device;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &spec, &obtained, 0);
    if (audio_device < 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
        return 1;
    }
    SDL_PauseAudioDevice(audio_device, 0); // Start audio playback

    printf("Ges emulator\n");
    printf("Press ESC to quit\n");

    // Rom loading
    memset(rom, 0xff, sizeof(rom));
    if(rom_file) {
        printf("Loading rom %s\n", rom_file);
        load_rom(rom, sizeof(rom), rom_file);
        mbc_type_id = rom[0x147];
        mbc_rom_size_info = rom[0x148];
        mbc_ram_size_info = rom[0x149];
        static const uint8_t mbc_type_table[] = {1,1,1,1, 2,2, 0,0,0,0, 3,3,3,3,3, 4,4,4, 5,5,5,5,5,5, 0,0,0,0,0};
        static const uint8_t mbc_ram_banks_table[] = {0, 0, 1, 4, 16, 8}; // Number of 8KB RAM banks
        mbc_ram_banks = mbc_ram_banks_table[mbc_ram_size_info];
        mbc_rom_banks = 2 << mbc_rom_size_info;
        mbc_type = mbc_type_table[mbc_type_id];
        printf("MBC type id: %02x\n", mbc_type_id);
        printf("MBC type: %02x\n", mbc_type);
        printf("MBC rom size: %02x (%02x banks)\n", mbc_rom_size_info, mbc_rom_banks);
        printf("MBC ram size: %02x (%02x banks)\n", mbc_ram_banks * 1024 * 8, mbc_ram_banks);
    }
    if(boot_rom_file) {
        printf("Loading boot-rom %s\n", boot_rom_file);
        load_rom(boot_rom, sizeof(boot_rom), boot_rom_file);
    }

    cpu_boot();
    if(!boot_rom_file)
        post_boot_teleport(); 

    bool running = true;

    int64_t frame_target = 0;
    uint64_t target_duration = timer_freq / 59.7275f;
    while (running) {

        uint64_t frame_start = SDL_GetPerformanceCounter();

        int cycles_left  = CYCLES_PR_FRAME;

        // Keyboard handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_KEYDOWN) {
                switch(event.key.keysym.sym) {
                    case SDLK_RIGHT: dpad_state |= 0x01; break;
                    case SDLK_LEFT:  dpad_state |= 0x02; break;
                    case SDLK_UP:    dpad_state |= 0x04; break;
                    case SDLK_DOWN:  dpad_state |= 0x08; break;
                    case SDLK_z:     keys_state |= 0x01; break; // A
                    case SDLK_x:     keys_state |= 0x02; break; // B
                    case SDLK_RETURN:keys_state |= 0x04; break; // Start
                    case SDLK_RSHIFT:keys_state |= 0x08; break; // Select
                }
            }
            if (event.type == SDL_KEYUP) {
                switch(event.key.keysym.sym) {
                    case SDLK_RIGHT: dpad_state &= ~0x01; break;
                    case SDLK_LEFT:  dpad_state &= ~0x02; break;
                    case SDLK_UP:    dpad_state &= ~0x04; break;
                    case SDLK_DOWN:  dpad_state &= ~0x08; break;
                    case SDLK_z:     keys_state &= ~0x01; break;
                    case SDLK_x:     keys_state &= ~0x02; break;
                    case SDLK_RETURN:keys_state &= ~0x04; break;
                    case SDLK_RSHIFT:keys_state &= ~0x08; break;
                }
            }   

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_0)
                show_profile_bar = true;
            else if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_0)
                show_profile_bar = false;
            
            if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_9)
                upscale = !upscale;
        }

        // Run cpu
        while (cycles_left > 0)
        {
            int cycles = cpu_tick();

            // Update timers
            bool apu_tick = false;
            sys_counter += cycles;
            uint8_t new_div = sys_counter >> 8;
            if ((REG_DIV & 0x10) && !(new_div & 0x10)) {
                // 512hz tick
                div_apu++;
                apu_tick = true;
            }
            REG_DIV = new_div;

            static uint16_t tima_clock_cycles_table[] = {0x400, 0x10, 0x40, 0x100};
            if ((REG_TAC & 0x4)) {
                tima_timer_cycles += cycles;
                uint16_t cycles = tima_clock_cycles_table[REG_TAC & 0x3];
                while(tima_timer_cycles >= cycles) {
                    tima_timer_cycles -= cycles;
                    // TIMA clock tick
                    REG_TIMA++;
                    if(REG_TIMA == 0) {
                        // Overflow
                        REG_TIMA = REG_TMA;
                        REG_IF |= 0x4;
                    }
                }
            }

            // Update serial timer
            serial_timer += cycles;
            if(serial_timer >= 512) {
                serial_timer -= 512;
                if(REG_SC & 0x80) {
                    // TODO transfer data
                    //REG_SC &= ~0x80; // clear transfer flag
                    //REG_IF |= 0x8;   // request serial interrupt
                }
            }




            // Update sound timers
            if(apu_tick) {

                if(!(REG_NR52 & 0x80)) {
                    // Sound disabled
                    continue;
                }

                if(REG_NR52 & 0x1) {
                    // Channel 1 length timer
                    if(sound_ch1_length_enable && (div_apu & 0x1) == 0) {
                        sound_ch1_length_timer++;
                        if(sound_ch1_length_timer == 0x40) {
                            REG_NR52 &= ~0x1; // disable chan 1
                        }
                    }

                    // Channel 1 envelope timer
                    uint8_t ch1_sweep_pace = REG_NR12 & 0x7;
                    // sweep_pace is at apu/8 = 64hz steps
                    if(ch1_sweep_pace > 0 && ((div_apu&0x7) == 0) && (++sound_ch1_envelope_timer == ch1_sweep_pace))
                    {
                        sound_ch1_envelope_timer = 0;
                        if(REG_NR12 & 0x8 && sound_ch1_volume < 15) {
                            sound_ch1_volume++;
                        } else if (sound_ch1_volume > 0) {
                            sound_ch1_volume--;
                        }
                    }

                    // Channel 1 frequency sweep timer
                    if(sound_ch1_frq_sweep_enabled) {
                        sound_ch1_frq_sweep_timer++;
                        if(sound_ch1_frq_sweep_timer >= ((REG_NR10 & 0x70) >> 2)) {
                            sound_ch1_frq_sweep_timer = 0;
                            uint16_t divider_change = sound_ch1_period_divider >> ((REG_NR10&0x7));
                            if(REG_NR10 & 0x8) {
                                sound_ch1_period_divider -= divider_change;
                            } else {
                                sound_ch1_period_divider += divider_change;
                                if(sound_ch1_period_divider > 0x7FF) {
                                    sound_ch1_period_divider = 0x7FF;
                                    REG_NR52 &= ~0x1; // disable chan 1
                                }
                            }
                            REG_NR13 = sound_ch1_period_divider & 0xFF;
                            REG_NR14 = (REG_NR14 & 0xF8) | ((sound_ch1_period_divider >> 8) & 0x7);
                        }
                    }
                }

                if(REG_NR52 & 0x2) {

                    // Channel 2 length timer
                    if(sound_ch2_length_enable && (div_apu & 0x1) == 0) {
                        sound_ch2_length_timer++;
                        if(sound_ch2_length_timer == 0x40) {
                            REG_NR52 &= ~0x2; // disable chan 2
                        }
                    }

                    // Channel 2 envelope timer
                    uint8_t ch2_sweep_pace = REG_NR22 & 0x7;
                    // sweep_pace is at apu/8 = 64hz steps
                    if(ch2_sweep_pace > 0 && ((div_apu&0x7) == 0) && (++sound_ch2_envelope_timer == ch2_sweep_pace))
                    {
                        sound_ch2_envelope_timer = 0;
                        if(REG_NR22 & 0x8 && sound_ch2_volume < 15) {
                            sound_ch2_volume++;
                        } else if (sound_ch2_volume > 0) {
                            sound_ch2_volume--;
                        }
                    }
                }

                if(REG_NR52 & 0x4) {
                    // Channel 3 length timer
                    if(sound_ch3_length_enable && (div_apu & 0x1) == 0) {
                        sound_ch3_length_timer++;
                        if(sound_ch3_length_timer == 0) {
                            REG_NR52 &= ~0x4;
                        }
                    }
                }

                if(REG_NR52 & 0x8) {
                    // Channel 4 length timer
                    if(sound_ch4_length_enable && (div_apu & 0x1) == 0) {
                        sound_ch4_length_timer++;
                        if(sound_ch4_length_timer == 0x40) {
                            REG_NR52 &= ~0x8; // disable chan 4
                        }
                    }   

                    // Channel 4 envelope timer
                    uint8_t ch4_sweep_pace = REG_NR42 & 0x7;
                    // sweep_pace is at apu/8 = 64hz steps
                    if(ch4_sweep_pace > 0 && ((div_apu&0x7) == 0) && (++sound_ch4_envelope_timer == ch4_sweep_pace)) {
                        sound_ch4_envelope_timer = 0;
                        if(REG_NR42 & 0x8 && sound_ch4_volume < 15) {
                            sound_ch4_volume++;
                        } else if (sound_ch4_volume > 0) {
                            sound_ch4_volume--;
                        }
                    }
                }

            }

            // LCD logic that needs to happen when a scanline is completed
            lcd_scanline_cycles += cycles; 
            bool draw_scanline = false;
            if(lcd_scanline_cycles >= LCD_CYCLES_PER_SCANLINE) {

                lcd_scanline_cycles -= LCD_CYCLES_PER_SCANLINE;

                // Start a new scanline
                REG_LY++;
                if(REG_LY > LCD_SCANLINES) {
                    REG_LY=0;
                    lcd_window_line = 0;
                }

                // Update stat for LYC match and trigger LYC=LY interrupt if enabled
                if(REG_LYC == REG_LY) {
                    REG_STAT |= 4;
                    // Fire interrupt if enabled
                    if (REG_STAT & 0x40) {
                        REG_IF |= 0x2;
                    }
                } else {
                    REG_STAT &= ~4;
                }

                // Trigger interrupt if entering vblank
                if(REG_LY == LCD_HEIGHT) {
                    // Always request VBlank interrupt
                    REG_IF |= 0x1; // Request VBlank interrupt
                    // If STAT mode 1 (vblank) interrupt enabled, request LCD STAT interrupt
                    if (REG_STAT & 0x10) {
                        REG_IF |= 0x2;
                    }
                }
                if (REG_LY >= LCD_HEIGHT) {
                    // VBlank lines
                    REG_STAT = (REG_STAT & ~3) | 1;
                }
                else {
                    // Visible scanlines all start in mode 2
                    REG_STAT = (REG_STAT & ~3) | 2;
                    // If stat mode 2 interrupt enabled, request LCD STAT interrupt
                    if(REG_STAT & 0x20) {
                        REG_IF |= 0x2;
                    }
                }
            }

            // Handle LCD mode transition during scanline (only regular lines)
            if(REG_LY < LCD_HEIGHT) {
                if(lcd_scanline_cycles >= 80 && (REG_STAT & 3) == 2) {
                    // Switch to mode 3
                    REG_STAT = (REG_STAT & ~3) | 3;
                    draw_scanline = true; 
                }
                else if(lcd_scanline_cycles >= 80 + 172 && (REG_STAT & 3) == 3) {
                    // Switch to mode 0
                    REG_STAT = (REG_STAT & ~3) | 0;
                    // If stat mode 0 interrupt enabled, request LCD STAT interrupt
                    if (REG_STAT & 0x8) {
                        REG_IF |= 0x2;
                    }
                }
            }
            if(draw_scanline) {

                // Copy to screen from memory for this scanline
                if(REG_LCDC & 0x1) // BG enabled
                {
                    uint8_t* tilemap = (REG_LCDC & 0x8) ? (map + 0x9C00) : (map + 0x9800);
                    for(int x = 0; x < 160; ++x)
                    {
                        uint8_t vx = x + REG_SCX, vy = REG_LY + REG_SCY;
                        screen[x + REG_LY * 160] = get_tile_pixel(vx >> 3, vx & 0x7, vy >> 3, vy & 0x7, tilemap, REG_BGP);
                    }
                }

                // Draw window if enabled, in front of bg and overlaps this scanline
                if((REG_LCDC & 0x20) && (REG_LCDC & 0x1) && (REG_WY <= REG_LY) && (REG_WX <= 166)) {
                    uint8_t* tilemap = (REG_LCDC & 0x40) ? (map + 0x9C00) : (map + 0x9800);
                    for(int win_x = 0; win_x < 160; ++win_x)
                    {
                        if(win_x + REG_WX < 7) continue; // before screen
                        int x = win_x + REG_WX - 7;
                        if(x >= 160) break;
                        screen[x + REG_LY * 160] = get_tile_pixel(win_x >> 3, win_x & 0x7, lcd_window_line >> 3, lcd_window_line & 0x7, tilemap, REG_BGP);
                    }
                    lcd_window_line++;
                }

                // Draw sprites if enabled
                if(REG_LCDC & 0x2) {

                    uint8_t sprite_height = (REG_LCDC & 0x4) ? 16 : 8;
                    // Find (up to 10) sprites on this scanline
                    uint16_t sprites_on_line[10] = {}; // upper byte = x position, lower byte = sprite index
                    uint8_t* oam = map + 0xFE00;
                    int spritecount = 0;
                    for(int i = 0; i < 40; ++i)
                    {
                        uint8_t sprite_y = *oam++; // Y position on screen + 16
                        uint8_t sprite_x = *oam++; // X position on screen + 8
                        oam+=2;
                        if(sprite_y + sprite_height <= 16 || sprite_y >= 160) {
                            // Not visible
                            continue;
                        }
                        if(REG_LY + 16 >= sprite_y && REG_LY + 16 < sprite_y + sprite_height) {
                            uint16_t sprite_to_add = (sprite_x << 8) | i;
                            int place = spritecount;
                            while(place > 0 && sprites_on_line[place - 1] <= sprite_to_add) {
                                sprites_on_line[place] = sprites_on_line[place - 1];
                                place--;
                            }
                            sprites_on_line[place] = sprite_to_add;
                            spritecount++;
                            if(spritecount == 10)
                                break;
                        }
                    }
                    // Now draw spritecount sprites on scanline
                    for(int s = 0; s < spritecount; ++s) {

                        uint8_t sprite_x = sprites_on_line[s] >> 8; // X position on screen + 8
                        uint8_t sprite_index = sprites_on_line[s] & 0xFF;
                        uint8_t* oam_entry = map + 0xFE00 + sprite_index * 4;
                        uint8_t sprite_y = *(oam_entry);     // Y position on screen + 16
                        uint8_t sprite_tile = *(oam_entry + 2); // Tile index
                        if(sprite_height == 16)
                            sprite_tile &= 0xFE; // force even tile number for 8x16 sprites
                        uint8_t sprite_attr = *(oam_entry+3); // Attributes
                        bool flip_x = (sprite_attr & 0x20) != 0;
                        bool flip_y = (sprite_attr & 0x40) != 0;
                        bool behind_bg = (sprite_attr & 0x80) != 0;
                        uint8_t palette = (sprite_attr & 0x10) ? REG_OBP1 : REG_OBP0;

                        int line_in_sprite = REG_LY + 16 - sprite_y;
                        if(flip_y)
                            line_in_sprite = (sprite_height - 1) - line_in_sprite;
                        uint8_t* tiledata = map + 0x8000 + sprite_tile * 16 + line_in_sprite * 2;

                        for(int xpix = 0; xpix < 8; ++xpix)
                        {
                            int screen_x = sprite_x + xpix - 8;
                            if(screen_x < 0 || screen_x >= 160)
                                continue;
                            int pixel_x_in_sprite = flip_x ? (7 - xpix) : xpix;
                            uint8_t mask = 0x80 >> pixel_x_in_sprite;
                            int paletteidx = (*tiledata) & mask ? 1 : 0;
                            paletteidx += (*(tiledata+1) & mask) ? 2 : 0;
                            if(paletteidx == 0)
                                continue; // Transparent pixel

                            // Get color from palette
                            int color = (palette >> (paletteidx * 2)) & 0x3;
                            // If behind bg and bg pixel not color 0, skip drawing
                            if(behind_bg) {
                                uint32_t bg_pixel = screen[screen_x + REG_LY * 160];
                                if(bg_pixel != 0x00000000)
                                    continue;
                            }
                            screen[screen_x + REG_LY * 160] = palette_colors[color];
                        }
                    }
                }
            }

            cycles_left -= cycles;
        }

        uint64_t frame_mid = SDL_GetPerformanceCounter();

        // Clear screen
        uint32_t clearcol = (200<<16)+(220<<8)+200;
        SDL_SetRenderDrawColor(renderer, 200, 220, 200, 255);
        SDL_RenderClear(renderer);

        // Draw screen
        if(upscale)
        {
            // Upscale 1.5 to hit 240 width
            for(int x = 0; x < 240; ++x)
            {
                for(int y = 0; y < 216; ++y)
                {
                    uint32_t sx = x * 2 / 3;
                    uint32_t sx2 = (x * 2 + 1) / 3;
                    uint32_t sy = y * 2 / 3;
                    uint32_t sy2 = (y * 2 + 1) / 3;
                    uint32_t col = screen[sx + sy * 160]; if(!(col & 0xFF000000)) col = clearcol;
                    uint32_t col1 = screen[sx2 + sy * 160];if(!(col1 & 0xFF000000)) col1 = clearcol;
                    uint32_t col2 = screen[sx + sy2 * 160];if(!(col2 & 0xFF000000)) col2 = clearcol;
                    uint32_t col3 = screen[sx2 + sy2 * 160];if(!(col3 & 0xFF000000)) col3 = clearcol;
                    if(y == 0 && show_profile_bar) col = profile_bar[x],col1=0,col2=0,col3=0;
                    uint16_t r = ((col >> 16) & 0xff) + ((col1 >> 16) & 0xff) + ((col2 >> 16) & 0xff) + ((col3 >> 16) & 0xff);
                    uint16_t g = ((col >> 8) & 0xff) + ((col1 >> 8) & 0xff) + ((col2 >> 8) & 0xff) + ((col3 >> 8) & 0xff);
                    uint16_t b = (col & 0xff) + (col1 & 0xff) + (col2 & 0xff) + (col3 & 0xff);
                    if(col || col1 || col2 || col3)
                    {
                        SDL_SetRenderDrawColor(renderer, r >> 2, g >> 2, b >> 2, 255);
                        SDL_Rect rect = {x * SCREENSCALE, (52 + y) * SCREENSCALE, SCREENSCALE, SCREENSCALE};
                        SDL_RenderFillRect(renderer, &rect);
                    }
                }
            }
        }
        else
        {
            // No upscale, center on screen
            for(int x = 0; x < 160; ++x)
            {
                for(int y = 0; y < 144; ++y)
                {
                    uint32_t col = screen[x + y * 160];
                    if(y == 0 && show_profile_bar) col = profile_bar[x];
                    uint8_t r = (col >> 16) & 0xff;
                    uint8_t g = (col >> 8) & 0xFF;
                    uint8_t b = (col & 0xff);
                    if(col)
                    {
                        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                        SDL_Rect rect = {(x + 40) * SCREENSCALE, (y + 88) * SCREENSCALE, SCREENSCALE, SCREENSCALE};
                        SDL_RenderFillRect(renderer, &rect);
                    }
                }
            }
        }

        // Flip
        SDL_RenderPresent(renderer);

        uint64_t frame_end = SDL_GetPerformanceCounter();
        uint64_t frame_duration = frame_end - frame_start;

        frame_target += target_duration;
        frame_target -= frame_duration;

        if(frame_target > 0) {
            uint64_t ms = frame_target * 1000 / timer_freq;
            if(ms > 20) ms = 20;
            uint64_t frame_cpu = frame_mid - frame_start;
            for(int i = 0; i < 160; ++i) {
                uint64_t tick_bar = i * target_duration / 160;
                profile_bar[i] = tick_bar <= frame_cpu ? 0xFF00FF00 : tick_bar <= frame_duration ? 0xFFFF0000 : 0xFF000000;
            }
            SDL_Delay(ms);
        }

        uint64_t frame_real_end = SDL_GetPerformanceCounter();
        frame_target -= frame_real_end - frame_end;

        if (frame_target < -100000)
        {
            printf("Teleport\n");
            frame_target = 0;
        }
    }

    printf("Shutting down...\n");
    SDL_CloseAudioDevice(audio_device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
