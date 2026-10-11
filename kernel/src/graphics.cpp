#include <myos/framebuffer.hpp>

namespace {
constexpr uint8_t GLYPHS[][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30},
    {0,0,14,16,16,17,14}, {1,1,15,17,17,17,15},
    {0,0,14,17,31,16,14}, {6,8,8,28,8,8,8},
    {0,15,17,17,15,1,14}, {16,16,30,17,17,17,17},
    {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12},
    {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17},
    {0,0,14,17,17,17,14}, {0,0,30,17,30,16,16},
    {0,0,15,17,15,1,1}, {0,0,22,25,16,16,16},
    {0,0,15,16,14,1,30}, {8,8,28,8,8,9,6},
    {0,0,17,17,17,19,13}, {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
    {0,0,17,17,15,1,14}, {0,0,31,2,4,8,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
};

uint8_t glyph_row(char c, int row) {
    if (c >= 'A' && c <= 'Z') { return GLYPHS[c - 'A'][row]; }
    if (c >= 'a' && c <= 'z') { return GLYPHS[26 + c - 'a'][row]; }
    if (c >= '0' && c <= '9') { return GLYPHS[52 + c - '0'][row]; }
    switch (c) {
    case ' ': return 0;
    case '.': return row == 6 ? 4 : 0;
    case ',': return row == 5 ? 4 : row == 6 ? 8 : 0;
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case ';': return row == 2 || row == 5 ? 4 : row == 6 ? 8 : 0;
    case '-': return row == 3 ? 14 : 0;
    case '_': return row == 6 ? 31 : 0;
    case '+': return row == 3 ? 31 : row >= 1 && row <= 5 ? 4 : 0;
    case '=': return row == 2 || row == 4 ? 31 : 0;
    case '/': return static_cast<uint8_t>(1 << (row * 4 / 6));
    case '\\': return static_cast<uint8_t>(16 >> (row * 4 / 6));
    case '>': return row == 3 ? 2 : row == 2 || row == 4 ? 4 : row == 1 || row == 5 ? 8 : 0;
    case '<': return row == 3 ? 8 : row == 2 || row == 4 ? 4 : row == 1 || row == 5 ? 2 : 0;
    case '!': return row < 4 || row == 6 ? 4 : 0;
    case '?': return row == 0 ? 14 : row == 1 ? 17 : row == 2 ? 2 : row == 3 || row == 6 ? 4 : 0;
    case '(': return row == 0 || row == 6 ? 2 : row == 1 || row == 5 ? 4 : 8;
    case ')': return row == 0 || row == 6 ? 8 : row == 1 || row == 5 ? 4 : 2;
    case '[': case '{': return row == 0 || row == 6 ? 14 : 8;
    case ']': case '}': return row == 0 || row == 6 ? 14 : 2;
    case '\'': return row < 2 ? 4 : 0;
    case '"': return row < 2 ? 10 : 0;
    case '`': return row == 0 ? 8 : row == 1 ? 4 : 0;
    case '~': return row == 2 ? 10 : row == 3 ? 21 : 0;
    case '|': return 4;
    case '#': return row == 2 || row == 4 ? 31 : 10;
    case '*': return row == 2 || row == 4 ? 10 : row == 3 ? 4 : 0;
    case '^': return row == 0 ? 4 : row == 1 ? 10 : 0;
    case '%': return row == 0 || row == 6 ? 17 : static_cast<uint8_t>(1 << (row * 4 / 6));
    case '$': return row == 0 || row == 6 ? 4 : row == 1 || row == 3 || row == 5 ? 14 : row == 2 ? 20 : 5;
    case '@': return row == 0 || row == 6 ? 14 : row == 1 ? 17 : row == 5 ? 16 : 23;
    case '&': return row == 0 || row == 6 ? 12 : row == 3 ? 8 : row == 4 ? 21 : 18;
    default: return row == 0 || row == 6 ? 31 : 17;
    }
}
uint32_t channel(uint8_t value, uint8_t size, uint8_t shift) {
    return (((static_cast<uint32_t>(value) * ((1u << size) - 1)) / 255) << shift);
}
bool mask_valid(uint8_t size, uint8_t shift) {
    return size > 0 && size <= 8 && shift <= 32 - size;
}
}

namespace myos {
bool Canvas::init(limine_framebuffer *fb, uint32_t *pixels) {
    if (!fb || !fb->address || !pixels || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->bpp != 32 || fb->width < 640 || fb->height < 480 ||
        fb->width > MAX_SCREEN_WIDTH || fb->height > MAX_SCREEN_HEIGHT ||
        fb->pitch < fb->width * 4 || fb->pitch % 4 != 0 ||
        reinterpret_cast<uintptr_t>(fb->address) % 4 != 0 ||
        !mask_valid(fb->red_mask_size, fb->red_mask_shift) ||
        !mask_valid(fb->green_mask_size, fb->green_mask_shift) ||
        !mask_valid(fb->blue_mask_size, fb->blue_mask_shift)) { return false; }
    const uint32_t r = ((1u << fb->red_mask_size) - 1) << fb->red_mask_shift;
    const uint32_t g = ((1u << fb->green_mask_size) - 1) << fb->green_mask_shift;
    const uint32_t b = ((1u << fb->blue_mask_size) - 1) << fb->blue_mask_shift;
    if ((r & g) || (r & b) || (g & b)) { return false; }
    framebuffer_ = fb;
    pixels_ = pixels;
    width_ = static_cast<int>(fb->width);
    height_ = static_cast<int>(fb->height);
    return true;
}

uint32_t Canvas::color(uint32_t rgb) const {
    return channel(static_cast<uint8_t>(rgb >> 16), framebuffer_->red_mask_size, framebuffer_->red_mask_shift) |
           channel(static_cast<uint8_t>(rgb >> 8), framebuffer_->green_mask_size, framebuffer_->green_mask_shift) |
           channel(static_cast<uint8_t>(rgb), framebuffer_->blue_mask_size, framebuffer_->blue_mask_shift);
}
void Canvas::pixel(int x, int y, uint32_t rgb) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) { pixels_[y * width_ + x] = color(rgb); }
}
void Canvas::rect(Rect bounds, uint32_t rgb) {
    const int x0 = bounds.x < 0 ? 0 : bounds.x;
    const int y0 = bounds.y < 0 ? 0 : bounds.y;
    const int x1 = bounds.x + bounds.w > width_ ? width_ : bounds.x + bounds.w;
    const int y1 = bounds.y + bounds.h > height_ ? height_ : bounds.y + bounds.h;
    const uint32_t encoded = color(rgb);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) { pixels_[y * width_ + x] = encoded; }
    }
}
int text_width(const char *text, int scale) {
    int count = 0;
    while (*text++) { ++count; }
    return count * 6 * scale;
}
void Canvas::text(const char *text, int x, int y, uint32_t rgb, int scale, int max_width) {
    const int end = max_width > 0 ? x + max_width : width_;
    while (*text && x + 5 * scale <= end) {
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_row(*text, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) { rect({x + col * scale, y + row * scale, scale, scale}, rgb); }
            }
        }
        x += 6 * scale;
        ++text;
    }
}
void Canvas::copy_from(const uint32_t *source) {
    for (int i = 0; i < width_ * height_; ++i) { pixels_[i] = source[i]; }
}
void Canvas::present(Rect bounds) const {
    const int x0 = bounds.x < 0 ? 0 : bounds.x;
    const int y0 = bounds.y < 0 ? 0 : bounds.y;
    const int x1 = bounds.x + bounds.w > width_ ? width_ : bounds.x + bounds.w;
    const int y1 = bounds.y + bounds.h > height_ ? height_ : bounds.y + bounds.h;
    for (int y = y0; y < y1; ++y) {
        auto *row = reinterpret_cast<volatile uint32_t *>(
            static_cast<uint8_t *>(framebuffer_->address) + static_cast<uint64_t>(y) * framebuffer_->pitch);
        for (int x = x0; x < x1; ++x) { row[x] = pixels_[y * width_ + x]; }
    }
}
}
