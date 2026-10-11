#ifndef MYOS_FRAMEBUFFER_HPP
#define MYOS_FRAMEBUFFER_HPP

#include <stdint.h>
#include <limine.h>

namespace myos {

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

constexpr int MAX_SCREEN_WIDTH = 1920;
constexpr int MAX_SCREEN_HEIGHT = 1200;

class Canvas {
public:
    bool init(limine_framebuffer *framebuffer, uint32_t *pixels);
    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t color(uint32_t rgb) const;
    void pixel(int x, int y, uint32_t rgb);
    void rect(Rect bounds, uint32_t rgb);
    void text(const char *text, int x, int y, uint32_t rgb, int scale = 2,
              int max_width = 0);
    void copy_from(const uint32_t *source);
    void present(Rect bounds) const;
    void present() const { present({0, 0, width_, height_}); }

private:
    limine_framebuffer *framebuffer_ = nullptr;
    uint32_t *pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

int text_width(const char *text, int scale = 2);

}

#endif

