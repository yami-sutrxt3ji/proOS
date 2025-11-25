#include "vbe.h"
#include "fb_font.h"

#include <stddef.h>
#include <stdint.h>

#define CONSOLE_COLUMNS 80
#define CONSOLE_ROWS 25
#define FONT_WIDTH 8

static const struct boot_info *bootinfo = (const struct boot_info *)BOOT_INFO_ADDR;
static uint32_t *fb_ptr = NULL;
static uint32_t fb_pitch_pixels = 0;
static uint32_t fb_w = 0;
static uint32_t fb_h = 0;
static int vbe_ready = 0;

static const uint8_t *font_base = &font8x8_basic[0][0];
static uint32_t font_stride = 8;
static uint32_t font_height_px = 8;
static uint32_t font_first_char = 32;
static uint32_t font_char_count = 96;
static int font_lsb_left = 1;

static uint8_t console_fg = 0x0F;
static uint8_t console_bg = 0x00;
static size_t console_row = 0;
static size_t console_col = 0;
static char console_chars[CONSOLE_ROWS][CONSOLE_COLUMNS];
static uint8_t console_attr[CONSOLE_ROWS][CONSOLE_COLUMNS];

static uint32_t vga_palette[16] = {
    0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
    0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
    0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
    0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF
};

static inline uint32_t attr_to_color(uint8_t attr)
{
    return vga_palette[attr & 0x0F];
}

static const uint8_t *glyph_for_char(unsigned char uc)
{
    if (!font_base || font_stride == 0)
        return NULL;

    if (uc >= font_first_char && uc < font_first_char + font_char_count)
        return font_base + ((uint32_t)(uc - font_first_char)) * font_stride;

    if (font_first_char == 0 && uc < font_char_count)
        return font_base + ((uint32_t)uc) * font_stride;

    uc = '?';
    if (uc >= font_first_char && uc < font_first_char + font_char_count)
        return font_base + ((uint32_t)(uc - font_first_char)) * font_stride;

    return NULL;
}

static void draw_glyph(int px, int py, char c, uint32_t fg, uint32_t bg)
{
    if (!vbe_ready)
        return;

    const uint8_t *glyph = glyph_for_char((unsigned char)c);
    if (!glyph)
        return;

    for (uint32_t y = 0; y < font_height_px; ++y)
    {
        uint8_t row = glyph[y];
        int dst_y = py + (int)y;
        if (dst_y < 0 || (uint32_t)dst_y >= fb_h)
            continue;

        uint32_t *dst = fb_ptr + dst_y * fb_pitch_pixels + px;
        for (int x = 0; x < FONT_WIDTH; ++x)
        {
            int dst_x = px + x;
            if (dst_x < 0 || (uint32_t)dst_x >= fb_w)
                continue;
            uint8_t mask = font_lsb_left ? (uint8_t)(1u << x) : (uint8_t)(0x80u >> x);
            uint32_t color = (row & mask) ? fg : bg;
            dst[x] = color;
        }
    }
}

static void console_redraw(void)
{
    if (!vbe_ready)
        return;

    for (int y = 0; y < CONSOLE_ROWS; ++y)
    {
        for (int x = 0; x < CONSOLE_COLUMNS; ++x)
        {
            uint8_t attr = console_attr[y][x];
            uint32_t fg = attr_to_color(attr & 0x0F);
            uint32_t bg = attr_to_color((attr >> 4) & 0x0F);
            draw_glyph(x * FONT_WIDTH, (int)(y * font_height_px), console_chars[y][x], fg, bg);
        }
    }
}

static void console_clear_buffers(uint8_t fg, uint8_t bg)
{
    for (int y = 0; y < CONSOLE_ROWS; ++y)
    {
        for (int x = 0; x < CONSOLE_COLUMNS; ++x)
        {
            console_chars[y][x] = ' ';
            console_attr[y][x] = ((bg & 0x0F) << 4) | (fg & 0x0F);
        }
    }
    console_row = 0;
    console_col = 0;
}

static void console_newline(void)
{
    console_col = 0;
    ++console_row;
    if (console_row < CONSOLE_ROWS)
        return;

    for (int y = 1; y < CONSOLE_ROWS; ++y)
    {
        for (int x = 0; x < CONSOLE_COLUMNS; ++x)
        {
            console_chars[y - 1][x] = console_chars[y][x];
            console_attr[y - 1][x] = console_attr[y][x];
        }
    }

    for (int x = 0; x < CONSOLE_COLUMNS; ++x)
    {
        console_chars[CONSOLE_ROWS - 1][x] = ' ';
        console_attr[CONSOLE_ROWS - 1][x] = ((console_bg & 0x0F) << 4) | (console_fg & 0x0F);
    }

    console_row = CONSOLE_ROWS - 1;
    console_col = 0;
    console_redraw();
}

int vbe_init(void)
{
    font_base = &font8x8_basic[0][0];
    font_stride = 8;
    font_height_px = 8;
    font_first_char = 32;
    font_char_count = 96;
    font_lsb_left = 1;

    if (bootinfo->magic != BOOT_INFO_MAGIC || bootinfo->fb_bpp != 32)
    {
        vbe_ready = 0;
        return 0;
    }

    fb_ptr = (uint32_t *)(uintptr_t)bootinfo->fb_ptr;
    fb_pitch_pixels = bootinfo->fb_pitch / 4;
    fb_w = bootinfo->fb_width;
    fb_h = bootinfo->fb_height;
    vbe_ready = 1;

    if (bootinfo->font_ptr && bootinfo->font_height >= 8 && bootinfo->font_bytes_per_char >= bootinfo->font_height)
    {
        font_base = (const uint8_t *)(uintptr_t)bootinfo->font_ptr;
        font_stride = bootinfo->font_bytes_per_char;
        font_height_px = bootinfo->font_height;
        font_first_char = 0;
        uint32_t count = bootinfo->font_char_count;
        if (count == 0)
            count = 256;
        font_char_count = count;
        font_lsb_left = (bootinfo->font_flags & 1u) ? 1 : 0;
    }

    vbe_clear(0x00000000);
    console_clear_buffers(console_fg, console_bg);
    console_redraw();
    return 1;
}

int vbe_available(void)
{
    return vbe_ready;
}

const struct boot_info *boot_info_get(void)
{
    return bootinfo;
}

uint32_t *vbe_framebuffer(void)
{
    return fb_ptr;
}

uint32_t vbe_pitch(void)
{
    return bootinfo->fb_pitch;
}

uint32_t vbe_width(void)
{
    return fb_w;
}

uint32_t vbe_height(void)
{
    return fb_h;
}

void vbe_clear(uint32_t color)
{
    if (!vbe_ready)
        return;

    uint32_t total = fb_pitch_pixels * fb_h;
    for (uint32_t i = 0; i < total; ++i)
        fb_ptr[i] = color;
}

void vbe_draw_pixel(int x, int y, uint32_t color)
{
    if (!vbe_ready)
        return;
    if (x < 0 || y < 0)
        return;
    if ((uint32_t)x >= fb_w || (uint32_t)y >= fb_h)
        return;

    fb_ptr[y * fb_pitch_pixels + x] = color;
}

void vbe_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!vbe_ready)
        return;

    for (int row = 0; row < h; ++row)
    {
        int dst_y = y + row;
        if (dst_y < 0 || (uint32_t)dst_y >= fb_h)
            continue;
        uint32_t *dst = fb_ptr + dst_y * fb_pitch_pixels;
        for (int col = 0; col < w; ++col)
        {
            int dst_x = x + col;
            if (dst_x < 0 || (uint32_t)dst_x >= fb_w)
                continue;
            dst[dst_x] = color;
        }
    }
}

void vbe_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    draw_glyph(x, y, c, fg, bg);
}

void vbe_draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg)
{
    while (*text)
    {
        vbe_draw_char(x, y, *text++, fg, bg);
        x += FONT_WIDTH;
    }
}

void vbe_console_set_colors(uint8_t fg_attr, uint8_t bg_attr)
{
    console_fg = fg_attr & 0x0F;
    console_bg = bg_attr & 0x0F;
}

void vbe_console_clear(uint8_t attr)
{
    console_fg = attr & 0x0F;
    console_bg = (attr >> 4) & 0x0F;
    if (!vbe_ready)
        return;

    vbe_clear(attr_to_color(console_bg));
    console_clear_buffers(console_fg, console_bg);
}

void vbe_console_putc(char c)
{
    if (!vbe_ready)
        return;

    if (c == '\n')
    {
        console_newline();
        return;
    }

    if (c == '\r')
    {
        console_col = 0;
        return;
    }

    if (c == '\b')
    {
        if (console_col > 0)
            --console_col;
        else if (console_row > 0)
        {
            --console_row;
            console_col = CONSOLE_COLUMNS - 1;
        }
        console_chars[console_row][console_col] = ' ';
        console_attr[console_row][console_col] = (console_bg << 4) | console_fg;
        draw_glyph((int)console_col * FONT_WIDTH, (int)(console_row * font_height_px), ' ', attr_to_color(console_fg), attr_to_color(console_bg));
        return;
    }

    console_chars[console_row][console_col] = c;
    console_attr[console_row][console_col] = ((console_bg & 0x0F) << 4) | (console_fg & 0x0F);
    draw_glyph((int)console_col * FONT_WIDTH, (int)(console_row * font_height_px), c, attr_to_color(console_fg), attr_to_color(console_bg));
    if (++console_col >= CONSOLE_COLUMNS)
        console_newline();
}

const uint8_t *vbe_font_table(void)
{
    return font_base;
}

uint32_t vbe_font_stride(void)
{
    return font_stride;
}

uint32_t vbe_font_height(void)
{
    return font_height_px;
}

uint32_t vbe_font_first_char(void)
{
    return font_first_char;
}

uint32_t vbe_font_char_count(void)
{
    return font_char_count ? font_char_count : 96;
}

int vbe_font_lsb_left(void)
{
    return font_lsb_left;
}
