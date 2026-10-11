#include <myos/desktop.hpp>
#include <myos/framebuffer.hpp>
#include <myos/input.hpp>
#include <myos/serial.hpp>
#include <myos/text_buffer.hpp>
#include <myos/memory.hpp>
#include <myos/runtime.hpp>

namespace {
using namespace myos;

constexpr uint32_t INK = 0x243531;
constexpr uint32_t MUTED = 0x61736d;
constexpr uint32_t PAPER = 0xf5f8f6;
constexpr uint32_t EDGE = 0xc7d6cf;
constexpr uint32_t ACCENT = 0x187c6b;
constexpr int PANEL = 48;
constexpr int TITLE = 36;
constexpr int APP_COUNT = 4;

enum App { Terminal, Notes, About, Appearance };
const char *names[] = {"Terminal", "Notes", "About", "Appearance"};
const char *labels[] = {"Terminal", "Notes", "About", "Style"};

struct Window { Rect bounds; bool open = false; bool minimized = false; };
Window windows[APP_COUNT];
int order[APP_COUNT] = {0, 1, 2, 3};
int active = -1;
int drag_app = -1;
int drag_x = 0;
int drag_y = 0;
int pointer_x = 0;
int pointer_y = 0;
bool left_down = false;
bool launcher = false;
int wallpaper_style = 0;
InputDevices devices;
Canvas canvas;

// Fixed contiguous backing storage; the physical allocator does not provide a heap yet.
uint32_t background[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT];
uint32_t scene[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT];
TextBuffer<2048> note;
TextBuffer<128> command_line;
char terminal_lines[64][80] = {};
int terminal_count = 0;
bool note_edited = false;

int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }
int abs(int a) { return a < 0 ? -a : a; }
int clamp(int value, int lo, int hi) { return max(lo, min(value, hi)); }
bool equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool starts_with(const char *text, const char *prefix) {
    while (*prefix) { if (*text++ != *prefix++) { return false; } }
    return true;
}
void number(uint64_t value, char *text) {
    char reversed[21];
    int count = 0;
    do { reversed[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    for (int i = 0; i < count; ++i) { text[i] = reversed[count - i - 1]; }
    text[count] = 0;
}
void outline(Rect r, uint32_t color) {
    canvas.rect({r.x, r.y, r.w, 1}, color);
    canvas.rect({r.x, r.y + r.h - 1, r.w, 1}, color);
    canvas.rect({r.x, r.y, 1, r.h}, color);
    canvas.rect({r.x + r.w - 1, r.y, 1, r.h}, color);
}

// Original raster landscape: independent of host files, GPU drivers, or a heap.
void landscape(Rect r, int style) {
    const uint32_t sky = style ? 0xeac7c8 : 0xd9e9e6;
    const uint32_t far = style ? 0x849b9f : 0x98bbb5;
    const uint32_t middle = style ? 0x547d7d : 0x648e84;
    const uint32_t front = style ? 0x304e59 : 0x355e57;
    canvas.rect(r, sky);
    for (int x = 0; x < r.w; ++x) {
        const int ridge = min(r.h * 33 / 100 + abs(x - r.w * 3 / 10) * r.h / r.w,
                              r.h * 42 / 100 + abs(x - r.w * 8 / 10) * r.h / (r.w * 2));
        canvas.rect({r.x + x, r.y + ridge, 1, r.h - ridge}, far);
        if (ridge < r.h * 44 / 100) {
            const int snow = min(r.h * 44 / 100 - ridge, r.h / 18);
            canvas.rect({r.x + x, r.y + ridge, 1, snow}, 0xecf3ee);
        }
        const int ridge2 = r.h * 62 / 100 + abs(x - r.w * 6 / 10) * r.h / (r.w * 3);
        canvas.rect({r.x + x, r.y + ridge2, 1, r.h - ridge2}, middle);
        const int ridge3 = r.h * 80 / 100 + abs(x - r.w / 5) * r.h / (r.w * 6);
        canvas.rect({r.x + x, r.y + ridge3, 1, r.h - ridge3}, front);
    }
}
void rebuild_background() {
    landscape({0, 0, canvas.width(), canvas.height()}, wallpaper_style);
    for (int i = 0; i < canvas.width() * canvas.height(); ++i) { background[i] = scene[i]; }
}
void icon(int app, int x, int y, int size) {
    if (app == Terminal) {
        canvas.rect({x, y, size, size}, INK);
        outline({x, y, size, size}, 0x94ada4);
        canvas.text(">", x + size / 6, y + size / 3, 0x9be2c1, size >= 32 ? 2 : 1);
        canvas.rect({x + size * 3 / 5, y + size * 2 / 3, size / 4, 2}, PAPER);
    } else if (app == Notes) {
        canvas.rect({x + size / 7, y, size * 5 / 7, size}, 0xf8f4d8);
        canvas.rect({x + size / 7, y, size * 5 / 7, size / 5}, 0xe6bd5c);
        for (int i = 0; i < 3; ++i) {
            canvas.rect({x + size / 4, y + size / 3 + i * size / 6, size / 2, 1}, MUTED);
        }
    } else if (app == About) {
        canvas.rect({x, y, size * 3 / 4, size * 3 / 4}, ACCENT);
        canvas.rect({x + size / 4, y + size / 4, size * 3 / 4, size * 3 / 4}, 0x9bdec1);
        canvas.rect({x + size / 2, y + size / 2, size / 4, size / 4}, INK);
    } else {
        canvas.rect({x, y, size / 2, size / 2}, 0xeac7c8);
        canvas.rect({x + size / 2, y, size / 2, size / 2}, 0x9bdec1);
        canvas.rect({x, y + size / 2, size / 2, size / 2}, 0xe6bd5c);
        canvas.rect({x + size / 2, y + size / 2, size / 2, size / 2}, 0x547d7d);
    }
}
Rect desktop_button(int app) { return {16, 54 + app * 84, 96, 78}; }
Rect task_button(int app) { return {68 + app * 48, canvas.height() - PANEL + 6, 40, 36}; }
Rect launcher_bounds() { return {12, canvas.height() - PANEL - 256, 236, 244}; }
Rect minimize_button(Rect r) { return {r.x + r.w - 66, r.y + 4, 28, 28}; }
Rect close_button(Rect r) { return {r.x + r.w - 34, r.y + 4, 28, 28}; }
bool visible(int app) { return windows[app].open && !windows[app].minimized; }
void focus(int app) {
    int position = 0;
    while (order[position] != app) { ++position; }
    for (int i = position; i < APP_COUNT - 1; ++i) { order[i] = order[i + 1]; }
    order[APP_COUNT - 1] = app;
    active = app;
}
void next_focus() {
    active = -1;
    for (int i = APP_COUNT - 1; i >= 0; --i) {
        if (visible(order[i])) { active = order[i]; return; }
    }
}
void open_app(int app) {
    windows[app].open = true;
    windows[app].minimized = false;
    launcher = false;
    focus(app);
    serial_write("MYOS: open ");
    serial_write_line(names[app]);
}
void terminal_line(const char *text) {
    const int columns = min(79, (windows[Terminal].bounds.w - 32) / 12);
    do {
        if (terminal_count == 64) {
            for (int row = 1; row < 64; ++row) {
                for (int col = 0; col < 80; ++col) { terminal_lines[row - 1][col] = terminal_lines[row][col]; }
            }
            --terminal_count;
        }
        char *dest = terminal_lines[terminal_count++];
        int col = 0;
        while (*text && col < columns) { dest[col++] = *text++; }
        dest[col] = 0;
    } while (*text);
}
void ram_text(uint64_t bytes, char *out) {
    char digits[21];
    number(bytes / (1024 * 1024), digits);
    int at = 0;
    for (int i = 0; digits[i]; ++i) { out[at++] = digits[i]; }
    const uint64_t fraction = bytes % (1024 * 1024) * 100 / (1024 * 1024);
    out[at++] = '.'; out[at++] = '0' + fraction / 10; out[at++] = '0' + fraction % 10;
    for (const char *unit = " MiB"; *unit; ++unit) { out[at++] = *unit; }
    out[at] = 0;
}
void terminal_stat(const char *label, uint64_t value, const char *unit) {
    TextBuffer<80> line;
    char digits[21];
    number(value, digits);
    const char *parts[] = {label, digits, unit};
    for (const char *part : parts) {
        while (*part) { line.append(*part++); }
    }
    terminal_line(line.data);
}
void execute_command() {
    terminal_line(command_line.data);
    const char *command = command_line.data;
    if (equal(command, "help")) {
        terminal_line("help  clear  version  echo TEXT");
        terminal_line("about  notes  wallpaper  mem  uptime  cpu");
        serial_write_line("MYOS: command help");
    } else if (equal(command, "clear")) {
        terminal_count = 0;
        serial_write_line("MYOS: command clear");
    } else if (equal(command, "version")) {
        terminal_line("ShawnOS 0.3.1 / x86-64 / C++");
        serial_write_line("MYOS: command version");
    } else if (equal(command, "about")) {
        open_app(About);
    } else if (equal(command, "notes")) {
        open_app(Notes);
    } else if (equal(command, "mem")) {
        const auto &frames = physical_frames();
        terminal_stat("Detected RAM: ", frames.detected_bytes() / 1024, " KiB");
        terminal_stat("Usable RAM: ", frames.usable_bytes() / 1024, " KiB");
        terminal_stat("Managed RAM: ", frames.managed_frames() * 4, " KiB");
        terminal_stat("Free RAM: ", frames.free_frames() * 4, " KiB");
        serial_write_line("MYOS: command mem");
    } else if (equal(command, "cpu")) {
        if (cpu_base_mhz()) { terminal_stat("CPU base: ", cpu_base_mhz(), " MHz"); }
        else { terminal_line("CPU base: not reported by hardware"); }
        if (tsc_mhz()) { terminal_stat("TSC clock: ~", tsc_mhz(), " MHz"); }
        else { terminal_line("TSC clock: unavailable"); }
        terminal_line("TSC is a clock counter, not live CPU speed.");
        serial_write_line("MYOS: command cpu");
    } else if (equal(command, "uptime")) {
        const uint64_t ticks = timer_ticks();
        terminal_stat("Uptime: ", ticks / TIMER_HZ, " seconds");
        terminal_stat("Timer ticks: ", ticks, "");
        terminal_stat("Idle wakes: ", idle_wakes(), "");
        char digits[21];
        serial_write("MYOS: uptime seconds="); number(ticks / TIMER_HZ, digits); serial_write(digits);
        serial_write(" ticks="); number(ticks, digits); serial_write(digits);
        serial_write(" idle="); number(idle_wakes(), digits); serial_write_line(digits);
    } else if (equal(command, "wallpaper")) {
        wallpaper_style ^= 1;
        rebuild_background();
        terminal_line(wallpaper_style ? "Dusk" : "Alpine");
        serial_write_line("MYOS: wallpaper changed");
    } else if (starts_with(command, "echo ")) {
        terminal_line(command + 5);
        serial_write_line("MYOS: command echo");
    } else if (*command) {
        terminal_line("Unknown command");
    }
    command_line.clear();
}
void paint_terminal(Rect r) {
    canvas.rect({r.x + 1, r.y + TITLE, r.w - 2, r.h - TITLE - 1}, 0x1d2926);
    const int rows = (r.h - TITLE - 36) / 20;
    const int first = max(0, terminal_count - rows);
    int y = r.y + TITLE + 14;
    for (int i = first; i < terminal_count; ++i) {
        canvas.text(terminal_lines[i], r.x + 16, y, 0xd9e9df, 2, r.w - 32);
        y += 20;
    }
    canvas.text(">", r.x + 16, y, 0x9be2c1);
    const int columns = (r.w - 52) / 12;
    const int start = max(0, static_cast<int>(command_line.size) - columns + 1);
    canvas.text(command_line.data + start, r.x + 40, y, PAPER, 2, r.w - 56);
    if (active == Terminal) {
        canvas.rect({r.x + 40 + (static_cast<int>(command_line.size) - start) * 12, y + 15, 10, 2}, 0x9be2c1);
    }
}
void paint_note(Rect r) {
    canvas.rect({r.x + 1, r.y + TITLE, r.w - 2, 34}, 0xe9efeb);
    canvas.text("Untitled", r.x + 16, r.y + TITLE + 10, MUTED);
    canvas.text("RAM only", r.x + r.w - 110, r.y + TITLE + 10, MUTED);
    const int columns = (r.w - 32) / 12;
    const int rows = (r.h - TITLE - 68) / 20;
    int total_row = 0;
    int col = 0;
    for (size_t i = 0; i < note.size; ++i) {
        if (note.data[i] == '\n') { ++total_row; col = 0; }
        else if (++col == columns) { ++total_row; col = 0; }
    }
    const int first = max(0, total_row - rows + 1);
    int row = 0;
    col = 0;
    for (size_t i = 0; i < note.size; ++i) {
        if (note.data[i] == '\n') { ++row; col = 0; continue; }
        if (row >= first && row < first + rows) {
            const char one[] = {note.data[i], 0};
            canvas.text(one, r.x + 16 + col * 12, r.y + TITLE + 48 + (row - first) * 20, INK);
        }
        if (++col == columns) { ++row; col = 0; }
    }
    if (active == Notes) {
        canvas.rect({r.x + 16 + col * 12, r.y + TITLE + 48 + (row - first) * 20, 2, 15}, ACCENT);
    }
    canvas.rect({r.x + 1, r.y + r.h - 24, r.w - 2, 23}, 0xe9efeb);
    char count[12];
    number(static_cast<unsigned>(note.size), count);
    canvas.text(count, r.x + 16, r.y + r.h - 16, MUTED, 1);
    canvas.text(note.size == 2047 ? "Full" : "characters", r.x + 54, r.y + r.h - 16, MUTED, 1);
}
void paint_about_uptime(Rect r) {
    const Rect band = {r.x + 136, r.y + 270, r.w - 160, 20};
    canvas.rect(band, PAPER);
    const uint64_t seconds = timer_ticks() / TIMER_HZ;
    char text[21]; number(seconds, text);
    const int scale = text_width(text) + 96 > band.w ? 1 : 2;
    canvas.text(text, band.x, band.y, INK, scale, band.w);
    canvas.text(seconds == 1 ? "second" : "seconds", band.x + text_width(text, scale) + 12, band.y, MUTED, scale);
}
void paint_about(Rect r) {
    icon(About, r.x + 24, r.y + 62, 48);
    canvas.text("ShawnOS", r.x + 92, r.y + 64, INK, 3);
    canvas.text("Version 0.3.1", r.x + 92, r.y + 94, MUTED);
    canvas.rect({r.x + 24, r.y + 132, r.w - 48, 1}, EDGE);
    char detected_ram[40], free_ram[40], clock[40];
    ram_text(physical_frames().detected_bytes(), detected_ram);
    ram_text(physical_frames().free_frames() * FrameAllocator::PAGE_SIZE, free_ram);
    const uint64_t mhz = cpu_base_mhz() ? cpu_base_mhz() : tsc_mhz();
    int at = 0;
    if (!cpu_base_mhz() && mhz) { clock[at++] = '~'; }
    if (mhz) {
        char digits[21]; number(mhz, digits);
        for (int i = 0; digits[i]; ++i) { clock[at++] = digits[i]; }
        for (const char *unit = " MHz"; *unit; ++unit) { clock[at++] = *unit; }
        clock[at] = 0;
    } else {
        const char *text = "Unavailable";
        while (*text) { clock[at++] = *text++; } clock[at] = 0;
    }
    const char *keys[] = {"Kernel", cpu_base_mhz() ? "CPU base" : "TSC clock", "Detected RAM", "Free RAM", "Timer"};
    const char *values[] = {"C++ / x86-64", clock, detected_ram, free_ram, "PIT / 100 Hz"};
    for (int i = 0; i < 5; ++i) {
        const int y = r.y + 150 + i * 24;
        const int scale = text_width(values[i]) > r.w - 160 ? 1 : 2;
        canvas.text(keys[i], r.x + 24, y, MUTED, 1);
        canvas.text(values[i], r.x + 136, y, INK, scale, r.w - 160);

    }
    paint_about_uptime(r);
}
Rect swatch(Rect r, int style) {
    const int width = (r.w - 64) / 2;
    return {r.x + 24 + style * (width + 16), r.y + 96, width, 92};
}
void paint_appearance(Rect r) {
    canvas.text("Wallpaper", r.x + 24, r.y + 60, INK);
    for (int i = 0; i < 2; ++i) {
        const Rect tile = swatch(r, i);
        landscape(tile, i);
        outline({tile.x - 3, tile.y - 3, tile.w + 6, tile.h + 6}, i == wallpaper_style ? ACCENT : EDGE);
        canvas.text(i ? "Dusk" : "Alpine", tile.x, tile.y + tile.h + 16, i == wallpaper_style ? ACCENT : MUTED);
    }
}
void paint_window(int app) {
    const Rect r = windows[app].bounds;
    canvas.rect({r.x + 4, r.y + 5, r.w, r.h}, 0x2b514b);
    canvas.rect(r, PAPER);
    canvas.rect({r.x + 1, r.y + 1, r.w - 2, TITLE - 1}, active == app ? 0xe1eee6 : 0xebefec);
    outline(r, active == app ? ACCENT : EDGE);
    canvas.rect({r.x + 1, r.y + TITLE - 1, r.w - 2, 1}, EDGE);
    icon(app, r.x + 12, r.y + 8, 20);
    canvas.text(names[app], r.x + 44, r.y + 12, INK, 2, r.w - 116);
    const Rect close = close_button(r);
    const Rect minimize = minimize_button(r);
    if (close.contains(pointer_x, pointer_y)) { canvas.rect(close, 0xf1d8d7); }
    if (minimize.contains(pointer_x, pointer_y)) { canvas.rect(minimize, 0xcbded3); }
    canvas.rect({minimize.x + 8, minimize.y + 18, 12, 2}, MUTED);
    for (int i = 0; i < 10; ++i) {
        canvas.rect({close.x + 9 + i, close.y + 9 + i, 2, 2}, MUTED);
        canvas.rect({close.x + 18 - i, close.y + 9 + i, 2, 2}, MUTED);
    }
    if (app == Terminal) { paint_terminal(r); }
    else if (app == Notes) { paint_note(r); }
    else if (app == About) { paint_about(r); }
    else { paint_appearance(r); }
}
void paint_launcher() {
    const Rect menu = launcher_bounds();
    canvas.rect({menu.x + 3, menu.y + 4, menu.w, menu.h}, 0x2b514b);
    canvas.rect(menu, PAPER);
    outline(menu, EDGE);
    canvas.text("Applications", menu.x + 16, menu.y + 18, MUTED);
    for (int app = 0; app < APP_COUNT; ++app) {
        Rect item = {menu.x + 8, menu.y + 48 + app * 46, menu.w - 16, 42};
        if (item.contains(pointer_x, pointer_y)) { canvas.rect(item, 0xe1eee6); }
        icon(app, item.x + 10, item.y + 7, 28);
        canvas.text(names[app], item.x + 54, item.y + 15, INK);
    }
}
int hover_target() {
    if (launcher && launcher_bounds().contains(pointer_x, pointer_y)) {
        const Rect menu = launcher_bounds();
        for (int app = 0; app < APP_COUNT; ++app) {
            if (Rect{menu.x + 8, menu.y + 48 + app * 46, menu.w - 16, 42}.contains(pointer_x, pointer_y)) {
                return 20 + app;
            }
        }
        return 24;
    }
    if (pointer_y >= canvas.height() - PANEL) {
        if (Rect{12, canvas.height() - PANEL + 6, 40, 36}.contains(pointer_x, pointer_y)) { return 30; }
        for (int app = 0; app < APP_COUNT; ++app) {
            if (task_button(app).contains(pointer_x, pointer_y)) { return 31 + app; }
        }
        return 35;
    }
    for (int i = APP_COUNT - 1; i >= 0; --i) {
        const int app = order[i];
        if (visible(app) && windows[app].bounds.contains(pointer_x, pointer_y)) {
            if (close_button(windows[app].bounds).contains(pointer_x, pointer_y)) { return 40 + app; }
            if (minimize_button(windows[app].bounds).contains(pointer_x, pointer_y)) { return 50 + app; }
            return 60 + app;
        }
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        if (desktop_button(app).contains(pointer_x, pointer_y)) { return app; }
    }
    return -1;
}
void draw_cursor() {
    constexpr int W = 14, H = 20;
    uint32_t saved[W * H] = {};
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (pointer_x + x < canvas.width() && pointer_y + y < canvas.height()) {
                saved[y * W + x] = scene[(pointer_y + y) * canvas.width() + pointer_x + x];
            }
        }
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x <= min(11, y * 3 / 4); ++x) {
            canvas.pixel(pointer_x + x, pointer_y + y, x == 0 || x == y * 3 / 4 || y == 15 ? INK : 0xffffff);
        }
    }
    canvas.rect({pointer_x + 5, pointer_y + 14, 4, 6}, INK);
    canvas.rect({pointer_x + 6, pointer_y + 14, 2, 5}, 0xffffff);
    canvas.present({pointer_x, pointer_y, W, H});
    // Restore only the RAM scene, leaving the visible cursor on the framebuffer.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (pointer_x + x < canvas.width() && pointer_y + y < canvas.height()) {
                scene[(pointer_y + y) * canvas.width() + pointer_x + x] = saved[y * W + x];
            }
        }
    }
}
void render() {
    canvas.copy_from(background);
    canvas.rect({0, 0, canvas.width(), 30}, INK);
    canvas.text("ShawnOS", 16, 8, PAPER);
    canvas.text("Desktop", 128, 8, 0xa9c9ba);
    canvas.text("v0.3.1", canvas.width() - 92, 8, 0xa9c9ba);
    for (int app = 0; app < APP_COUNT; ++app) {
        const Rect button = desktop_button(app);
        if (hover_target() == app) { outline(button, 0xcce6d6); }
        icon(app, button.x + 30, button.y + 4, 36);
        const int label_x = button.x + (button.w - text_width(labels[app])) / 2;
        canvas.text(labels[app], label_x + 1, button.y + 55, 0x35534d);
        canvas.text(labels[app], label_x, button.y + 54, PAPER);
    }
    for (int i = 0; i < APP_COUNT; ++i) { if (visible(order[i])) { paint_window(order[i]); } }
    canvas.rect({0, canvas.height() - PANEL, canvas.width(), PANEL}, PAPER);
    canvas.rect({0, canvas.height() - PANEL, canvas.width(), 1}, EDGE);
    const Rect start = {12, canvas.height() - PANEL + 6, 40, 36};
    if (launcher || start.contains(pointer_x, pointer_y)) { canvas.rect(start, 0xd8e9de); }
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) { canvas.rect({23 + x * 11, start.y + 8 + y * 11, 8, 8}, ACCENT); }
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        const Rect button = task_button(app);
        if (active == app || button.contains(pointer_x, pointer_y)) { canvas.rect(button, 0xe1eee6); }
        icon(app, button.x + 8, button.y + 6, 24);
        if (windows[app].open) {
            canvas.rect({button.x + 12, button.y + 32, 16, 2}, visible(app) ? ACCENT : MUTED);
        }
    }
    canvas.rect({canvas.width() - 155, canvas.height() - 26, 6, 6}, devices.keyboard && devices.mouse ? ACCENT : 0xc28a42);
    canvas.text("PS/2", canvas.width() - 138, canvas.height() - 28, MUTED, 1);
    canvas.text("x86-64", canvas.width() - 76, canvas.height() - 28, MUTED, 1);
    if (launcher) { paint_launcher(); }
    if (!launcher && pointer_y >= canvas.height() - PANEL) {
        const char *tip = start.contains(pointer_x, pointer_y) ? "Applications" : nullptr;
        for (int app = 0; app < APP_COUNT; ++app) {
            if (task_button(app).contains(pointer_x, pointer_y)) { tip = names[app]; }
        }
        if (tip) {
            const int w = text_width(tip, 1) + 16;
            const int x = clamp(pointer_x - w / 2, 4, canvas.width() - w - 4);
            canvas.rect({x, canvas.height() - PANEL - 26, w, 20}, INK);
            canvas.text(tip, x + 8, canvas.height() - PANEL - 20, PAPER, 1);
        }
    }
    canvas.present();
    draw_cursor();
}
bool mouse_event(const InputEvent &event) {
    static bool movement_logged = false, click_logged = false;
    if (!movement_logged && (event.dx || event.dy)) {
        movement_logged = true;
        serial_write_line("MYOS: mouse movement received");
    }
    const int old_x = pointer_x, old_y = pointer_y;
    const int old_hover = hover_target();
    pointer_x = clamp(pointer_x + event.dx, 0, canvas.width() - 1);
    pointer_y = clamp(pointer_y + event.dy, 0, canvas.height() - 1);
    const bool press = event.left && !left_down;
    if (press && !click_logged) {
        click_logged = true;
        serial_write_line("MYOS: mouse click received");
    }
    const bool release = !event.left && left_down;
    left_down = event.left;
    bool dirty = old_hover != hover_target();
    if (drag_app >= 0 && left_down) {
        Rect &r = windows[drag_app].bounds;
        r.x = clamp(pointer_x - drag_x, 0, canvas.width() - r.w);
        r.y = clamp(pointer_y - drag_y, 32, canvas.height() - PANEL - r.h - 4);
        dirty = true;
    }
    if (release && drag_app >= 0) {
        drag_app = -1;
        serial_write_line("MYOS: window moved");
    }
    if (press) {
        dirty = true;
        if (pointer_y >= canvas.height() - PANEL) {
            if (Rect{12, canvas.height() - PANEL + 6, 40, 36}.contains(pointer_x, pointer_y)) {
                launcher = !launcher;
                serial_write_line(launcher ? "MYOS: launcher opened" : "MYOS: launcher closed");
            } else {
                launcher = false;
                for (int app = 0; app < APP_COUNT; ++app) {
                    if (!task_button(app).contains(pointer_x, pointer_y)) { continue; }
                    if (active == app && visible(app)) {
                        windows[app].minimized = true;
                        next_focus();
                        serial_write_line("MYOS: window minimized");
                    } else { open_app(app); }
                }
            }
        } else if (launcher && launcher_bounds().contains(pointer_x, pointer_y)) {
            const Rect menu = launcher_bounds();
            for (int app = 0; app < APP_COUNT; ++app) {
                if (Rect{menu.x + 8, menu.y + 48 + app * 46, menu.w - 16, 42}.contains(pointer_x, pointer_y)) {
                    open_app(app);
                    break;
                }
            }
        } else {
            launcher = false;
            bool hit = false;
            for (int i = APP_COUNT - 1; i >= 0; --i) {
                const int app = order[i];
                Rect r = windows[app].bounds;
                if (!visible(app) || !r.contains(pointer_x, pointer_y)) { continue; }
                hit = true;
                focus(app);
                if (close_button(r).contains(pointer_x, pointer_y)) {
                    windows[app].open = false;
                    next_focus();
                    serial_write_line("MYOS: window closed");
                } else if (minimize_button(r).contains(pointer_x, pointer_y)) {
                    windows[app].minimized = true;
                    next_focus();
                    serial_write_line("MYOS: window minimized");
                } else if (pointer_y < r.y + TITLE) {
                    drag_app = app;
                    drag_x = pointer_x - r.x;
                    drag_y = pointer_y - r.y;
                } else if (app == Appearance) {
                    for (int style = 0; style < 2; ++style) {
                        if (swatch(r, style).contains(pointer_x, pointer_y)) {
                            wallpaper_style = style;
                            rebuild_background();
                            serial_write_line("MYOS: wallpaper changed");
                        }
                    }
                }
                break;
            }
            if (!hit) {
                active = -1;
                for (int app = 0; app < APP_COUNT; ++app) {
                    if (desktop_button(app).contains(pointer_x, pointer_y)) { open_app(app); break; }
                }
            }
        }
    }
    if (!dirty && (old_x != pointer_x || old_y != pointer_y)) {
        canvas.present({old_x, old_y, 14, 20});
        draw_cursor();
    }
    return dirty;
}
bool keyboard_event(const InputEvent &event) {
    if (event.key >= Key::F1 && event.key <= Key::F4) {
        open_app(static_cast<int>(event.key) - static_cast<int>(Key::F1));
        return true;
    }
    if (event.key == Key::Escape) {
        if (launcher) { launcher = false; }
        else if (active >= 0) { windows[active].open = false; next_focus(); serial_write_line("MYOS: window closed"); }
        return true;
    }
    if (event.key == Key::Tab) {
        for (int i = 0; i < APP_COUNT; ++i) { if (visible(order[i])) { focus(order[i]); return true; } }
        return false;
    }
    if (launcher) { return false; }
    if (active == Terminal) {
        if (event.key == Key::Character) { return command_line.append(event.character); }
        if (event.key == Key::Backspace) { return command_line.backspace(); }
        if (event.key == Key::Enter) { execute_command(); return true; }
    } else if (active == Notes) {
        bool changed = false;
        if (event.key == Key::Character) { changed = note.append(event.character); }
        if (event.key == Key::Enter) { changed = note.append('\n'); }
        if (event.key == Key::Backspace) { changed = note.backspace(); }
        if (changed && !note_edited) { note_edited = true; serial_write_line("MYOS: note edited"); }
        return changed;
    }
    return false;
}
}

namespace myos {
bool desktop_init(limine_framebuffer *framebuffer) {
    if (!canvas.init(framebuffer, scene)) { return false; }
    panic_attach(&canvas);
    const int w = canvas.width(), h = canvas.height();
    windows[Terminal].bounds = {0, 0, min(580, w - 120), min(350, h - 100)};
    windows[Notes].bounds = {0, 0, min(460, w - 120), min(360, h - 100)};
    windows[About].bounds = {0, 0, 420, 300};
    windows[Appearance].bounds = {0, 0, 400, 280};
    for (int app = 0; app < APP_COUNT; ++app) {
        Rect &r = windows[app].bounds;
        r.x = clamp((w - r.w) / 2 + (app == Terminal ? 32 : app == Notes ? -24 : 0), 0, w - r.w - 4);
        r.y = clamp((h - PANEL - r.h) / 2 + 12, 32, h - PANEL - r.h - 4);
    }
    pointer_x = w * 3 / 4;
    pointer_y = h * 3 / 4;
    devices = input_init();
    serial_write_line(devices.keyboard ? "MYOS: keyboard ready" : "MYOS: keyboard unavailable");
    serial_write_line(devices.mouse ? "MYOS: mouse ready" : "MYOS: mouse unavailable");
    rebuild_background();
    terminal_line("ShawnOS 0.3.1");
    open_app(About);
    render();
    serial_write_line("MYOS: desktop ready");
    return true;
}
[[noreturn]] void desktop_run() {
    // Clock calibration completes after desktop_init; refresh those initial readings once.
    render();
    uint64_t last_second = 0;
    bool timer_proven = false;
    for (;;) {
        InputEvent event;
        const bool input = input_poll(event);
        bool dirty = input && (event.mouse ? mouse_event(event) : keyboard_event(event));
        const uint64_t second = timer_ticks() / TIMER_HZ;
        if (second != last_second) {
            last_second = second;
            // Refresh only the unobscured uptime band, avoiding a full-frame redraw.
            int top = -1;
            for (int i = APP_COUNT - 1; i >= 0; --i) {
                if (visible(order[i])) { top = order[i]; break; }
            }
            if (!dirty && top == About) {
                const Rect r = windows[About].bounds;
                paint_about_uptime(r);
                canvas.present({r.x + 136, r.y + 270, r.w - 160, 20});
                draw_cursor();
            }
            if (!timer_proven) { timer_proven = true; serial_write_line("MYOS: timer ticked"); }
        }
        if (dirty) { render(); }
        if (!input) { idle_wait(); }
    }
}
}
