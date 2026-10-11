#include <myos/input.hpp>

namespace myos {

bool KeyboardDecoder::feed(uint8_t byte, InputEvent &event) {
    event = {};
    if (pause_remaining_ > 0) { --pause_remaining_; return false; }
    if (byte == 0xe1) { pause_remaining_ = 5; return false; }
    if (byte == 0xe0) { extended_ = true; return false; }
    const bool extended = extended_;
    extended_ = false;
    const bool released = (byte & 0x80) != 0;
    const uint8_t code = byte & 0x7f;
    if (!extended && code == 0x2a) { left_shift_ = !released; return false; }
    if (!extended && code == 0x36) { right_shift_ = !released; return false; }
    if (!extended && code == 0x3a) {
        if (!released && !caps_down_) { caps_ = !caps_; }
        caps_down_ = !released;
        return false;
    }
    if (released) { return false; }
    if (code == 0x1c) { event.key = Key::Enter; return true; }
    if (extended) { return false; }
    switch (code) {
    case 0x01: event.key = Key::Escape; return true;
    case 0x0e: event.key = Key::Backspace; return true;
    case 0x0f: event.key = Key::Tab; return true;
    case 0x3b: event.key = Key::F1; return true;
    case 0x3c: event.key = Key::F2; return true;
    case 0x3d: event.key = Key::F3; return true;
    case 0x3e: event.key = Key::F4; return true;
    default: break;
    }
    static constexpr char normal[] =
        "\0\0" "1234567890-=\0\0" "qwertyuiop[]\0\0" "asdfghjkl;'`\0\\"
        "zxcvbnm,./\0*\0 \0";
    static constexpr char shifted[] =
        "\0\0" "!@#$%^&*()_+\0\0" "QWERTYUIOP{}\0\0" "ASDFGHJKL:\"~\0|"
        "ZXCVBNM<>?\0*\0 \0";
    if (code >= sizeof(normal) - 1 || normal[code] == 0) { return false; }
    const bool shift = left_shift_ || right_shift_;
    const bool letter = normal[code] >= 'a' && normal[code] <= 'z';
    event.character = (letter ? shift != caps_ : shift) ? shifted[code] : normal[code];
    event.key = Key::Character;
    return true;
}

bool MouseDecoder::feed(uint8_t byte, InputEvent &event) {
    event = {};
    if (index_ == 0 && (byte & 0x08) == 0) { return false; }
    packet_[index_++] = byte;
    if (index_ < 3) { return false; }
    index_ = 0;
    event.mouse = true;
    event.left = (packet_[0] & 1) != 0;
    // Mouse deltas are signed 9-bit values, not int8_t values.
    if ((packet_[0] & 0xc0) == 0) {
        event.dx = static_cast<int>(packet_[1]) - ((packet_[0] & 0x10) ? 256 : 0);
        event.dy = -(static_cast<int>(packet_[2]) - ((packet_[0] & 0x20) ? 256 : 0));
    }
    return true;
}

}
