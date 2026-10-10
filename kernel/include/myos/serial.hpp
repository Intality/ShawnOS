#ifndef MYOS_SERIAL_HPP
#define MYOS_SERIAL_HPP

namespace myos {

void serial_init();
void serial_write(const char *text);
void serial_write_line(const char *text);

}

#endif

