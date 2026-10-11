#ifndef MYOS_INPUT_HPP
#define MYOS_INPUT_HPP

#include <stdint.h>

namespace myos {

enum class Key { None, Character, Enter, Backspace, Escape, Tab, F1, F2, F3, F4 };

struct InputEvent {
    Key key = Key::None;
    char character = 0;
    int dx = 0;
    int dy = 0;
    bool mouse = false;
    bool left = false;
};

// These decoders are pure: the same code is exercised by host unit tests.
class KeyboardDecoder {
public:
    bool feed(uint8_t byte, InputEvent &event);
private:
    bool extended_ = false;
    bool left_shift_ = false;
    bool right_shift_ = false;
    bool caps_ = false;
    bool caps_down_ = false;
    int pause_remaining_ = 0;
};

class MouseDecoder {
public:
    bool feed(uint8_t byte, InputEvent &event);
private:
    uint8_t packet_[3] = {};
    int index_ = 0;
};

struct InputDevices { bool keyboard; bool mouse; };
InputDevices input_init();
bool input_poll(InputEvent &event);

}
#endif
