#include <stddef.h>
#include <stdint.h>

#include <limine.h>
#include <myos/framebuffer.hpp>

namespace {

constexpr uint8_t GLYPHS[][7] = {
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, // A
    {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}, // B
    {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}, // C
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, // D
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, // E
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}, // F
    {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}, // G
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, // H
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, // I
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, // L
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, // O
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, // P
    {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}, // Q
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}, // R
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, // S
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, // U
    {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11}, // W
    {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}, // Z
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, // 0
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, // 1
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, // 2
    {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}, // 3
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, // 4
    {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}, // 5
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, // 6
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, // 8
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, // 9
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}, // .
    {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}, // :
    {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}, // +
    {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}, // _
};

int glyph_index(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= '0' && c <= '9') {
        return 26 + (c - '0');
    }
    switch (c) {
    case '.':
        return 36;
    case ':':
        return 37;
    case '+':
        return 38;
    case '-':
        return 39;
    case '_':
        return 40;
    default:
        return -1;
    }
}

uint32_t channel(uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    if (mask_size == 0) {
        return 0;
    }
    const uint64_t max = (uint64_t{1} << mask_size) - 1;
    return static_cast<uint32_t>(((static_cast<uint64_t>(value) * max) / 255) << mask_shift);
}

uint32_t rgb(limine_framebuffer *framebuffer, uint8_t r, uint8_t g, uint8_t b) {
    return channel(r, framebuffer->red_mask_size, framebuffer->red_mask_shift) |
           channel(g, framebuffer->green_mask_size, framebuffer->green_mask_shift) |
           channel(b, framebuffer->blue_mask_size, framebuffer->blue_mask_shift);
}

void put_pixel(limine_framebuffer *framebuffer, uint64_t x, uint64_t y, uint32_t color) {
    if (x >= framebuffer->width || y >= framebuffer->height) {
        return;
    }
    auto *row = reinterpret_cast<uint8_t *>(framebuffer->address) + y * framebuffer->pitch;
    auto *pixels = reinterpret_cast<uint32_t *>(row);
    pixels[x] = color;
}

void fill_rect(limine_framebuffer *framebuffer, uint64_t x, uint64_t y,
               uint64_t w, uint64_t h, uint32_t color) {
    for (uint64_t py = y; py < y + h && py < framebuffer->height; ++py) {
        for (uint64_t px = x; px < x + w && px < framebuffer->width; ++px) {
            put_pixel(framebuffer, px, py, color);
        }
    }
}

void draw_char(limine_framebuffer *framebuffer, char c, uint64_t x, uint64_t y,
               uint64_t scale, uint32_t color) {
    if (c == ' ') {
        return;
    }

    const int index = glyph_index(c);
    if (index < 0) {
        return;
    }

    for (uint64_t row = 0; row < 7; ++row) {
        for (uint64_t col = 0; col < 5; ++col) {
            if ((GLYPHS[index][row] & (uint8_t{1} << (4 - col))) != 0) {
                fill_rect(framebuffer, x + col * scale, y + row * scale,
                          scale, scale, color);
            }
        }
    }
}

void draw_text(limine_framebuffer *framebuffer, const char *text, uint64_t x,
               uint64_t y, uint64_t scale, uint32_t color) {
    uint64_t cursor = x;
    for (const char *p = text; *p != '\0'; ++p) {
        draw_char(framebuffer, *p, cursor, y, scale, color);
        cursor += 6 * scale;
    }
}

} // namespace

namespace myos {

bool draw_welcome(limine_framebuffer *framebuffer) {
    if (framebuffer == nullptr ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32 ||
        framebuffer->width == 0 ||
        framebuffer->height == 0) {
        return false;
    }

    const uint32_t deep = rgb(framebuffer, 9, 19, 28);
    const uint32_t blue = rgb(framebuffer, 34, 113, 177);
    const uint32_t green = rgb(framebuffer, 67, 176, 122);
    const uint32_t white = rgb(framebuffer, 238, 244, 240);
    const uint32_t muted = rgb(framebuffer, 151, 174, 183);
    const uint32_t line = rgb(framebuffer, 236, 177, 74);

    for (uint64_t y = 0; y < framebuffer->height; ++y) {
        for (uint64_t x = 0; x < framebuffer->width; ++x) {
            const uint8_t r = static_cast<uint8_t>(9 + (x * 42) / framebuffer->width);
            const uint8_t g = static_cast<uint8_t>(19 + (y * 54) / framebuffer->height);
            const uint8_t b = static_cast<uint8_t>(28 + ((x + y) * 48) /
                                                   (framebuffer->width + framebuffer->height));
            put_pixel(framebuffer, x, y, rgb(framebuffer, r, g, b));
        }
    }

    const uint64_t center_x = framebuffer->width / 2;
    const uint64_t center_y = framebuffer->height / 2;
    const uint64_t logo = framebuffer->height < 700 ? 72 : 96;
    const uint64_t scale = framebuffer->height < 700 ? 2 : 3;
    const uint64_t text_x = center_x > 210 ? center_x - 210 : 24;
    const uint64_t text_y = center_y > 70 ? center_y - 70 : 24;

    fill_rect(framebuffer, center_x - logo - 20, text_y - 16, logo, logo, blue);
    fill_rect(framebuffer, center_x - logo + 6, text_y + 10, logo, logo, green);
    fill_rect(framebuffer, center_x - logo + 18, text_y + 22, logo / 2, logo / 2, deep);

    draw_text(framebuffer, "MYOS V0.1.0", text_x, text_y, scale, white);
    draw_text(framebuffer, "C++ KERNEL BOOTED", text_x, text_y + 12 * scale, scale, muted);
    draw_text(framebuffer, "LIMINE FRAMEBUFFER OK", text_x, text_y + 23 * scale, scale, muted);
    fill_rect(framebuffer, text_x, text_y + 34 * scale, 260, 4, line);

    return true;
}

} // namespace myos

