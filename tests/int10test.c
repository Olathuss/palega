#include <dos.h>

void main(void)
{
    union REGS r;

    /* Set video mode 3 (80x25 text) */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    /* Print a character using BIOS */
    r.h.ah = 0x0E;
    r.h.al = 'A';
    r.h.bh = 0x00;
    r.h.bl = 0x07;
    int86(0x10, &r, &r);
}
