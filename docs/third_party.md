# Third-Party Notes

MyOS uses Limine as its bootloader and speaks the Limine boot protocol.

- Limine project: <https://github.com/Limine-Bootloader/Limine>
- Limine protocol declarations: <https://github.com/Limine-Bootloader/limine-protocol>

`kernel/include/limine.h` is a minimal subset of Limine protocol declarations
needed for v0.3.0: framebuffer, memory map, HHDM, and executable command line.
The upstream Limine protocol header is 0BSD licensed.

