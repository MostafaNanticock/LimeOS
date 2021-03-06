#include <misc/memory.h>
#include <string.h>
#include <misc/cpuio.h>
#include <stdctl/framebuffer.h>
#include <stream.h>
#include <vfs/vfs.h>
#include <vfs/fileio.h>
#include <kernio/kernio.h>
#include <kmod/mctl.h>

int TERM_WIDTH = 0;
int TERM_HEIGHT = 0;

int CHAR_WIDTH = 0;
int CHAR_HEIGHT = 0;

struct RGBColor_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    bool operator==(RGBColor_t c) {
        return c.r == r && c.g == g && c.b == b;
    }
};

const RGBColor_t ANSI_COLOR_PALLET[16] = {
    {0, 0, 0},
    {127, 0, 0},
    {0, 127, 0},
    {127, 127, 0},
    {0, 0, 127},
    {127, 0, 127},
    {0, 127, 127},
    {192, 192, 192},
    {127, 127, 127},
    {255, 0, 0},
    {0, 255, 0},
    {255, 255, 0},
    {0, 0, 255},
    {255, 0, 255},
    {0, 255, 255},
    {255, 255, 255},
};

char* genDevName() {
    vector<FSNode_t> nodes = vfs::listNodes("/fio/dev");
    uint32_t count = 0;
    for (int i = 0; i < nodes.size(); i++) {
        if (strsw(nodes[i].name, "tty") && nodes[i].name[3] >= '0' && nodes[i].name[3] <= '9') {
            count++;
        }
    }
    string num = itoa(count, 10);
    char* base = "/dev/tty";
    char* name = (char*)malloc(8 + num.length() + 1);
    memcpy(name, base, 8);
    memcpy(name + 8, num.c_str(), num.length());
    name[8 + num.length()] = 0;
    return name;
}

uint8_t* background;
uint8_t* font;
FramebufferInfo_t fbInfo;

void loadBgImg(char* path) {
    stream_t img_stream = vfs::getStream(path);
    char* buffer = (char*)malloc(img_stream.slen);
    stream::read(img_stream, buffer, img_stream.slen);
    stream::close(img_stream);

    uint8_t* data = (uint8_t*)&buffer[0x36];

    float xfact = 1920.0f / (float)fbInfo.width;
    float yfact = 1080.0f / (float)fbInfo.height;

    for (int y = 0; y < fbInfo.height; y++) {
        for (int x = 0; x < fbInfo.width; x++) {
            int ix = xfact * x;
            int iy = 1079 - (yfact * y);
            background[((y * fbInfo.width) + x) * 4] = (float)data[((iy * 1920) + ix) * 3] * 0.2f;
            background[((y * fbInfo.width) + x) * 4 + 1] = (float)data[((iy * 1920) + ix) * 3 + 1] * 0.2f;
            background[((y * fbInfo.width) + x) * 4 + 2] = (float)data[((iy * 1920) + ix) * 3 + 2] * 0.2f;
        }
    }
    free(buffer);
    // TODO: Add support different file and fb types.
}

void drawBackground() {
    memcpy((char*)fbInfo.addr, background, fbInfo.height * fbInfo.pitch);
}

void drawChar(char c, int x, int y, RGBColor_t fore, RGBColor_t back) {
    uint8_t* fb = (uint8_t*)fbInfo.addr;
    uint8_t* fd = &font[(c * CHAR_HEIGHT * CHAR_WIDTH) + 12];
    for (int sy = 0; sy < CHAR_HEIGHT; sy++) {
        for (int sx = 0; sx < CHAR_WIDTH; sx++) {
            float fg = (float)fd[(sy * CHAR_WIDTH) + sx] / 256.0f;
            float bg = 1.0f - fg;
            if (back.r != 0 || back.g != 0 || back.b != 0) {
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4] = (fg * fore.b) + (bg * back.b);
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 1] = (fg * fore.g) + (bg * back.g);
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 2] = (fg * fore.r) + (bg * back.r);
            }
            else {
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4] = (fg * fore.b) + (bg * background[(((sy + y) * fbInfo.width) + (sx + x)) * 4]);
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 1] = (fg * fore.g) + (bg * background[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 1]);
                fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 2] = (fg * fore.r) + (bg * background[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 2]);
            }
        }
    }
}

int cx = 0;
int cy = 0;

struct TermChar_t {
    char c;
    RGBColor_t fg;
    RGBColor_t bg;

    bool operator==(TermChar_t t) {
        return t.c == c && t.fg == fg && t.bg == bg;
    }
};


uint32_t cursorX = 0;
uint32_t cursorY = 0;

RGBColor_t fgColor = {255, 255, 255};
RGBColor_t bgColor = {0, 0, 0};

TermChar_t* cbuf;

void drawCursor() {
    int height = CHAR_HEIGHT / 8;
    if (height == 0) {
        height = 1;
    }
    uint8_t* fb = (uint8_t*)fbInfo.addr;
    int sx = cursorX * CHAR_WIDTH;
    int sy = cursorY * CHAR_HEIGHT + (CHAR_HEIGHT - height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < CHAR_WIDTH; x++) {
            fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4] = 255;
            fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 1] = 255;
            fb[(((sy + y) * fbInfo.width) + (sx + x)) * 4 + 2] = 255;
        }
    }
}

void clearCursor() {
    TermChar_t tc = cbuf[(cursorY * TERM_WIDTH) + cursorX];
    drawChar(tc.c, cursorX * CHAR_WIDTH, cursorY * CHAR_HEIGHT, tc.fg, tc.bg);
}

void renderFullScreen() {
    for (int y = 0; y < TERM_HEIGHT; y++) {
        for (int x = 0; x < TERM_WIDTH; x++) {
            drawChar(cbuf[(y * TERM_WIDTH) + x].c, x * CHAR_WIDTH, y * CHAR_HEIGHT, cbuf[(y * TERM_WIDTH) + x].fg, cbuf[(y * TERM_WIDTH) + x].bg);
        }
    }
}

void scrollUp(int n = 1) {
    for (int i = 0; i < n; i++) {
        memcpy(&cbuf[0], &cbuf[TERM_WIDTH], TERM_WIDTH * (TERM_HEIGHT - 1) * sizeof(TermChar_t));
        for (int i = 0; i < TERM_WIDTH; i++) {
            cbuf[(TERM_WIDTH * (TERM_HEIGHT - 1)) + i].c = ' ';
        }
    }
    for (int y = 0; y < TERM_HEIGHT; y++) {
        for (int x = 0; x < TERM_WIDTH; x++) {
            if (y >= 1) {
                if (cbuf[(y * TERM_WIDTH) + x] == cbuf[((y - 1) * TERM_WIDTH) + x]) {
                    continue;
                }
            }
            drawChar(cbuf[(y * TERM_WIDTH) + x].c, x * CHAR_WIDTH, y * CHAR_HEIGHT, cbuf[(y * TERM_WIDTH) + x].fg, cbuf[(y * TERM_WIDTH) + x].bg);
        }
    }
}

void newLine() {
    cursorX = 0;
    if (cursorY >= TERM_HEIGHT - 1) {
        scrollUp();
        return;
    }
    cursorY++;
}

int runEscCode(char* code) {
    uint32_t codeLen = strlen(code);
    uint32_t last = 0;
    if (code[0] == '[') {
        uint32_t num = 0;
        vector<int> values;
        char suffix = '\0';
        for (int i = 1; i < codeLen; i++) {
            if (code[i] == ';') {
                values.push_back(num);
                num = 0;
                continue;
            }
            if (code[i] >= '0' && code[i] <= '9') {
                num *= 10;
                num += code[i] - '0';
                continue;
            }
            values.push_back(num);
            suffix = code[i];
            last = i;
            break;
        }

        if (suffix == 'm') {
            uint8_t intensity = 0;
            for (int i = 0; i < values.size(); i++) {
                if (values[i] == 1) {
                    intensity = 1 << 3;
                }
                if (values[i] >= 30 && values[i] <= 37) {
                    fgColor = ANSI_COLOR_PALLET[(values[i] - 30) | intensity];
                }
                if (values[i] >= 40 && values[i] <= 47) {
                    bgColor = ANSI_COLOR_PALLET[(values[i] - 40) | intensity];
                }
            }
        }
        else if (suffix == 'H') {
            if (values.size() != 2) {
                return last + 1;
            }
            int posx = (values[1] + (values[1] == 0)) - 1;
            int posy = (values[0] + (values[0] == 0)) - 1;
            if (posx >= TERM_WIDTH || posy >= TERM_HEIGHT) {
                return last + 1;
            }
            cursorX = posx;
            cursorY = posy;
        }
        else if (suffix == 'G') {
            if (values.size() != 1) {
                return last + 1;
            }
            int posx = (values[0] + (values[0] == 0)) - 1;
            if (posx >= TERM_WIDTH) {
                return last + 1;
            }
            cursorX = posx;
        }
    }
    return last + 1;
}

void print(char* str) {
    clearCursor();
    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        if (str[i] == '\n') {
            newLine();
            continue;
        }
        else if (str[i] == '\x1B') {
            i += runEscCode(&str[i + 1]);
        }
        else {
            cbuf[(cursorY * TERM_WIDTH) + cursorX].c = str[i];
            cbuf[(cursorY * TERM_WIDTH) + cursorX].fg = fgColor;
            cbuf[(cursorY * TERM_WIDTH) + cursorX].bg = bgColor;
            drawChar(str[i], cursorX * CHAR_WIDTH, cursorY * CHAR_HEIGHT, fgColor, bgColor);
            cursorX++;
            if (cursorX >= TERM_WIDTH) {
                newLine();
            }
        }  
    }
    drawCursor();
}

uint32_t _writeHndlr(stream_t s, uint32_t len, uint64_t pos) {
    char* str = (char*)malloc(len + 1);
    memcpy(str, s.buffer, len);
    str[len] = 0;
    print(str);
    free(str);
    return len;
}

uint32_t _readHndlr(stream_t s, uint32_t len, uint64_t pos) {
    return 0;
}

void _closeHndlr(stream_t s) {
    
}

stream_t _provider(void* tag) {
    return stream::create(0x1000, 0, _writeHndlr, _readHndlr, _closeHndlr, 0);;
}

extern "C"
bool _start() {
    kio::println("[gfx_term] Initializing graphic terminal...");

    if (!vfs::nodeExists("/fio/dev/fb0")) {
        kio::println("[gfx_term] ERROR: No framebuffer device found!");
        return false;
    }

    int ret = mctl::call("/dev/fb0", FB_MCTL_CMD_GETINFO, 0, &fbInfo);
    if (ret <= 0) {
        kio::println("[gfx_term] ERROR: Could not get framebuffer information!");
        return false;
    }
    if (fbInfo.bpp != 32) {
        kio::println("[gfx_term] ERROR: Only 32bpp framebuffers are currently supported!");
        return false;
    }
    char* buf = (char*)fbInfo.addr;
    memset(buf, 0x00, fbInfo.pitch * fbInfo.height);

    kio::println("[gfx_term] Loading background...");
    background = (uint8_t*)malloc(fbInfo.height * fbInfo.pitch);
    loadBgImg("/usr/images/dragon.bmp");
    drawBackground();

    kio::println("[gfx_term] Loading font...");
    stream_t font_s = vfs::getStream("/usr/fonts/consolas.bms");
    font = (uint8_t*)malloc(font_s.slen);
    stream::read(font_s, (char*)font, font_s.slen);
    stream::close(font_s);

    cursorX = 0;
    cursorY = 0;

    fgColor = {255, 255, 255};
    bgColor = {0, 0, 0};

    CHAR_WIDTH = be_uint16(&font[4]);
    CHAR_HEIGHT = be_uint16(&font[6]);
    TERM_WIDTH = fbInfo.width / CHAR_WIDTH;
    TERM_HEIGHT = fbInfo.height / CHAR_HEIGHT;
    cbuf = (TermChar_t*)malloc(TERM_WIDTH * TERM_HEIGHT * sizeof(TermChar_t));
    for (int i = 0; i < TERM_WIDTH * TERM_HEIGHT; i++) {
        cbuf[i].c = ' ';
        cbuf[i].fg = fgColor;
        cbuf[i].bg = bgColor;
    }

    drawCursor();

    kio::print("[gfx_term] Char WIDTHxHEIGHT: ");
    kio::print(itoa(CHAR_WIDTH, 10).c_str());
    kio::print("x");
    kio::println(itoa(CHAR_HEIGHT, 10).c_str());

    char* name = genDevName();
    kio::print("[gfx_term] Mounting ");
    kio::print(name);
    kio::println(" ...");

    fio::mountStreamProvider(name, FS_FLAG_O_W | FS_FLAG_O_R, _provider, NULL);
    kio::println("[gfx_term] Done.");
    return true;
}