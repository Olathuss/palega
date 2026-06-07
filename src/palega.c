#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_SIZE 256

extern char *_pgmptr;

// 2Fh signatures
static const unsigned SIG_BX_EG = 0x4547;
static const unsigned SIG_CX_AV = 0x4156;
static const unsigned SIG_DX_GA = 0x4741;

// multiplex for 2Fh
static unsigned char MUX_ID = 0;

// palette data
#define EGA 17
#define VGA 768
unsigned char pal_ega[EGA];
unsigned char pal_vga[VGA];

typedef struct {
    unsigned char data[17];
    unsigned short tick;
    unsigned char mode;
} Palette;

typedef struct {
    unsigned char ah, al, bh, bl;
} LogEntry;

static LogEntry logbuf[LOG_SIZE];
static volatile unsigned log_index = 0;

static void (__interrupt __far *old_int2f)(void);
static void (__interrupt __far *old_int10)(void);

void __interrupt __far int2f_handler( union INTPACK r );
void __interrupt __far int10_handler( union INTPACK r );

unsigned char find_free_mux_id(void)
{
    union REGS r;
    unsigned char id;

    for (id = 0xFF; id >= 0xC0; id--) {

        r.h.ah = id;
        r.h.al = 0x00;     /* query function */
        r.x.ax = (r.x.ax); /* ensure AX starts unchanged */

        int86(0x2F, &r, &r);

        /* If AX unchanged, ID is free */
        if (r.h.ah == id && r.h.al == 0x00) {
            return id;     /* free ID found */
        }

        /* else: someone responded → try next ID */
    }

    return 0; /* no free ID found (should never happen) */
}

void install_tsr(void)
{
    unsigned endseg;

    MUX_ID = find_free_mux_id();
    if (MUX_ID == 0) {
        printf("No free multiplex IDs.\n");
        exit(1);
    }

    printf("Using multiplex ID: %02Xh\n", MUX_ID);
    printf("Installing EGA->VGA TSR...\n");
    fflush(stdout);

    /* Install INT 2Fh handler */
    old_int2f = _dos_getvect(0x2f);
    _dos_setvect(0x2f, int2f_handler);

    /* Install INT 10h handler */
    old_int10 = _dos_getvect(0x10);
    _dos_setvect(0x10, int10_handler);

    printf("TSR installed. Staying resident...\n");
    fflush(stdout);

    /* Stay resident */
    endseg = FP_SEG(&endseg) + ((FP_OFF(&endseg) + 15) >> 4);
    _dos_keep(0, endseg - _psp);
}


int is_installed(unsigned char id)
{
    union REGS r;

    r.h.ah = id;
    r.h.al = 0x00;   /* query */
    int86(0x2F, &r, &r);

    /* TSR responds with AL = 0xFF and signature in BX/CX/DX */
    if (r.h.al == 0xFF && r.x.bx == SIG_BX_EG && r.x.cx == SIG_CX_AV && r.x.dx == SIG_DX_GA) {
        return 1; /* installed */
    }

    return 0;
}

void clear_log()
{
    log_index = 0;
}

void log( union INTPACK r )
{
    if (log_index < LOG_SIZE) {
        logbuf[log_index].ah = r.h.ah;
        logbuf[log_index].al = r.h.al;
        logbuf[log_index].bh = r.h.bh;
        logbuf[log_index].bl = r.h.bl;
        log_index++;
    }
}

void __interrupt __far int2f_handler( union INTPACK r )
{
    if (r.h.ah != MUX_ID) {
        _chain_intr(old_int2f);
        return;
    }

    if (r.h.al == 0x00) {
        r.h.al = 0xFF;
        r.x.bx = SIG_BX_EG;
        r.x.cx = SIG_CX_AV;
        r.x.dx = SIG_DX_GA;
        return;
    }

    if (r.h.al == 0x01) {
        r.x.di = FP_OFF(logbuf);
        r.x.es = FP_SEG(logbuf);
        r.x.si = FP_OFF(&log_index);
        r.x.ds = FP_SEG(&log_index);
        return;
    }

    if (r.h.al == 0x02) {
        clear_log();
        return;
    }

    _chain_intr(old_int2f);
}

void __interrupt __far int10_handler(union INTPACK r)
{
    unsigned char far* in_pal;
    int i = 0;

    /* EGA palette write: INT 10h AH=10h AL=00h */
    if (r.h.ah == 0x10 && r.h.al == 0x00) {
        r.h.bl = (r.h.bl + 1) & 0x0F;   /* wrap 0–15 */
        /* fall through to chain */
    }

    if (r.h.ah == 0x10 && r.h.al == 0x02) {
        in_pal = (unsigned char far*)MK_FP(r.x.es, r.x.dx);

        _fmemcpy(in_pal, pal_ega, EGA);
    }

    /* VGA DAC write: INT 10h AH=10h AL=10h */
    else if (r.h.ah == 0x10 && r.h.al == 0x10) {
        unsigned idx = r.x.bx & 0xFF;

        r.h.dh = 0;//pal_vga[idx*3 + 0];  // red
        r.h.ch = 1;//pal_vga[idx*3 + 1];  // green
        r.h.cl = 5;//pal_vga[idx*3 + 2];  // blue
    }

    _chain_intr(old_int10);
}

void get_exe_directory(char *out)
{
    char *p;

    strcpy(out, _pgmptr);

    // Find last backslash
    p = strrchr(out, '\\');
    if (p) {
        *p = '\0';   // terminate string BEFORE filename
    }
}

void load_palette_file(char* filename)
{
    FILE *fp;
    char exedir[128];
    char path[160];
    char *p;
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

void handle_args(int argc, char *argv[]) {
    char palfile[64];

    if (argc < 2) {
        printf("Usage: PALETTESWAP <file|profile>\n");
        exit(0);
    }

    strcpy(palfile, argv[1]);

    if (strchr(palfile, '.') == NULL) {
        strcat(palfile, ".pal");
    }

    load_palette_file(palfile);
}

int main(int argc, char *argv[])
{
    handle_args(argc, argv);
    install_tsr();
    return 0;
}
