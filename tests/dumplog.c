#include <dos.h>
#include <stdio.h>

#define LOG_SIZE 256

// 2Fh signatures
static const unsigned SIG_BX_EG = 0x4547;
static const unsigned SIG_CX_AV = 0x4156;
static const unsigned SIG_DX_GA = 0x4741;

typedef struct {
    unsigned char ah, al, bh, bl;
} LogEntry;

extern unsigned _psp;

unsigned char find_egavga_mux_id(void)
{
    union REGS r;
    unsigned char id;

    for (id = 0xFF; id >= 0xC0; id--) {

        r.h.ah = id;
        r.h.al = 0x00;   /* query */
        int86(0x2F, &r, &r);

        if (r.h.al == 0xFF &&
            r.x.bx == SIG_BX_EG &&
            r.x.cx == SIG_CX_AV &&
            r.x.dx == SIG_DX_GA) {

            return id;   /* TSR found */
        }
    }

    return 0;   /* not found */
}

void main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned char mux;
    LogEntry far *logbuf;
    unsigned far *log_idx;
    unsigned count, i;

    mux = find_egavga_mux_id();
    if (mux == 0) {
        printf("TSR not found.\n");
        return;
    }

    /* Request pointers */
    r.h.ah = mux;
    r.h.al = 0x01;   /* get pointers */
    int86x(0x2F, &r, &r, &s);

    logbuf = (LogEntry far *)MK_FP(s.es, r.x.di);
    log_idx = (unsigned far *)MK_FP(s.ds, r.x.si);

    count = *log_idx;
    if (count > LOG_SIZE)
        count = LOG_SIZE;

    for (i = 0; i < count; i++) {
        printf("%02X %02X %02X %02X ",
            logbuf[i].ah,
            logbuf[i].al,
            logbuf[i].bh,
            logbuf[i].bl);
        if (i % 5 == 0) {
            printf("\n");
        }
    }

    /* Clear log */
    r.h.ah = mux;
    r.h.al = 0x02;   /* clear log */
    int86x(0x2F, &r, &r, &s);
    printf("Log cleared.\n");
}
