// =====================================================================
//  chip8emu 0.1.1.1 by mrkondo 0.1  —  A.C Holdings / KONDO SOLUTIONS
//  Single-file CHIP-8 emulator, C++17 + SDL2, 600x400 window, 60 FPS.
//  mGBA / VB6 MenuStrip: File (Load ROM, Exit) · Help (Controls, Copyright, About)
//  Drag & drop ROMs, Tab fast-forward, P pause, F1/F4 state, F5 reset.
//
//  macOS 27 (Apple Silicon or Intel):
//    brew install sdl2
//    clang++ -std=c++17 -O2 chip8emu0.1.1.1bymrkondo0.1.cpp \
//      $(sdl2-config --cflags --libs) -o chip8emu
//    ./chip8emu [rom.ch8] [--legacy] [--ipf N]
//
//  Linux:
//    clang++ -std=c++17 -O2 chip8emu0.1.1.1bymrkondo0.1.cpp \
//      $(sdl2-config --cflags --libs) -o chip8emu
//
//  Keypad (left = PC key, right = CHIP-8 key):
//    1 2 3 4     1 2 3 C
//    Q W E R  -> 4 5 6 D
//    A S D F     7 8 9 E
//    Z X C V     A 0 B F
//  No ROM given? A built-in bounce demo runs.
// =====================================================================
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

static const char* APP_NAME = "chip8emu 0.1.1.1 by mrkondo 0.1";
static const char* APP_COPY =
    "[C] AC KONDO / mrkondo 1999-2026\n"
    "A.C Holdings / KONDO SOLUTIONS\n"
    "chip8emu 0.1.1.1 by mrkondo 0.1";
static const char* APP_ABOUT =
    "chip8emu 0.1.1.1 by mrkondo 0.1\n"
    "Single-file CHIP-8 · C++17 + SDL2\n"
    "mGBA-style frontend · 600x400 · 60 FPS\n"
    "A.C Holdings / KONDO SOLUTIONS\n"
    "[C] AC KONDO 1999-2026";
static const char* APP_CONTROLS =
    "CHIP-8 keypad:\n"
    "  1 2 3 4   ->  1 2 3 C\n"
    "  Q W E R   ->  4 5 6 D\n"
    "  A S D F   ->  7 8 9 E\n"
    "  Z X C V   ->  A 0 B F\n"
    "\n"
    "Emulator:\n"
    "  P pause · Tab fast-forward · Esc exit\n"
    "  F1 save state · F4 load state · F5 reset\n"
    "  +/- change IPF · Ctrl+O Load ROM\n"
    "  Drag & drop a .ch8 onto the window";
static const char* APP_HELP =
    "MenuStrip (VB6 / mGBA style):\n"
    "  File > Load ROM...  open a CHIP-8 ROM\n"
    "  File > Exit         quit the emulator\n"
    "  Help > Controls     keypad & hotkeys\n"
    "  Help > Copyright    copyright notice\n"
    "  Help > About        version / credits\n"
    "\n"
    "Drop a ROM on the window to load it.";
static const int WIN_W = 600, WIN_H = 400;
static const int CH8_W = 64, CH8_H = 32;
static const int SCALE = 9;                        // 576 x 288
static const int MENU_H = 22;                      // VB6 / mGBA MenuStrip height
static const int SCR_X = (WIN_W - CH8_W * SCALE) / 2;  // 12
static const int SCR_Y = 48;                       // below MenuStrip + LEDs

// ---------------------------------------------------------------- core
struct Quirks {
    bool shiftUsesVy   = false; // 8XY6/8XYE: original COSMAC shifts Vy
    bool loadStoreIncI = false; // FX55/FX65: original increments I
    bool jumpUsesVx    = false; // BNNN: SUPER-CHIP uses BXNN + Vx
    bool vfReset       = false; // 8XY1/2/3: original resets VF
    bool clipSprites   = true;  // clip at screen edge (vs wrap)
};

static const uint8_t FONT[80] = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
    0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
    0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80};
static const uint16_t FONT_ADDR = 0x50;

// Built-in demo: a "8" glyph bouncing around the screen.
static const uint8_t DEMO_ROM[] = {
    0x60,0x00, 0x61,0x00, 0x62,0x01, 0x63,0x01, 0x64,0x08, 0xF4,0x29, // init
    0x00,0xE0, 0xD0,0x15,                                             // 20C cls/draw
    0x65,0x02, 0xF5,0x15,                                             // DT = 2
    0xF5,0x07, 0x35,0x00, 0x12,0x14,                                  // wait DT
    0x80,0x24, 0x81,0x34,                                             // move
    0x40,0x3B, 0x62,0xFF, 0x40,0x00, 0x62,0x01,                       // x bounce
    0x41,0x1B, 0x63,0xFF, 0x41,0x00, 0x63,0x01,                       // y bounce
    0x12,0x0C};                                                       // loop

struct Chip8 {
    uint8_t  mem[4096];
    uint8_t  V[16];
    uint16_t I, pc;
    uint16_t stack[16];
    uint8_t  sp;
    uint8_t  dt, st;
    uint8_t  disp[CH8_W * CH8_H];
    bool     keys[16];
    int      heldKey;  // for FX0A press-then-release
    bool     halted;
    std::mt19937 rng;
    Quirks   q;

    void reset() {
        std::memset(mem, 0, sizeof mem);
        std::memset(V, 0, sizeof V);
        std::memset(stack, 0, sizeof stack);
        std::memset(disp, 0, sizeof disp);
        std::memset(keys, 0, sizeof keys);
        std::memcpy(mem + FONT_ADDR, FONT, sizeof FONT);
        I = 0; pc = 0x200; sp = 0; dt = st = 0;
        heldKey = -1; halted = false;
        try { rng.seed(std::random_device{}()); }
        catch (...) { rng.seed(0xC8118u); }
    }

    bool load(const std::vector<uint8_t>& rom) {
        if (rom.empty() || rom.size() > 4096 - 0x200) return false;
        reset();
        std::memcpy(mem + 0x200, rom.data(), rom.size());
        return true;
    }

    void tickTimers() { if (dt) --dt; if (st) --st; }

    void step() {
        if (halted) return;
        uint16_t op = (uint16_t)(mem[pc & 0xFFF] << 8 | mem[(pc + 1) & 0xFFF]);
        pc = (pc + 2) & 0xFFF;
        uint8_t x = (op >> 8) & 0xF, y = (op >> 4) & 0xF, n = op & 0xF;
        uint8_t nn = op & 0xFF; uint16_t nnn = op & 0xFFF;

        switch (op >> 12) {
        case 0x0:
            if (op == 0x00E0) std::memset(disp, 0, sizeof disp);
            else if (op == 0x00EE) { if (sp) pc = stack[--sp]; else halted = true; }
            break; // 0NNN (machine code) ignored
        case 0x1: pc = nnn; break;
        case 0x2: if (sp < 16) { stack[sp++] = pc; pc = nnn; } else halted = true; break;
        case 0x3: if (V[x] == nn) pc += 2; break;
        case 0x4: if (V[x] != nn) pc += 2; break;
        case 0x5: if (V[x] == V[y]) pc += 2; break;
        case 0x6: V[x] = nn; break;
        case 0x7: V[x] += nn; break;
        case 0x8: {
            uint8_t vx = V[x], vy = V[y], f;
            switch (n) {
            case 0x0: V[x] = vy; break;
            case 0x1: V[x] |= vy; if (q.vfReset) V[0xF] = 0; break;
            case 0x2: V[x] &= vy; if (q.vfReset) V[0xF] = 0; break;
            case 0x3: V[x] ^= vy; if (q.vfReset) V[0xF] = 0; break;
            case 0x4: { unsigned s = vx + vy; V[x] = (uint8_t)s; V[0xF] = s > 255; } break;
            case 0x5: f = vx >= vy; V[x] = vx - vy; V[0xF] = f; break;
            case 0x6: { uint8_t s = q.shiftUsesVy ? vy : vx; V[x] = s >> 1; V[0xF] = s & 1; } break;
            case 0x7: f = vy >= vx; V[x] = vy - vx; V[0xF] = f; break;
            case 0xE: { uint8_t s = q.shiftUsesVy ? vy : vx; V[x] = (uint8_t)(s << 1); V[0xF] = s >> 7; } break;
            }
        } break;
        case 0x9: if (V[x] != V[y]) pc += 2; break;
        case 0xA: I = nnn; break;
        case 0xB: pc = (nnn + (q.jumpUsesVx ? V[x] : V[0])) & 0xFFF; break;
        case 0xC: V[x] = (uint8_t)(rng() & 0xFF) & nn; break;
        case 0xD: {
            int sx = V[x] % CH8_W, sy = V[y] % CH8_H;
            V[0xF] = 0;
            for (int r = 0; r < n; ++r) {
                int py = sy + r;
                if (py >= CH8_H) { if (q.clipSprites) break; py %= CH8_H; }
                uint8_t bits = mem[(I + r) & 0xFFF];
                for (int c = 0; c < 8; ++c) {
                    if (!(bits & (0x80 >> c))) continue;
                    int px = sx + c;
                    if (px >= CH8_W) { if (q.clipSprites) break; px %= CH8_W; }
                    uint8_t& p = disp[py * CH8_W + px];
                    if (p) V[0xF] = 1;
                    p ^= 1;
                }
            }
        } break;
        case 0xE:
            if (nn == 0x9E && keys[V[x] & 0xF]) pc += 2;
            else if (nn == 0xA1 && !keys[V[x] & 0xF]) pc += 2;
            break;
        case 0xF:
            switch (nn) {
            case 0x07: V[x] = dt; break;
            case 0x0A:
                if (heldKey < 0) {
                    for (int k = 0; k < 16; ++k) if (keys[k]) { heldKey = k; break; }
                    pc -= 2;
                } else if (!keys[heldKey]) { V[x] = (uint8_t)heldKey; heldKey = -1; }
                else pc -= 2;
                break;
            case 0x15: dt = V[x]; break;
            case 0x18: st = V[x]; break;
            case 0x1E: I = (I + V[x]) & 0xFFF; break;
            case 0x29: I = FONT_ADDR + (V[x] & 0xF) * 5; break;
            case 0x33:
                mem[I & 0xFFF] = V[x] / 100;
                mem[(I + 1) & 0xFFF] = (V[x] / 10) % 10;
                mem[(I + 2) & 0xFFF] = V[x] % 10;
                break;
            case 0x55: for (int i = 0; i <= x; ++i) mem[(I + i) & 0xFFF] = V[i];
                       if (q.loadStoreIncI) I = (I + x + 1) & 0xFFF; break;
            case 0x65: for (int i = 0; i <= x; ++i) V[i] = mem[(I + i) & 0xFFF];
                       if (q.loadStoreIncI) I = (I + x + 1) & 0xFFF; break;
            }
            break;
        }
    }
};

// --------------------------------------------------------------- audio
static std::atomic<bool> g_beep{false};
static double g_phase = 0.0;
static void audioCallback(void*, Uint8* stream, int len) {
    int16_t* out = (int16_t*)stream;
    int samples = len / 2;
    bool on = g_beep.load();
    for (int i = 0; i < samples; ++i) {
        out[i] = on ? (g_phase < 0.5 ? 2500 : -2500) : 0;
        g_phase += 440.0 / 44100.0;
        if (g_phase >= 1.0) g_phase -= 1.0;
    }
}

// ------------------------------------------------------------ frontend
static int mapKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_1: return 0x1; case SDLK_2: return 0x2; case SDLK_3: return 0x3; case SDLK_4: return 0xC;
    case SDLK_q: return 0x4; case SDLK_w: return 0x5; case SDLK_e: return 0x6; case SDLK_r: return 0xD;
    case SDLK_a: return 0x7; case SDLK_s: return 0x8; case SDLK_d: return 0x9; case SDLK_f: return 0xE;
    case SDLK_z: return 0xA; case SDLK_x: return 0x0; case SDLK_c: return 0xB; case SDLK_v: return 0xF;
    default: return -1;
    }
}

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return !out.empty();
}

static std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// Full printable ASCII 5x7 font (missing glyphs previously showed as '?')
static const uint8_t* glyph5x7(char ch) {
    static const uint8_t FONT5[95][7] = {
        /* ' ' */ {0,0,0,0,0,0,0},
        /* '!' */ {4,4,4,4,4,0,4},
        /* '"' */ {10,10,10,0,0,0,0},
        /* '#' */ {10,10,31,10,31,10,10},
        /* '$' */ {4,15,20,14,5,30,4},
        /* '%' */ {25,25,2,4,8,19,19},
        /* '&' */ {8,20,20,8,21,18,13},
        /* '\''*/ {4,4,4,0,0,0,0},
        /* '(' */ {2,4,8,8,8,4,2},
        /* ')' */ {8,4,2,2,2,4,8},
        /* '*' */ {0,4,21,14,21,4,0},
        /* '+' */ {0,4,4,31,4,4,0},
        /* ',' */ {0,0,0,0,4,4,8},
        /* '-' */ {0,0,0,31,0,0,0},
        /* '.' */ {0,0,0,0,0,12,12},
        /* '/' */ {1,1,2,4,8,16,16},
        /* '0' */ {14,17,19,21,25,17,14},
        /* '1' */ {4,12,4,4,4,4,14},
        /* '2' */ {14,17,1,2,4,8,31},
        /* '3' */ {14,17,1,6,1,17,14},
        /* '4' */ {2,6,10,18,31,2,2},
        /* '5' */ {31,16,30,1,1,17,14},
        /* '6' */ {6,8,16,30,17,17,14},
        /* '7' */ {31,1,2,4,8,8,8},
        /* '8' */ {14,17,17,14,17,17,14},
        /* '9' */ {14,17,17,15,1,2,12},
        /* ':' */ {0,12,12,0,12,12,0},
        /* ';' */ {0,12,12,0,12,4,8},
        /* '<' */ {2,4,8,16,8,4,2},
        /* '=' */ {0,0,31,0,31,0,0},
        /* '>' */ {8,4,2,1,2,4,8},
        /* '?' */ {14,17,1,2,4,0,4},
        /* '@' */ {14,17,1,13,21,21,14},
        /* 'A' */ {14,17,17,31,17,17,17},
        /* 'B' */ {30,17,17,30,17,17,30},
        /* 'C' */ {14,17,16,16,16,17,14},
        /* 'D' */ {30,17,17,17,17,17,30},
        /* 'E' */ {31,16,16,30,16,16,31},
        /* 'F' */ {31,16,16,30,16,16,16},
        /* 'G' */ {14,17,16,23,17,17,15},
        /* 'H' */ {17,17,17,31,17,17,17},
        /* 'I' */ {14,4,4,4,4,4,14},
        /* 'J' */ {1,1,1,1,17,17,14},
        /* 'K' */ {17,18,20,24,20,18,17},
        /* 'L' */ {16,16,16,16,16,16,31},
        /* 'M' */ {17,27,21,21,17,17,17},
        /* 'N' */ {17,25,21,19,17,17,17},
        /* 'O' */ {14,17,17,17,17,17,14},
        /* 'P' */ {30,17,17,30,16,16,16},
        /* 'Q' */ {14,17,17,17,21,18,13},
        /* 'R' */ {30,17,17,30,20,18,17},
        /* 'S' */ {15,16,16,14,1,1,30},
        /* 'T' */ {31,4,4,4,4,4,4},
        /* 'U' */ {17,17,17,17,17,17,14},
        /* 'V' */ {17,17,17,17,17,10,4},
        /* 'W' */ {17,17,17,21,21,21,10},
        /* 'X' */ {17,17,10,4,10,17,17},
        /* 'Y' */ {17,17,10,4,4,4,4},
        /* 'Z' */ {31,1,2,4,8,16,31},
        /* '[' */ {14,8,8,8,8,8,14},
        /* '\\'*/ {16,16,8,4,2,1,1},
        /* ']' */ {14,2,2,2,2,2,14},
        /* '^' */ {4,10,17,0,0,0,0},
        /* '_' */ {0,0,0,0,0,0,31},
        /* '`' */ {8,4,2,0,0,0,0},
        /* 'a' */ {0,0,14,1,15,17,15},
        /* 'b' */ {16,16,30,17,17,17,30},
        /* 'c' */ {0,0,14,16,16,16,14},
        /* 'd' */ {1,1,15,17,17,17,15},
        /* 'e' */ {0,0,14,17,31,16,14},
        /* 'f' */ {6,8,8,28,8,8,8},
        /* 'g' */ {0,0,15,17,15,1,14},
        /* 'h' */ {16,16,30,17,17,17,17},
        /* 'i' */ {4,0,12,4,4,4,14},
        /* 'j' */ {2,0,6,2,2,18,12},
        /* 'k' */ {16,16,18,20,24,20,18},
        /* 'l' */ {12,4,4,4,4,4,14},
        /* 'm' */ {0,0,26,21,21,21,21},
        /* 'n' */ {0,0,30,17,17,17,17},
        /* 'o' */ {0,0,14,17,17,17,14},
        /* 'p' */ {0,0,30,17,30,16,16},
        /* 'q' */ {0,0,15,17,15,1,1},
        /* 'r' */ {0,0,22,25,16,16,16},
        /* 's' */ {0,0,15,16,14,1,30},
        /* 't' */ {8,8,28,8,8,8,6},
        /* 'u' */ {0,0,17,17,17,17,15},
        /* 'v' */ {0,0,17,17,17,10,4},
        /* 'w' */ {0,0,17,17,21,21,10},
        /* 'x' */ {0,0,17,10,4,10,17},
        /* 'y' */ {0,0,17,17,15,1,14},
        /* 'z' */ {0,0,31,2,4,8,31},
        /* '{' */ {2,4,4,8,4,4,2},
        /* '|' */ {4,4,4,4,4,4,4},
        /* '}' */ {8,4,4,2,4,4,8},
        /* '~' */ {0,0,8,21,2,0,0},
    };
    if (ch < 32 || ch > 126) ch = '?';
    return FONT5[(unsigned char)ch - 32];
}

static void drawChar(SDL_Renderer* ren, int x, int y, char ch, Uint8 cr, Uint8 cg, Uint8 cb, int scale = 1) {
    const uint8_t* gl = glyph5x7(ch);
    SDL_SetRenderDrawColor(ren, cr, cg, cb, 0xFF);
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = gl[row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (16 >> col)) {
                SDL_Rect px{x + col * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(ren, &px);
            }
        }
    }
}

static void drawText(SDL_Renderer* ren, int x, int y, const char* text, Uint8 r, Uint8 g, Uint8 b, int scale = 1) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { cx = x; y += 8 * scale; continue; }
        drawChar(ren, cx, y, *p, r, g, b, scale);
        cx += 6 * scale;
    }
}

static int textWidth(const char* text, int scale = 1) {
    int n = 0, maxn = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { if (n > maxn) maxn = n; n = 0; }
        else ++n;
    }
    if (n > maxn) maxn = n;
    return maxn * 6 * scale;
}

// Native file picker (macOS osascript / Linux zenity) — VB6 CommonDialog equivalent.
// Kept simple: no UTI filters (those made osascript abort on some macOS versions).
static std::string pickRomFile() {
#if defined(__APPLE__)
    // One try / on error / end try only — dual "on error" handlers are a syntax error (-2741).
    FILE* pipe = popen(
        "osascript 2>/dev/null <<'APPLESCRIPT'\n"
        "try\n"
        "  POSIX path of (choose file with prompt \"Load CHIP-8 ROM\")\n"
        "on error\n"
        "  return \"\"\n"
        "end try\n"
        "APPLESCRIPT",
        "r");
#elif defined(__linux__)
    FILE* pipe = popen(
        "zenity --file-selection --title='Load CHIP-8 ROM' "
        "--file-filter='CHIP-8 ROMs | *.ch8 *.c8 *.bin *.rom' "
        "--file-filter='All files | *' 2>/dev/null",
        "r");
#else
    FILE* pipe = nullptr;
#endif
    if (!pipe) return {};
    char buf[2048]{};
    if (!std::fgets(buf, sizeof buf, pipe)) { pclose(pipe); return {}; }
    int rc = pclose(pipe);
    if (rc != 0 && buf[0] == '\0') return {};
    std::string path = buf;
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
        path.pop_back();
    return path;
}

// VB6 / mGBA MenuStrip ------------------------------------------------
enum MenuAction {
    MENU_NONE = 0,
    MENU_LOAD_ROM,
    MENU_EXIT,
    MENU_CONTROLS,
    MENU_COPYRIGHT,
    MENU_ABOUT,
    MENU_HELP,
};

struct MenuItem {
    const char* label;
    MenuAction action;
    bool separator;
};

struct MenuTop {
    const char* label;
    int x, w;                 // hit box on strip
    const MenuItem* items;
    int count;
};

static const MenuItem FILE_ITEMS[] = {
    {"Load ROM...", MENU_LOAD_ROM, false},
    {"Exit",        MENU_EXIT,     false},
};
static const MenuItem HELP_ITEMS[] = {
    {"Help",       MENU_HELP,       false},
    {"Controls",   MENU_CONTROLS,   false},
    {"Copyright",  MENU_COPYRIGHT,  false},
    {"About",      MENU_ABOUT,      false},
};

struct MenuStrip {
    MenuTop tops[2]{};
    int open = -1;            // which top menu is dropped (-1 = none)
    int hoverTop = -1;
    int hoverItem = -1;
    static constexpr int DROP_ITEM_H = 20;
    static constexpr int DROP_PAD = 4;

    void layout() {
        tops[0] = {"File", 6, 40, FILE_ITEMS, (int)(sizeof FILE_ITEMS / sizeof FILE_ITEMS[0])};
        tops[1] = {"Help", 50, 40, HELP_ITEMS, (int)(sizeof HELP_ITEMS / sizeof HELP_ITEMS[0])};
    }

    int dropWidth(const MenuTop& t) const {
        int mw = 100;
        for (int i = 0; i < t.count; ++i) {
            int tw = textWidth(t.items[i].label) + 24;
            if (tw > mw) mw = tw;
        }
        return mw;
    }

    SDL_Rect dropRect(const MenuTop& t) const {
        return SDL_Rect{t.x, MENU_H, dropWidth(t), t.count * DROP_ITEM_H + DROP_PAD * 2};
    }

    // returns action if a menu command was chosen
    MenuAction onEvent(const SDL_Event& e) {
        if (e.type == SDL_MOUSEMOTION) {
            int mx = e.motion.x, my = e.motion.y;
            hoverTop = -1;
            hoverItem = -1;
            if (my < MENU_H) {
                for (int i = 0; i < 2; ++i)
                    if (mx >= tops[i].x && mx < tops[i].x + tops[i].w) hoverTop = i;
            }
            if (open >= 0) {
                SDL_Rect dr = dropRect(tops[open]);
                if (mx >= dr.x && mx < dr.x + dr.w && my >= dr.y && my < dr.y + dr.h) {
                    int iy = (my - dr.y - DROP_PAD) / DROP_ITEM_H;
                    if (iy >= 0 && iy < tops[open].count) hoverItem = iy;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = e.button.x, my = e.button.y;
            if (my < MENU_H) {
                int hit = -1;
                for (int i = 0; i < 2; ++i)
                    if (mx >= tops[i].x && mx < tops[i].x + tops[i].w) hit = i;
                if (hit >= 0) {
                    open = (open == hit) ? -1 : hit;
                    hoverItem = -1;
                    return MENU_NONE;
                }
                open = -1;
                return MENU_NONE;
            }
            if (open >= 0) {
                SDL_Rect dr = dropRect(tops[open]);
                if (mx >= dr.x && mx < dr.x + dr.w && my >= dr.y && my < dr.y + dr.h) {
                    int iy = (my - dr.y - DROP_PAD) / DROP_ITEM_H;
                    if (iy >= 0 && iy < tops[open].count) {
                        MenuAction a = tops[open].items[iy].action;
                        open = -1;
                        hoverItem = -1;
                        return a;
                    }
                }
                open = -1; // click outside closes
            }
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            // Alt shortcuts like VB6 MenuStrip
            SDL_Keycode k = e.key.keysym.sym;
            SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;
            if ((mod & KMOD_CTRL) && (k == SDLK_o)) return MENU_LOAD_ROM;
            if ((mod & KMOD_CTRL) && (k == SDLK_q)) return MENU_EXIT;
            if (k == SDLK_F1 && !(mod & KMOD_CTRL)) { /* keep F1 = save state in main */ }
            if (k == SDLK_ESCAPE && open >= 0) { open = -1; return MENU_NONE; }
        }
        return MENU_NONE;
    }

    void draw(SDL_Renderer* ren) {
        // MenuStrip bar (mGBA / VB6 gray-blue chrome)
        SDL_Rect bar{0, 0, WIN_W, MENU_H};
        SDL_SetRenderDrawColor(ren, 0x2A, 0x2A, 0x36, 0xFF);
        SDL_RenderFillRect(ren, &bar);
        SDL_SetRenderDrawColor(ren, 0x3E, 0x3E, 0x50, 0xFF);
        SDL_RenderDrawLine(ren, 0, MENU_H - 1, WIN_W, MENU_H - 1);

        for (int i = 0; i < 2; ++i) {
            bool hot = (hoverTop == i) || (open == i);
            if (hot) {
                SDL_Rect hb{tops[i].x - 2, 2, tops[i].w, MENU_H - 4};
                SDL_SetRenderDrawColor(ren, 0x3A, 0x5A, 0x8A, 0xFF);
                SDL_RenderFillRect(ren, &hb);
            }
            drawText(ren, tops[i].x + 4, 7, tops[i].label, 0xE0, 0xE4, 0xF0, 1);
        }

        // right-side strip caption (mGBA feel)
        const char* cap = "MenuStrip";
        drawText(ren, WIN_W - textWidth(cap) - 8, 7, cap, 0x70, 0x78, 0x90, 1);

        if (open < 0) return;
        const MenuTop& t = tops[open];
        SDL_Rect dr = dropRect(t);
        // dropdown shadow
        SDL_Rect sh{dr.x + 2, dr.y + 2, dr.w, dr.h};
        SDL_SetRenderDrawColor(ren, 0x10, 0x10, 0x14, 0xC0);
        SDL_RenderFillRect(ren, &sh);
        SDL_SetRenderDrawColor(ren, 0x36, 0x36, 0x44, 0xFF);
        SDL_RenderFillRect(ren, &dr);
        SDL_SetRenderDrawColor(ren, 0x6A, 0x6A, 0x7E, 0xFF);
        SDL_RenderDrawRect(ren, &dr);

        for (int i = 0; i < t.count; ++i) {
            int iy = dr.y + DROP_PAD + i * DROP_ITEM_H;
            if (hoverItem == i) {
                SDL_Rect hi{dr.x + 2, iy, dr.w - 4, DROP_ITEM_H};
                SDL_SetRenderDrawColor(ren, 0x3A, 0x5A, 0x8A, 0xFF);
                SDL_RenderFillRect(ren, &hi);
            }
            drawText(ren, dr.x + 10, iy + 6, t.items[i].label, 0xF0, 0xF2, 0xF8, 1);
        }
    }

    bool blocksGameClick(int mx, int my) const {
        if (my < MENU_H) return true;
        if (open >= 0) {
            SDL_Rect dr = dropRect(tops[open]);
            if (mx >= dr.x && mx < dr.x + dr.w && my >= dr.y && my < dr.y + dr.h) return true;
        }
        return false;
    }
};

int main(int argc, char** argv) {
    std::string romPath;
    int ipf = 10; // ~600 Hz @ 60 FPS — classic CHIP-8 pace
    bool legacy = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--legacy") legacy = true;
        else if (a == "--ipf" && i + 1 < argc) ipf = std::max(1, std::atoi(argv[++i]));
        else romPath = a;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    SDL_Window* win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { std::fprintf(stderr, "Window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { std::fprintf(stderr, "Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H); // crisp on Retina
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, CH8_W, CH8_H);
    if (!tex) {
        std::fprintf(stderr, "Texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 512; want.callback = audioCallback;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    static Chip8 cpu, saveState;
    bool hasState = false;
    if (legacy) { cpu.q.shiftUsesVy = cpu.q.loadStoreIncI = cpu.q.vfReset = true; }
    Quirks quirks = cpu.q;

    std::vector<uint8_t> rom;
    std::string romName = "Demo";
    if (!romPath.empty() && readFile(romPath, rom) && rom.size() <= 4096 - 0x200)
        romName = baseName(romPath);
    else {
        if (!romPath.empty()) std::fprintf(stderr, "Could not load '%s', running demo.\n", romPath.c_str());
        rom.assign(DEMO_ROM, DEMO_ROM + sizeof DEMO_ROM);
    }
    cpu.load(rom); cpu.q = quirks;

    const uint32_t COL_ON = 0xFF9BBC0F, COL_OFF = 0xFF0F380F;  // classic green LCD
    const Uint64 freq = SDL_GetPerformanceFrequency();
    // Exact 60 Hz CHIP-8 frame clock (accumulates; no integer-division drift)
    const double ticksPerFrame = (double)freq / 60.0;
    double frameAccum = 0.0;
    Uint64 lastTick = SDL_GetPerformanceCounter(), fpsT = lastTick;
    int fpsFrames = 0;
    double fps = 60.0;
    bool running = true, paused = false, fast = false;
    uint32_t pix[CH8_W * CH8_H];
    // Defer modal dialogs: running them inside PollEvent crashes Cocoa/SDL on macOS.
    MenuAction pendingMenu = MENU_NONE;
    std::string pendingDrop;

    MenuStrip menu{};
    menu.layout();

    auto doLoadRom = [&](const std::string& path) {
        if (path.empty()) return false;
        std::vector<uint8_t> r;
        if (readFile(path, r) && !r.empty() && r.size() <= 4096 - 0x200) {
            rom = std::move(r); romName = baseName(path);
            cpu.load(rom); cpu.q = quirks; paused = false; hasState = false;
            lastTick = SDL_GetPerformanceCounter();
            frameAccum = 0.0;
            return true;
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Load ROM",
                                 "Could not load that file as a CHIP-8 ROM.", win);
        lastTick = SDL_GetPerformanceCounter();
        frameAccum = 0.0;
        return false;
    };

    auto runMenuAction = [&](MenuAction a) {
        // Pause audio while native dialogs block the main thread
        if (audio) SDL_PauseAudioDevice(audio, 1);
        g_beep = false;
        switch (a) {
        case MENU_LOAD_ROM: {
            std::string path = pickRomFile();
            if (!path.empty()) doLoadRom(path);
        } break;
        case MENU_EXIT:
            running = false;
            break;
        case MENU_CONTROLS:
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Controls", APP_CONTROLS, win);
            break;
        case MENU_COPYRIGHT:
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Copyright", APP_COPY, win);
            break;
        case MENU_ABOUT:
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "About chip8emu", APP_ABOUT, win);
            break;
        case MENU_HELP:
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Help", APP_HELP, win);
            break;
        default: break;
        }
        if (audio) SDL_PauseAudioDevice(audio, 0);
        lastTick = SDL_GetPerformanceCounter();
        frameAccum = 0.0;
        // Drain any events queued while the dialog was open
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            MenuAction ma = menu.onEvent(e);
            if (ma != MENU_NONE) {
                pendingMenu = ma; // run after this frame — avoids Cocoa crash
                continue;
            }
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_DROPFILE) {
                if (e.drop.file) {
                    pendingDrop = e.drop.file;
                    SDL_free(e.drop.file);
                }
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool down = e.type == SDL_KEYDOWN;
                SDL_Keycode k = e.key.keysym.sym;
                SDL_Keymod mod = (SDL_Keymod)e.key.keysym.mod;
                // Ctrl+O / Ctrl+Q handled in MenuStrip::onEvent
                if ((mod & KMOD_CTRL) && (k == SDLK_o || k == SDLK_q)) continue;
                int ck = mapKey(k);
                if (ck >= 0) cpu.keys[ck] = down;
                if (k == SDLK_TAB) fast = down;
                if (down && !e.key.repeat) {
                    if (k == SDLK_ESCAPE) {
                        if (menu.open >= 0) menu.open = -1;
                        else running = false;
                    }
                    else if (k == SDLK_p) paused = !paused;
                    else if (k == SDLK_F5) { cpu.load(rom); cpu.q = quirks; }
                    else if (k == SDLK_F1) { saveState = cpu; hasState = true; }
                    else if (k == SDLK_F4 && hasState) { cpu = saveState; }
                    else if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) ipf = std::min(200, ipf + 2);
                    else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) ipf = std::max(1, ipf - 2);
                }
            }
        }

        if (pendingMenu != MENU_NONE) {
            MenuAction a = pendingMenu;
            pendingMenu = MENU_NONE;
            runMenuAction(a);
            if (!running) break;
        }
        if (!pendingDrop.empty()) {
            std::string p = std::move(pendingDrop);
            pendingDrop.clear();
            doLoadRom(p);
        }

        // Advance CHIP-8 at a locked 60 Hz (timers + display cadence)
        Uint64 now = SDL_GetPerformanceCounter();
        frameAccum += (double)(now - lastTick);
        lastTick = now;
        int maxCatchUp = fast ? 12 : 3;
        int steps = 0;
        while (frameAccum >= ticksPerFrame && steps < maxCatchUp) {
            frameAccum -= ticksPerFrame;
            ++steps;
            if (!paused) {
                for (int i = 0; i < ipf; ++i) cpu.step();
                cpu.tickTimers();
            }
        }
        if (frameAccum > ticksPerFrame * 4) frameAccum = 0.0; // resync after stalls/dialogs
        g_beep.store(!paused && cpu.st > 0);

        for (int i = 0; i < CH8_W * CH8_H; ++i) pix[i] = cpu.disp[i] ? COL_ON : COL_OFF;
        SDL_UpdateTexture(tex, nullptr, pix, CH8_W * 4);

        // chrome
        SDL_SetRenderDrawColor(ren, 0x1E, 0x1E, 0x24, 0xFF);
        SDL_RenderClear(ren);

        // VB6 / mGBA MenuStrip (File / Help)
        menu.draw(ren);

        SDL_Rect frame{SCR_X - 3, SCR_Y - 3, CH8_W * SCALE + 6, CH8_H * SCALE + 6};
        SDL_SetRenderDrawColor(ren, 0x3A, 0x3A, 0x48, 0xFF);
        SDL_RenderFillRect(ren, &frame);
        SDL_Rect dst{SCR_X, SCR_Y, CH8_W * SCALE, CH8_H * SCALE};
        SDL_RenderCopy(ren, tex, nullptr, &dst);

        // status LEDs + English labels (under MenuStrip)
        auto led = [&](int x, bool on, Uint8 r, Uint8 g, Uint8 b) {
            SDL_Rect l{x, MENU_H + 4, 10, 10};
            if (on) SDL_SetRenderDrawColor(ren, r, g, b, 0xFF);
            else    SDL_SetRenderDrawColor(ren, 0x30, 0x30, 0x3A, 0xFF);
            SDL_RenderFillRect(ren, &l);
        };
        int lx = SCR_X;
        led(lx, !paused, 0x40, 0xD0, 0x60); drawText(ren, lx + 12, MENU_H + 5, "RUN", 0xA8, 0xB0, 0xC0, 1); lx += 42;
        led(lx, paused, 0xE0, 0xC0, 0x30);  drawText(ren, lx + 12, MENU_H + 5, "PAUSE", 0xA8, 0xB0, 0xC0, 1); lx += 54;
        led(lx, fast, 0x40, 0x90, 0xF0);     drawText(ren, lx + 12, MENU_H + 5, "FF", 0xA8, 0xB0, 0xC0, 1); lx += 30;
        led(lx, cpu.st > 0, 0xF0, 0x50, 0x50); drawText(ren, lx + 12, MENU_H + 5, "SND", 0xA8, 0xB0, 0xC0, 1); lx += 42;
        led(lx, cpu.halted, 0xA0, 0x40, 0xE0); drawText(ren, lx + 12, MENU_H + 5, "HALT", 0xA8, 0xB0, 0xC0, 1);

        // English status line: ROM / FPS / speed
        char shortName[48];
        std::snprintf(shortName, sizeof shortName, "%s", romName.c_str());
        if (std::strlen(shortName) > 28) { shortName[25] = '.'; shortName[26] = '.'; shortName[27] = '.'; shortName[28] = 0; }
        char status[160];
        std::snprintf(status, sizeof status, "%s  |  %.0f FPS  |  %d IPF  |  60Hz%s",
                      shortName, fps, ipf, paused ? "  PAUSED" : "");
        drawText(ren, SCR_X, SCR_Y - 12, status, 0xC8, 0xD0, 0xE0, 1);

        // keypad indicator (bottom): 16 keys with hex labels
        static const int order[16] = {1,2,3,0xC, 4,5,6,0xD, 7,8,9,0xE, 0xA,0,0xB,0xF};
        static const char* hexL = "0123456789ABCDEF";
        int ky = SCR_Y + CH8_H * SCALE + 12, kx0 = (WIN_W - (16 * 26 - 6)) / 2;
        drawText(ren, kx0, ky - 10, "CHIP-8 KEYPAD", 0x70, 0x78, 0x90, 1);
        for (int i = 0; i < 16; ++i) {
            SDL_Rect kr{kx0 + i * 26, ky, 20, 20};
            if (cpu.keys[order[i]]) SDL_SetRenderDrawColor(ren, 0x9B, 0xBC, 0x0F, 0xFF);
            else                    SDL_SetRenderDrawColor(ren, 0x2E, 0x34, 0x2A, 0xFF);
            SDL_RenderFillRect(ren, &kr);
            char lab[2] = { hexL[order[i]], 0 };
            drawText(ren, kr.x + 7, kr.y + 6, lab, 0xE0, 0xE8, 0xF0, 1);
        }
        SDL_RenderPresent(ren);

        // Cap to ~60 FPS when vsync is off; with vsync this mostly yields
        now = SDL_GetPerformanceCounter();
        double ahead = ticksPerFrame - frameAccum;
        if (ahead > 0) {
            Uint32 ms = (Uint32)(ahead * 1000.0 / (double)freq);
            if (ms > 0) SDL_Delay(ms > 16 ? 16 : ms);
        }

        ++fpsFrames;
        now = SDL_GetPerformanceCounter();
        if (now - fpsT >= freq) {
            fps = fpsFrames * (double)freq / (double)(now - fpsT);
            fpsFrames = 0; fpsT = now;
            char title[256];
            std::snprintf(title, sizeof title, "%s - %s | %.1f FPS | %d IPF%s%s",
                          APP_NAME, romName.c_str(), fps, ipf,
                          paused ? " | PAUSED" : "", fast ? " | FF" : "");
            SDL_SetWindowTitle(win, title);
        }
    }

    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
