#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_SIZE 256

extern char *_pgmptr;

/* 2Fh signature constants */
static const unsigned SIG_BX_EG = 0x4547; /* 'EG' */
static const unsigned SIG_CX_AV = 0x4156; /* 'AV' */
static const unsigned SIG_DX_GA = 0x4741; /* 'GA' */

/* Multiplex ID for INT 2Fh */
static unsigned char MUX_ID = 0;

/* Palette sizes */
#define EGA 17
#define VGA 768

unsigned char pal_ega[EGA];
unsigned char pal_vga[VGA];

/* Logging structures */
typedef struct {
    unsigned char ah, al, bh, bl;
} LogEntry;

static LogEntry logbuf[LOG_SIZE];
static volatile unsigned log_index = 0;

/* Old interrupt handlers */
static void (__interrupt __far *old_int2f)(void);
static void (__interrupt __far *old_int10)(void);

/* Forward declarations */
void __interrupt __far int2f_handler(union INTPACK r);
void __interrupt __far int10_handler(union INTPACK r);

/* -------------------------------------------------------------
   Find a free multiplex ID (C0h–FFh)
   ------------------------------------------------------------- */
unsigned char find_free_mux_id(void)
{
    union REGS r;
    unsigned char id;

    for (id = 0xFF; id >= 0xC0; id--) {

        r.h.ah = id;
        r.h.al = 0x00; /* query function */

        int86(0x2F, &r, &r);

        /* If AX unchanged, ID is free */
        if (r.h.ah == id && r.h.al == 0x00)
            return id;
    }

    return 0; /* should never happen */
}

/* -------------------------------------------------------------
   Install TSR and hook INT 2Fh + INT 10h
   ------------------------------------------------------------- */
void install_tsr(void)
{
    unsigned endseg;

    MUX_ID = find_free_mux_id();
    if (MUX_ID == 0) {
        printf("No free multiplex IDs.\n");
        exit(1);
    }

    printf("Using multiplex ID: %02Xh\n", MUX_ID);
    printf("Installing EGA palette TSR...\n");

    /* Install INT 2Fh handler */
    old_int2f = _dos_getvect(0x2F);
    _dos_setvect(0x2F, int2f_handler);

    /* Install INT 10h handler */
    old_int10 = _dos_getvect(0x10);
    _dos_setvect(0x10, int10_handler);

    printf("TSR installed. Staying resident...\n");

    /* Stay resident */
    endseg = FP_SEG(&endseg) + ((FP_OFF(&endseg) + 15) >> 4);
    _dos_keep(0, endseg - _psp);
}

/* -------------------------------------------------------------
   Check if TSR is already installed
   ------------------------------------------------------------- */
int is_installed(unsigned char id)
{
    union REGS r;

    r.h.ah = id;
    r.h.al = 0x00; /* query */
    int86(0x2F, &r, &r);

    if (r.h.al == 0xFF &&
        r.x.bx == SIG_BX_EG &&
        r.x.cx == SIG_CX_AV &&
        r.x.dx == SIG_DX_GA)
        return 1;

    return 0;
}

/* -------------------------------------------------------------
   Logging helpers
   ------------------------------------------------------------- */
void clear_log(void)
{
    log_index = 0;
}

void log(union INTPACK r)
{
    if (log_index < LOG_SIZE) {
        logbuf[log_index].ah = r.h.ah;
        logbuf[log_index].al = r.h.al;
        logbuf[log_index].bh = r.h.bh;
        logbuf[log_index].bl = r.h.bl;
        log_index++;
    }
}

/* -------------------------------------------------------------
   INT 2Fh handler (multiplex interface)
   ------------------------------------------------------------- */
void __interrupt __far int2f_handler(union INTPACK r)
{
    if (r.h.ah != MUX_ID) {
        _chain_intr(old_int2f);
        return;
    }

    /* Query */
    if (r.h.al == 0x00) {
        r.h.al = 0xFF;
        r.x.bx = SIG_BX_EG;
        r.x.cx = SIG_CX_AV;
        r.x.dx = SIG_DX_GA;
        return;
    }

    /* Return log buffer pointer */
    if (r.h.al == 0x01) {
        r.x.di = FP_OFF(logbuf);
        r.x.es = FP_SEG(logbuf);
        r.x.si = FP_OFF(&log_index);
        r.x.ds = FP_SEG(&log_index);
        return;
    }

    /* Clear log */
    if (r.h.al == 0x02) {
        clear_log();
        return;
    }

    _chain_intr(old_int2f);
}

/* -------------------------------------------------------------
   INT 10h handler (EGA palette interception)
   ------------------------------------------------------------- */
void __interrupt __far int10_handler(union INTPACK r)
{
    unsigned char far *in_pal;

    /* EGA: INT 10h AH=10h AL=00h (set single register) */
    if (r.h.ah == 0x10 && r.h.al == 0x00) {
        r.h.bl = (r.h.bl + 1) & 0x0F; /* wrap 0–15 */
    }

    /* EGA: INT 10h AH=10h AL=02h (set all registers) */
    if (r.h.ah == 0x10 && r.h.al == 0x02) {
        in_pal = (unsigned char far*)MK_FP(r.x.es, r.x.dx);
        _fmemcpy(in_pal, pal_ega, EGA);
    }

    _chain_intr(old_int10);
}

/* -------------------------------------------------------------
   Utility: get directory of EXE
   ------------------------------------------------------------- */
void get_exe_directory(char *out)
{
    char *p;

    strcpy(out, _pgmptr);

    p = strrchr(out, '\\');
    if (p)
        *p = '\0';
}

/* -------------------------------------------------------------
   Load .pal file (17 bytes)
   ------------------------------------------------------------- */
void load_palette_file(char *filename)
{
    FILE *fp;
    char exedir[128];
    char path[160];
    size_t n;

    get_exe_directory(exedir);
    sprintf(path, "%s\\%s", exedir, filename);

    fp = fopen(path, "rb");
    if (!fp) {
        printf("Error: cannot open palette file '%s'\n", path);
        exit(1);
    }

    n = fread(pal_ega, 1, EGA, fp);
    fclose(fp);

    if (n != EGA) {
        printf("Error: palette file '%s' must be exactly 17 bytes.\n", path);
        exit(1);
    }

    printf("Loaded palette file: '%s'\n", path);
}

/* -------------------------------------------------------------
   Argument handling
   ------------------------------------------------------------- */
void handle_args(int argc, char *argv[])
{
    char palfile[64];

    if (argc < 2) {
        printf("Usage: PALEGA <file|profile>\n");
        exit(0);
    }

    strcpy(palfile, argv[1]);

    if (!strchr(palfile, '.'))
        strcat(palfile, ".pal");

    load_palette_file(palfile);
}

/* -------------------------------------------------------------
   Main
   ------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    handle_args(argc, argv);
    install_tsr();
    return 0;
}
