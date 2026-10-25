/* Copie de fichiers sur un disque dur Amiga (.hdf) : voir hdtools.h. Port en C de ffs.js, lha.js, readAdf et de la
 * lecture des .zip de la version web, pour les applications Android, Mac et PC. */
#define _FILE_OFFSET_BITS 64
#include "hdtools.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#undef fseeko
#undef ftello
#define fseeko _fseeki64
#define ftello _ftelli64
#define off_t long long
#endif

#define BS 512
#define HT 72
#define T_HEADER 2
#define T_LIST 16
#define T_DATA 8
#define ST_ROOT 1
#define ST_USERDIR 2
#define ST_FILE 0xfffffffdu

/* ---------- utilitaires ---------- */
static uint32_t g32(const uint8_t *b, int o) { return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3]; }
static void p32(uint8_t *b, int o, uint32_t v) { b[o] = v >> 24; b[o + 1] = v >> 16; b[o + 2] = v >> 8; b[o + 3] = v; }
static uint16_t l16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint32_t l32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

typedef struct { char *s; int len, cap; } Str;
static void sadd(Str *s, const char *fmt, ...) {
    va_list a;
    for (;;) {
        int room = s->cap - s->len;
        va_start(a, fmt);
        int n = vsnprintf(s->s ? s->s + s->len : NULL, s->s ? (size_t)room : 0, fmt, a);
        va_end(a);
        if (n < 0) return;
        if (s->s && n < room) { s->len += n; return; }
        s->cap = s->cap * 2 + n + 256;
        s->s = (char *)realloc(s->s, (size_t)s->cap);
    }
}

/* Noms : UTF-8 (système) ↔ Latin-1 (Amiga) */
static char *toLatin1(const char *u) {
    size_t n = strlen(u);
    char *r = (char *)malloc(n + 1), *o = r;
    for (const unsigned char *p = (const unsigned char *)u; *p;) {
        unsigned c = *p++;
        if (c >= 0x80) {
            int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
            c &= 0x3f >> extra;
            while (extra-- > 0 && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 0x3f);
            if (c > 255) c = '?';
        }
        *o++ = (char)c;
    }
    *o = 0;
    return r;
}
static void latin1To(Str *s, const char *l) {
    for (const unsigned char *p = (const unsigned char *)l; *p; p++) {
        if (*p < 0x80) sadd(s, "%c", *p);
        else sadd(s, "%c%c", 0xc0 | *p >> 6, 0x80 | (*p & 0x3f));
    }
}

typedef struct { uint32_t d, m, t; } ADate;
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
/* Heure locale, comme l'horloge de l'Amiga */
static ADate fromUnix(time_t t) {
    ADate a = {0, 0, 0};
    struct tm tm;
#ifdef _WIN32
    if (localtime_s(&tm, &t)) return a;
#else
    if (!localtime_r(&t, &tm)) return a;
#endif
    long days = daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) - daysFromCivil(1978, 1, 1);
    if (days < 0) return a;
    a.d = (uint32_t)days; a.m = (uint32_t)(tm.tm_hour * 60 + tm.tm_min); a.t = (uint32_t)tm.tm_sec * 50;
    return a;
}
static ADate fromDos(uint32_t v) {
    unsigned tim = v & 0xffff, dat = v >> 16;
    ADate a = {0, 0, 0};
    long days = daysFromCivil(1980 + (dat >> 9), (dat >> 5) & 15, dat & 31) - daysFromCivil(1978, 1, 1);
    if (days >= 0) { a.d = (uint32_t)days; a.m = (tim >> 11) * 60 + ((tim >> 5) & 63); a.t = (tim & 31) * 2 * 50; }
    return a;
}

/* Fichier lu en entier */
static uint8_t *readFile(const char *path, size_t *len) {
#ifdef _WIN32
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return NULL;
    FILE *f = _wfopen(w, L"rb");
#else
    FILE *f = fopen(path, "rb");
#endif
    if (!f) return NULL;
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long long n = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (n < 0 || n > (1ll << 31)) { fclose(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}
static time_t mtimeOf(const char *path) {
#ifdef _WIN32
    struct _stat64 st;
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) || _wstat64(w, &st)) return time(NULL);
#else
    struct stat st;
    if (stat(path, &st)) return time(NULL);
#endif
    return st.st_mtime;
}

/* ---------- disque : blocs lus à la demande, gardés ; seules les données vont directement sur le disque ---------- */
typedef struct { uint64_t n; uint8_t *buf; uint8_t dirty, hdr, map; } Ent;
typedef struct {
    FILE *f;
    uint64_t blocks;
    Ent *e; size_t cap, count;   /* table de hachage (adressage ouvert) */
    jmp_buf *fail;
    char why[256];
} Disk;

static void fail(Disk *d, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt); vsnprintf(d->why, sizeof d->why, fmt, a); va_end(a);
    longjmp(*d->fail, 1);
}
static Ent *slot(Disk *d, uint64_t n) {
    if (d->count * 2 >= d->cap) {
        size_t oc = d->cap;
        Ent *old = d->e;
        d->cap = oc ? oc * 2 : 1024;
        d->e = (Ent *)calloc(d->cap, sizeof(Ent));
        d->count = 0;
        for (size_t i = 0; i < oc; i++) if (old[i].buf) { Ent *s = slot(d, old[i].n); *s = old[i]; d->count++; }
        free(old);
    }
    size_t i = (size_t)(n * 2654435761u) & (d->cap - 1);
    while (d->e[i].buf && d->e[i].n != n) i = (i + 1) & (d->cap - 1);
    return &d->e[i];
}
static uint8_t *blk(Disk *d, uint64_t n) {
    if (n >= d->blocks) fail(d, "bloc %llu hors du disque", (unsigned long long)n);
    Ent *s = slot(d, n);
    if (!s->buf) {
        s->n = n; s->buf = (uint8_t *)calloc(1, BS); d->count++;
        if (fseeko(d->f, (off_t)(n * BS), SEEK_SET) != 0 || fread(s->buf, 1, BS, d->f) != BS) fail(d, "lecture du disque impossible");
    }
    return s->buf;
}
static void touch(Disk *d, uint64_t n, int hdr, int map) { blk(d, n); Ent *s = slot(d, n); s->dirty = 1; s->hdr |= hdr; s->map |= map; }
static int diskOpen(Disk *d, const char *path, int write) {
    memset(d, 0, sizeof *d);
#ifdef _WIN32
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return 0;
    d->f = _wfopen(w, write ? L"r+b" : L"rb");
#else
    d->f = fopen(path, write ? "r+b" : "rb");
#endif
    if (!d->f) return 0;
    fseeko(d->f, 0, SEEK_END);
    d->blocks = (uint64_t)ftello(d->f) / BS;
    return d->blocks > 2;
}
static void diskClose(Disk *d) {
    for (size_t i = 0; i < d->cap; i++) free(d->e[i].buf);
    free(d->e);
    if (d->f) fclose(d->f);
    d->f = NULL; d->e = NULL;
}
static void checksum(uint8_t *b, int at) {
    p32(b, at, 0);
    uint32_t s = 0;
    for (int i = 0; i < BS; i += 4) s += g32(b, i);
    p32(b, at, (uint32_t)-s);
}
static int sumOk(const uint8_t *b) { uint32_t s = 0; for (int i = 0; i < BS; i += 4) s += g32(b, i); return s == 0; }

/* ---------- partitions ---------- */
typedef struct { char name[32], volume[32], fs[32], reason[96]; uint64_t start, blocks; uint32_t reserved, dosType, root; int ok; } Part;
static const char *FS_NAMES[] = {"OFS", "FFS", "OFS international", "FFS international", "OFS + cache", "FFS + cache", "OFS noms longs", "FFS noms longs"};
static void fsName(uint32_t t, char *out) {
    if ((t >> 8) == 0x444f53 && (t & 255) < 8) { strcpy(out, FS_NAMES[t & 255]); return; }
    char a = (char)(t >> 24), b = (char)(t >> 16), c = (char)(t >> 8);
    if (a >= 32 && a < 127 && b >= 32 && b < 127 && c >= 32 && c < 127) snprintf(out, 32, "%c%c%c\\%u", a, b, c, t & 255);
    else strcpy(out, "inconnu");
}
static void bcpl(const uint8_t *b, int o, char *out, int max) { int n = b[o] < max ? b[o] : max; memcpy(out, b + o + 1, (size_t)n); out[n] = 0; }
static void describe(Disk *d, Part *p) {
    uint32_t t = g32(blk(d, p->start), 0);
    p->dosType = t; fsName(t, p->fs); p->ok = 0;
    if ((t >> 8) != 0x444f53) { snprintf(p->reason, sizeof p->reason, t == 0 || t == 0x42414400 ? "non formatée" : "système %s (seuls OFS et FFS sont modifiables)", p->fs); return; }
    if ((t & 255) > 3) { snprintf(p->reason, sizeof p->reason, (t & 255) < 6 ? "FFS avec cache de dossiers (non géré)" : "FFS à noms longs (non géré)"); return; }
    p->root = (uint32_t)((p->blocks - 1 + p->reserved) / 2);
    const uint8_t *r = blk(d, p->start + p->root);
    if (g32(r, 0) != T_HEADER || g32(r, 508) != ST_ROOT || !sumOk(r)) { strcpy(p->reason, "bloc racine introuvable"); return; }
    bcpl(r, 432, p->volume, 30);
    if (g32(r, 312) != 0xffffffff) { strcpy(p->reason, "disque à valider : démarrez une fois l'Amiga avec ce disque"); return; }
    p->ok = 1;
}
static int listParts(Disk *d, Part *parts, int max) {
    int n = 0;
    int64_t rdsk = -1;
    for (uint64_t b = 0; b < 16 && b < d->blocks; b++) if (g32(blk(d, b), 0) == 0x5244534b) { rdsk = (int64_t)b; break; }
    if (rdsk < 0) {
        memset(&parts[0], 0, sizeof(Part));
        strcpy(parts[0].name, "DH0"); parts[0].blocks = d->blocks; parts[0].reserved = 2;
        describe(d, &parts[0]);
        return 1;
    }
    uint32_t seen[64];
    int ns = 0;
    for (uint32_t pb = g32(blk(d, (uint64_t)rdsk), 28); pb != 0xffffffff && pb < d->blocks && n < max && ns < 64;) {
        int dup = 0;
        for (int i = 0; i < ns; i++) dup |= seen[i] == pb;
        if (dup) break;
        seen[ns++] = pb;
        const uint8_t *p = blk(d, pb);
        if (g32(p, 0) != 0x50415254) break;
#define ENV(i) g32(p, 128 + (i) * 4)
        uint64_t cyl = (uint64_t)ENV(3) * ENV(5);
        Part *q = &parts[n++];
        memset(q, 0, sizeof *q);
        bcpl(p, 36, q->name, 30);
        q->start = ENV(9) * cyl; q->blocks = (ENV(10) - ENV(9) + 1) * cyl; q->reserved = ENV(6); q->dosType = ENV(16);
        if (ENV(1) != 128 || (ENV(4) ? ENV(4) : 1) != 1) { fsName(q->dosType, q->fs); strcpy(q->reason, "taille de bloc différente de 512 octets"); }
        else describe(d, q);
        pb = g32(p, 16);
#undef ENV
    }
    return n;
}

/* ---------- volume OFS / FFS ---------- */
typedef struct {
    Disk *d; Part *p;
    int ofs, intl;
    uint32_t *maps; int nmaps;
    uint64_t cursor;
} Vol;
static uint8_t *vb(Vol *v, uint64_t n) { return blk(v->d, v->p->start + n); }
static void mark(Vol *v, uint64_t n) { touch(v->d, v->p->start + n, 1, 0); }
static void volOpen(Vol *v, Disk *d, Part *p) {
    memset(v, 0, sizeof *v);
    v->d = d; v->p = p; v->ofs = !(p->dosType & 1); v->intl = (p->dosType & 255) >= 2;
    int cap = 64;
    v->maps = (uint32_t *)malloc(cap * sizeof(uint32_t));
    const uint8_t *r = vb(v, p->root);
    for (int i = 0; i < 25; i++) { uint32_t m = g32(r, 316 + i * 4); if (m) v->maps[v->nmaps++] = m; }
    for (uint32_t e = g32(r, 416), guard = 0; e && guard < 100000; guard++) {
        const uint8_t *b = vb(v, e);
        for (int i = 0; i < 127; i++) {
            uint32_t m = g32(b, i * 4);
            if (!m) continue;
            if (v->nmaps == cap) { cap *= 2; v->maps = (uint32_t *)realloc(v->maps, cap * sizeof(uint32_t)); }
            v->maps[v->nmaps++] = m;
        }
        e = g32(b, 508);
    }
    v->cursor = p->root;
}
static void bitOf(Vol *v, uint64_t n, uint32_t *mb, int *off, int *bit) {
    uint64_t idx = n - v->p->reserved, m = idx / 4064, r = idx % 4064;
    if (m >= (uint64_t)v->nmaps) fail(v->d, "carte des blocs libres incomplète");
    *mb = v->maps[m]; *off = 4 + (int)(r >> 5) * 4; *bit = (int)(r & 31);
}
static int isFree(Vol *v, uint64_t n) { uint32_t mb; int off, bit; bitOf(v, n, &mb, &off, &bit); return (g32(vb(v, mb), off) >> bit) & 1; }
static void setFree(Vol *v, uint64_t n, int fr) {
    uint32_t mb; int off, bit;
    bitOf(v, n, &mb, &off, &bit);
    uint8_t *b = vb(v, mb);
    uint32_t w = g32(b, off);
    w = fr ? (w | 1u << bit) : (w & ~(1u << bit));
    p32(b, off, w);
    touch(v->d, v->p->start + mb, 0, 1);
}
static uint64_t freeCount(Vol *v) {
    uint64_t n = 0, total = v->p->blocks - v->p->reserved;
    for (int m = 0; m < v->nmaps; m++) {
        const uint8_t *b = vb(v, v->maps[m]);
        for (int w = 0; w < 127; w++) {
            uint64_t first = (uint64_t)m * 4064 + (uint64_t)w * 32;
            if (first >= total) break;
            uint32_t x = g32(b, 4 + w * 4);
            if (first + 32 > total) x &= (uint32_t)((1ull << (total - first)) - 1);
            while (x) { x &= x - 1; n++; }
        }
    }
    return n;
}
static uint64_t alloc(Vol *v) {
    uint64_t lo = v->p->reserved, hi = v->p->blocks;
    for (int pass = 0; pass < 2; pass++) {
        for (uint64_t n = pass ? lo : v->cursor; n < hi; n++) {
            uint32_t mb; int off, bit;
            bitOf(v, n, &mb, &off, &bit);
            if (bit == 0 && g32(vb(v, mb), off) == 0) { n += 31; continue; }
            if (isFree(v, n)) { setFree(v, n, 0); v->cursor = n + 1; return n; }
        }
    }
    fail(v->d, "disque plein");
    return 0;
}
static int up(Vol *v, int c) { return (c >= 97 && c <= 122) || (v->intl && c >= 224 && c <= 254 && c != 247) ? c - 32 : c; }
static int hashName(Vol *v, const char *name) {
    uint32_t h = (uint32_t)strlen(name);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) h = (h * 13 + (uint32_t)up(v, *p)) & 0x7ff;
    return (int)(h % HT);
}
static int sameName(Vol *v, const char *a, const char *b) {
    if (strlen(a) != strlen(b)) return 0;
    for (; *a; a++, b++) if (up(v, (unsigned char)*a) != up(v, (unsigned char)*b)) return 0;
    return 1;
}
static uint32_t findEntry(Vol *v, uint32_t dir, const char *name) {
    char nm[32];
    for (uint32_t k = g32(vb(v, dir), 24 + hashName(v, name) * 4), guard = 0; k && guard < 100000; guard++) {
        const uint8_t *b = vb(v, k);
        bcpl(b, 432, nm, 30);
        if (sameName(v, nm, name)) return k;
        k = g32(b, 496);
    }
    return 0;
}
static void linkEntry(Vol *v, uint32_t dir, uint32_t k, const char *name) {
    uint8_t *d = vb(v, dir);
    int sl = 24 + hashName(v, name) * 4;
    uint32_t prev = 0, cur = g32(d, sl);
    while (cur && cur < k) { prev = cur; cur = g32(vb(v, cur), 496); }
    p32(vb(v, k), 496, cur);
    if (prev) { p32(vb(v, prev), 496, k); mark(v, prev); }
    else { p32(vb(v, dir), sl, k); mark(v, dir); }
}
static void unlinkEntry(Vol *v, uint32_t dir, uint32_t k) {
    char nm[32];
    bcpl(vb(v, k), 432, nm, 30);
    int sl = 24 + hashName(v, nm) * 4;
    uint32_t next = g32(vb(v, k), 496);
    if (g32(vb(v, dir), sl) == k) { p32(vb(v, dir), sl, next); mark(v, dir); return; }
    for (uint32_t c = g32(vb(v, dir), sl), guard = 0; c && guard < 100000; guard++) {
        uint32_t nn = g32(vb(v, c), 496);
        if (nn == k) { p32(vb(v, c), 496, next); mark(v, c); return; }
        c = nn;
    }
}
static void putName(uint8_t *b, int o, const char *s, int max) { int n = (int)strlen(s); if (n > max) n = max; b[o] = (uint8_t)n; memcpy(b + o + 1, s, (size_t)n); }
static void stamp(Vol *v, uint32_t k, ADate a) { uint8_t *b = vb(v, k); p32(b, 420, a.d); p32(b, 424, a.m); p32(b, 428, a.t); mark(v, k); }
static uint32_t mkdirEntry(Vol *v, uint32_t parent, const char *name, ADate a) {
    uint32_t k = (uint32_t)alloc(v);
    uint8_t *b = vb(v, k);
    memset(b, 0, BS);
    p32(b, 0, T_HEADER); p32(b, 4, k);
    putName(b, 432, name, 30);
    p32(b, 500, parent); p32(b, 508, ST_USERDIR);
    mark(v, k); stamp(v, k, a);
    linkEntry(v, parent, k, name);
    stamp(v, parent, fromUnix(time(NULL)));
    return k;
}
/* Dossier (créé au besoin) pour un chemin « a/b/c » */
static uint32_t dirPath(Vol *v, const char *path) {
    uint32_t cur = v->p->root;
    char part[64];
    const char *s = path;
    while (*s) {
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n) {
            if (n > 30) n = 30;
            memcpy(part, s, n); part[n] = 0;
            uint32_t k = findEntry(v, cur, part);
            if (k) {
                if (g32(vb(v, k), 508) != ST_USERDIR) { Str t = {0}; latin1To(&t, part); fail(v->d, "« %s » existe déjà et n'est pas un dossier", t.s); }
                cur = k;
            } else cur = mkdirEntry(v, cur, part, fromUnix(time(NULL)));
        }
        if (!e) break;
        s = e + 1;
    }
    return cur;
}
static uint64_t blocksFor(Vol *v, size_t size) {
    uint64_t data = (size + (v->ofs ? 487 : BS - 1)) / (v->ofs ? 488 : BS);
    return 1 + data + (data > HT ? (data + HT - 1) / HT - 1 : 0);
}
static void removeFile(Vol *v, uint32_t dir, uint32_t k) {
    if (g32(vb(v, k), 508) != ST_FILE) { char nm[32]; bcpl(vb(v, k), 432, nm, 30); Str t = {0}; latin1To(&t, nm); fail(v->d, "« %s » est un dossier", t.s); }
    unlinkEntry(v, dir, k);
    for (uint32_t b = k, guard = 0; b && guard < 1000000; guard++) {
        const uint8_t *x = vb(v, b);
        uint32_t cnt = g32(x, 8), next = g32(x, 504);
        for (uint32_t i = 0; i < cnt && i < HT; i++) { uint32_t dd = g32(vb(v, b), 308 - (int)i * 4); if (dd) setFree(v, dd, 1); }
        setFree(v, b, 1);
        b = next;
    }
}
static void writeData(Vol *v, uint64_t n, const uint8_t *buf) {
    Disk *d = v->d;
    uint64_t a = v->p->start + n;
    Ent *s = slot(d, a);
    if (s->buf) { memcpy(s->buf, buf, BS); s->dirty = 1; return; }
    if (fseeko(d->f, (off_t)(a * BS), SEEK_SET) != 0 || fwrite(buf, 1, BS, d->f) != BS) fail(d, "écriture sur le disque impossible");
}
static void writeFileEntry(Vol *v, uint32_t dir, const char *name, const uint8_t *data, size_t len, ADate a, const char *comment) {
    uint32_t old = findEntry(v, dir, name);
    if (old) removeFile(v, dir, old);
    size_t per = v->ofs ? 488 : BS, count = (len + per - 1) / per;
    uint32_t hdr = (uint32_t)alloc(v);
    uint32_t *blocks = (uint32_t *)malloc((count ? count : 1) * sizeof(uint32_t));
    for (size_t i = 0; i < count; i++) blocks[i] = (uint32_t)alloc(v);
    uint8_t b[BS];
    for (size_t i = 0; i < count; i++) {
        memset(b, 0, BS);
        size_t from = i * per, n = len - from < per ? len - from : per;
        if (v->ofs) {
            p32(b, 0, T_DATA); p32(b, 4, hdr); p32(b, 8, (uint32_t)i + 1); p32(b, 12, (uint32_t)n);
            p32(b, 16, i + 1 < count ? blocks[i + 1] : 0);
            memcpy(b + 24, data + from, n); checksum(b, 20);
        } else memcpy(b, data + from, n);
        writeData(v, blocks[i], b);
    }
    size_t nchain = count > HT ? (count + HT - 1) / HT : 1;
    uint32_t *chain = (uint32_t *)malloc(nchain * sizeof(uint32_t));
    chain[0] = hdr;
    for (size_t c = 1; c < nchain; c++) chain[c] = (uint32_t)alloc(v);
    for (size_t c = 0; c < nchain; c++) {
        uint8_t *x = vb(v, chain[c]);
        memset(x, 0, BS);
        size_t first = c * HT, cnt = count - first < HT ? count - first : HT;
        if (count == 0) cnt = 0;
        p32(x, 0, c ? T_LIST : T_HEADER); p32(x, 4, chain[c]); p32(x, 8, (uint32_t)cnt);
        for (size_t i = 0; i < cnt; i++) p32(x, 308 - (int)i * 4, blocks[first + i]);
        p32(x, 500, c ? hdr : dir); p32(x, 504, c + 1 < nchain ? chain[c + 1] : 0); p32(x, 508, ST_FILE);
        if (!c) {
            p32(x, 16, count ? blocks[0] : 0); p32(x, 324, (uint32_t)len);
            if (comment && *comment) putName(x, 328, comment, 79);
            putName(x, 432, name, 30);
            p32(x, 420, a.d); p32(x, 424, a.m); p32(x, 428, a.t);
        }
        mark(v, chain[c]);
    }
    free(chain); free(blocks);
    linkEntry(v, dir, hdr, name);
    stamp(v, dir, fromUnix(time(NULL)));
}
/* Sommes de contrôle, date du volume, puis écriture des blocs modifiés */
static void volFinish(Vol *v) {
    ADate now = fromUnix(time(NULL));
    uint8_t *r = vb(v, v->p->root);
    p32(r, 472, now.d); p32(r, 476, now.m); p32(r, 480, now.t);
    mark(v, v->p->root);
    Disk *d = v->d;
    for (size_t i = 0; i < d->cap; i++) {
        Ent *e = &d->e[i];
        if (!e->buf || !e->dirty) continue;
        if (e->map) checksum(e->buf, 0);
        else if (e->hdr) checksum(e->buf, 20);
    }
    for (size_t i = 0; i < d->cap; i++) {
        Ent *e = &d->e[i];
        if (!e->buf || !e->dirty) continue;
        if (fseeko(d->f, (off_t)(e->n * BS), SEEK_SET) != 0 || fwrite(e->buf, 1, BS, d->f) != BS) fail(d, "écriture sur le disque impossible");
        e->dirty = 0;
    }
    fflush(d->f);
}

/* ---------- éléments à copier ---------- */
typedef struct { char *path; int dir; uint8_t *data; size_t len; ADate date; char *comment; } Item;
typedef struct { Item *v; int n, cap; } Items;
static void addItem(Items *it, const char *path, int dir, uint8_t *data, size_t len, ADate date, const char *comment) {
    if (it->n == it->cap) { it->cap = it->cap * 2 + 64; it->v = (Item *)realloc(it->v, (size_t)it->cap * sizeof(Item)); }
    Item *x = &it->v[it->n++];
    x->path = strdup(path); x->dir = dir; x->data = data; x->len = len; x->date = date; x->comment = comment ? strdup(comment) : NULL;
}
static void freeItems(Items *it) {
    for (int i = 0; i < it->n; i++) { free(it->v[i].path); free(it->v[i].data); free(it->v[i].comment); }
    free(it->v);
}

/* ---------- LhA : -lh0-, -lh4- à -lh7- (algorithme de LHa pour UNIX) ---------- */
typedef struct {
    const uint8_t *src; size_t slen, pos;
    unsigned bitbuf, subbitbuf, bitcount;
    uint16_t left[1019], right[1019];
    uint8_t cLen[510], ptLen[19 + 17];
    uint16_t cTable[4096], ptTable[256];
    int bad;
} Lzh;
static void fillbuf(Lzh *z, unsigned n) {
    while (n > z->bitcount) {
        n -= z->bitcount;
        z->bitbuf = ((z->bitbuf << z->bitcount) + (z->subbitbuf >> (8 - z->bitcount))) & 0xffff;
        z->subbitbuf = z->pos < z->slen ? z->src[z->pos++] : 0;
        z->bitcount = 8;
    }
    z->bitcount -= n;
    z->bitbuf = ((z->bitbuf << n) + (z->subbitbuf >> (8 - n))) & 0xffff;
    z->subbitbuf = (z->subbitbuf << n) & 0xff;
}
static unsigned getbits(Lzh *z, unsigned n) { unsigned x = z->bitbuf >> (16 - n); fillbuf(z, n); return x; }
static void makeTable(Lzh *z, int nchar, const uint8_t *bitlen, int tablebits, uint16_t *table, int tableSize) {
    uint16_t count[17] = {0}; uint32_t weight[17] = {0}, start[18] = {0};
    for (int i = 0; i < nchar; i++) if (bitlen[i] <= 16) count[bitlen[i]]++;
    for (int i = 1; i <= 16; i++) start[i + 1] = start[i] + ((uint32_t)count[i] << (16 - i));
    if (start[17] != 0x10000) { z->bad = 1; return; }
    int jut = 16 - tablebits;
    for (int i = 1; i <= tablebits; i++) { start[i] >>= jut; weight[i] = 1u << (tablebits - i); }
    for (int i = tablebits + 1; i <= 16; i++) weight[i] = 1u << (16 - i);
    uint32_t i = start[tablebits + 1] >> jut;
    if (i != (0x10000u >> jut)) { uint32_t k = 1u << tablebits; while (i != k && i < (uint32_t)tableSize) table[i++] = 0; }
    int avail = nchar;
    uint32_t mask = 1u << (15 - tablebits);
    for (int ch = 0; ch < nchar; ch++) {
        int len = bitlen[ch];
        if (len == 0) continue;
        uint32_t k = start[len], next = k + weight[len];
        if (len <= tablebits) { for (uint32_t j = k; j < next && j < (uint32_t)tableSize; j++) table[j] = (uint16_t)ch; }
        else {
            uint16_t *p = &table[k >> jut];
            for (int n = len - tablebits; n > 0; n--) {
                if (*p == 0) { if (avail >= 1019) { z->bad = 1; return; } z->right[avail] = z->left[avail] = 0; *p = (uint16_t)avail++; }
                p = (k & mask) ? &z->right[*p] : &z->left[*p];
                k <<= 1;
            }
            *p = (uint16_t)ch;
        }
        start[len] = next;
    }
}
static void readPtLen(Lzh *z, int nn, int nbit, int special) {
    int n = (int)getbits(z, (unsigned)nbit);
    if (n == 0) { unsigned c = getbits(z, (unsigned)nbit); memset(z->ptLen, 0, (size_t)nn); for (int i = 0; i < 256; i++) z->ptTable[i] = (uint16_t)c; return; }
    int i = 0;
    while (i < n && i < nn) {
        unsigned c = z->bitbuf >> 13;
        if (c == 7) { unsigned m = 1u << 12; while (m & z->bitbuf) { m >>= 1; c++; } }
        fillbuf(z, c < 7 ? 3 : c - 3);
        z->ptLen[i++] = (uint8_t)c;
        if (i == special) { int zz = (int)getbits(z, 2); while (--zz >= 0 && i < nn) z->ptLen[i++] = 0; }
    }
    while (i < nn) z->ptLen[i++] = 0;
    makeTable(z, nn, z->ptLen, 8, z->ptTable, 256);
}
static void readCLen(Lzh *z) {
    int n = (int)getbits(z, 9);
    if (n == 0) { unsigned c = getbits(z, 9); memset(z->cLen, 0, 510); for (int i = 0; i < 4096; i++) z->cTable[i] = (uint16_t)c; return; }
    int i = 0;
    while (i < n && i < 510) {
        unsigned c = z->ptTable[z->bitbuf >> 8];
        if (c >= 19) { unsigned m = 1u << 7; do { c = (z->bitbuf & m) ? z->right[c] : z->left[c]; m >>= 1; } while (c >= 19 && m); }
        fillbuf(z, z->ptLen[c < 19 ? c : 0]);
        if (c <= 2) {
            int cc = c == 0 ? 1 : c == 1 ? (int)getbits(z, 4) + 3 : (int)getbits(z, 9) + 20;
            while (--cc >= 0 && i < 510) z->cLen[i++] = 0;
        } else z->cLen[i++] = (uint8_t)(c - 2);
    }
    while (i < 510) z->cLen[i++] = 0;
    makeTable(z, 510, z->cLen, 12, z->cTable, 4096);
}
static uint8_t *decodeLzh(const uint8_t *src, size_t slen, size_t outSize, int dicbit) {
    Lzh *z = (Lzh *)calloc(1, sizeof(Lzh));
    uint8_t *out = (uint8_t *)malloc(outSize ? outSize : 1);
    z->src = src; z->slen = slen;
    int np = dicbit + 1, pbit = dicbit <= 13 ? 4 : 5;
    fillbuf(z, 16);
    unsigned blocksize = 0;
    size_t o = 0;
    while (o < outSize && !z->bad) {
        if (blocksize == 0) {
            blocksize = getbits(z, 16);
            readPtLen(z, 19, 5, 3); readCLen(z); readPtLen(z, np, pbit, -1);
            if (z->bad) break;
        }
        blocksize--;
        unsigned c = z->cTable[z->bitbuf >> 4];
        if (c >= 510) { unsigned m = 1u << 3; do { c = (z->bitbuf & m) ? z->right[c] : z->left[c]; m >>= 1; } while (c >= 510 && m); }
        if (c >= 510) { z->bad = 1; break; }
        fillbuf(z, z->cLen[c]);
        if (c < 256) { out[o++] = (uint8_t)c; continue; }
        unsigned len = c - 253;
        unsigned p = z->ptTable[z->bitbuf >> 8];
        if (p >= (unsigned)np) { unsigned m = 1u << 7; do { p = (z->bitbuf & m) ? z->right[p] : z->left[p]; m >>= 1; } while (p >= (unsigned)np && m); }
        if (p >= (unsigned)np) { z->bad = 1; break; }
        fillbuf(z, z->ptLen[p]);
        if (p != 0) p = (1u << (p - 1)) + getbits(z, p - 1);
        long from = (long)o - (long)p - 1;
        for (unsigned k = 0; k < len && o < outSize; k++) out[o++] = from + (long)k >= 0 ? out[from + k] : 0x20;
    }
    int bad = z->bad;
    free(z);
    if (bad) { free(out); return NULL; }
    return out;
}
static uint16_t crc16(const uint8_t *u, size_t n) {
    static uint16_t t[256];
    static int init = 0;
    if (!init) { for (int i = 0; i < 256; i++) { unsigned c = (unsigned)i; for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xa001 : c >> 1; t[i] = (uint16_t)c; } init = 1; }
    unsigned c = 0;
    for (size_t i = 0; i < n; i++) c = t[(c ^ u[i]) & 255] ^ (c >> 8);
    return (uint16_t)c;
}
static int isHead(const uint8_t *u, size_t len, size_t q) { return q + 22 <= len && u[q + 2] == '-' && u[q + 3] == 'l' && u[q + 6] == '-' && u[q + 20] <= 3; }
/* Contenu d'une archive LhA ; renvoie 0 si ce n'en est pas une */
static int readLha(const uint8_t *u, size_t len, const char *prefix, Items *out, Str *problems, const char *arcName) {
    size_t p = 0;
    if (!isHead(u, len, 0)) { size_t q = 0; while (q < len && q < 65536 && !isHead(u, len, q)) q++; if (!isHead(u, len, q)) return 0; p = q; }
    while (p < len && u[p] != 0 && isHead(u, len, p)) {
        int level = u[p + 20];
        char method[6];
        memcpy(method, u + p + 2, 5); method[5] = 0;
        uint32_t comp = l32(u + p + 7), orig = l32(u + p + 11), st = l32(u + p + 15);
        char name[512] = "", dir[512] = "", comment[256] = "";
        int crc = -1;
        ADate date = {0, 0, 0};
        size_t dataAt;
        int haveDate = 0;
        /* En-têtes d'extension : nom, dossier, commentaire, date Unix */
#define PARSE_EXT(q0, total)                                                                                  \
    do {                                                                                                      \
        size_t q = (q0);                                                                                      \
        unsigned sz = q + 2 <= len ? l16(u + q) : 0;                                                         \
        q += 2;                                                                                               \
        while (sz >= 3 && q + sz <= len) {                                                                    \
            unsigned type = u[q], bl = sz - 3;                                                                \
            const uint8_t *body = u + q + 1;                                                                  \
            if (type == 0x01 && bl < sizeof name) { memcpy(name, body, bl); name[bl] = 0; }                   \
            else if (type == 0x02 && bl < sizeof dir) {                                                       \
                memcpy(dir, body, bl); dir[bl] = 0;                                                           \
                for (unsigned i = 0; i < bl; i++) if ((unsigned char)dir[i] == 0xff) dir[i] = '/';            \
            } else if ((type == 0x3f || type == 0x71) && bl < sizeof comment) { memcpy(comment, body, bl); comment[bl] = 0; } \
            else if (type == 0x54 && bl >= 4) { date = fromUnix((time_t)l32(body)); haveDate = 1; }          \
            (total) += sz;                                                                                    \
            unsigned nx = l16(u + q + sz - 2);                                                                \
            q += sz; sz = nx;                                                                                 \
        }                                                                                                     \
    } while (0)
        if (level == 0 || level == 1) {
            unsigned H = u[p], nlen = u[p + 21];
            if (p + 22 + nlen > len) break;
            char raw[512];
            unsigned n = nlen < sizeof raw - 1 ? nlen : (unsigned)sizeof raw - 1;
            memcpy(raw, u + p + 22, n); raw[n] = 0;
            size_t z = strlen(raw);
            strcpy(name, raw);
            if (z < n) { snprintf(comment, sizeof comment, "%s", raw + z + 1); }
            date = fromDos(st); haveDate = 1;
            if (level == 0) {
                if (22 + nlen + 2 <= H + 2) crc = l16(u + p + 22 + nlen);
                dataAt = p + H + 2;
            } else {
                crc = l16(u + p + 22 + nlen);
                size_t ext = 0;
                PARSE_EXT(p + H, ext);
                dataAt = p + H + 2 + ext;
                comp -= (uint32_t)ext;
            }
        } else if (level == 2) {
            unsigned total = l16(u + p);
            size_t ext = 0;
            crc = l16(u + p + 21);
            date = fromUnix((time_t)st); haveDate = 1;
            PARSE_EXT(p + 24, ext);
            dataAt = p + total;
        } else { sadd(problems, "%s : en-tête LhA de niveau %d non géré\n", arcName, level); break; }
#undef PARSE_EXT
        (void)haveDate;
        char path[1100];
        snprintf(path, sizeof path, "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] != '/' ? "/" : "", name);
        for (char *c = path; *c; c++) if (*c == '\\') *c = '/';
        char full[1200];
        snprintf(full, sizeof full, "%s%s", prefix, path);
        int isDir = strcmp(method, "-lhd-") == 0;
        if (dataAt + comp > len) { sadd(problems, "%s : archive incomplète\n", arcName); break; }
        if (path[0]) {
            if (isDir) addItem(out, full, 1, NULL, 0, date, NULL);
            else {
                uint8_t *data = NULL;
                int dic = !strcmp(method, "-lh4-") ? 12 : !strcmp(method, "-lh5-") ? 13 : !strcmp(method, "-lh6-") ? 15 : !strcmp(method, "-lh7-") ? 16 : 0;
                if (!strcmp(method, "-lh0-") || !strcmp(method, "-lz4-")) {
                    data = (uint8_t *)malloc(orig ? orig : 1);
                    memcpy(data, u + dataAt, orig < comp ? orig : comp);
                } else if (dic) data = decodeLzh(u + dataAt, comp, orig, dic);
                Str t = {0};
                latin1To(&t, path);
                if (!data) sadd(problems, "%s / %s : %s\n", arcName, t.s, dic || !strcmp(method, "-lh0-") ? "contenu endommagé" : "méthode non gérée");
                else if (crc >= 0 && crc16(data, orig) != crc) { sadd(problems, "%s / %s : contenu endommagé (CRC)\n", arcName, t.s); free(data); }
                else addItem(out, full, 0, data, orig, date, comment);
                free(t.s);
            }
        }
        p = dataAt + comp;
    }
    return 1;
}

/* ---------- .zip (stocké ou deflate, zip64) ---------- */
static uint8_t *inflateRaw(const uint8_t *src, size_t slen, size_t outSize, int gzip, size_t *got) {
    z_stream s;
    memset(&s, 0, sizeof s);
    if (inflateInit2(&s, gzip ? 16 + MAX_WBITS : -MAX_WBITS) != Z_OK) return NULL;
    size_t cap = outSize ? outSize : slen * 4 + 1024;
    uint8_t *out = (uint8_t *)malloc(cap);
    s.next_in = (Bytef *)src; s.avail_in = (uInt)slen;
    int r;
    do {
        if (s.total_out == cap) { cap *= 2; out = (uint8_t *)realloc(out, cap); }
        s.next_out = out + s.total_out; s.avail_out = (uInt)(cap - s.total_out);
        r = inflate(&s, Z_NO_FLUSH);
    } while (r == Z_OK);
    *got = s.total_out;
    inflateEnd(&s);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    return out;
}
static void readZip(const uint8_t *u, size_t len, const char *prefix, Items *out, Str *problems, const char *arcName) {
    long long eocd = -1;
    for (long long i = (long long)len - 22; i >= 0 && i >= (long long)len - 65557; i--) if (l32(u + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { sadd(problems, "%s : archive ZIP illisible\n", arcName); return; }
    unsigned count = l16(u + eocd + 10);
    size_t p = l32(u + eocd + 16);
    for (unsigned k = 0; k < count && p + 46 <= len; k++) {
        if (l32(u + p) != 0x02014b50) break;
        unsigned flags = l16(u + p + 8), method = l16(u + p + 10), nlen = l16(u + p + 28), xlen = l16(u + p + 30), clen = l16(u + p + 32);
        uint32_t dosTime = l32(u + p + 12);
        uint64_t csize = l32(u + p + 20), usize = l32(u + p + 24), loc = l32(u + p + 42);
        if (csize == 0xffffffffu || usize == 0xffffffffu || loc == 0xffffffffu) {
            for (size_t x = p + 46 + nlen, end = x + xlen; x + 4 <= end && x + 4 <= len; x += 4 + l16(u + x + 2)) {
                if (l16(u + x) != 1) continue;
                size_t q = x + 4;
#define BIG(v) do { (v) = (uint64_t)l32(u + q) | (uint64_t)l32(u + q + 4) << 32; q += 8; } while (0)
                if (usize == 0xffffffffu) BIG(usize);
                if (csize == 0xffffffffu) BIG(csize);
                if (loc == 0xffffffffu) BIG(loc);
#undef BIG
                break;
            }
        }
        char name[1024];
        unsigned n = nlen < sizeof name - 1 ? nlen : (unsigned)sizeof name - 1;
        memcpy(name, u + p + 46, n); name[n] = 0;
        p += 46 + nlen + xlen + clen;
        if (!n || name[n - 1] == '/' || strstr(name, "__MACOSX") || name[0] == '.') continue;
        /* Noms en UTF-8 (drapeau 0x800) ou déjà en Latin-1 */
        char *lat = (flags & 0x800) ? toLatin1(name) : strdup(name);
        if (flags & 1) { sadd(problems, "%s : archive protégée par mot de passe\n", arcName); free(lat); return; }
        if (loc + 30 > len) { free(lat); break; }
        size_t start = (size_t)loc + 30 + l16(u + loc + 26) + l16(u + loc + 28);
        if (start + csize > len) { sadd(problems, "%s : archive incomplète\n", arcName); free(lat); return; }
        uint8_t *data = NULL;
        size_t got = 0;
        if (method == 0) { data = (uint8_t *)malloc(csize ? (size_t)csize : 1); memcpy(data, u + start, (size_t)csize); got = (size_t)csize; }
        else if (method == 8) data = inflateRaw(u + start, (size_t)csize, (size_t)usize, 0, &got);
        Str t = {0};
        latin1To(&t, lat);
        if (!data) sadd(problems, "%s / %s : %s\n", arcName, t.s, method == 8 ? "contenu endommagé" : "méthode de compression non gérée");
        else { char full[1200]; snprintf(full, sizeof full, "%s%s", prefix, lat); addItem(out, full, 0, data, got, fromDos(dosTime), NULL); }
        free(t.s); free(lat);
    }
}

/* ---------- disquette AmigaDOS ---------- */
typedef struct { const uint8_t *u; size_t n; int ffs; Items *out; const char *prefix; Str *problems; const char *arcName; uint8_t *seen; uint8_t *icon; size_t iconLen; } Adf;
static uint32_t AL(Adf *a, uint32_t b, int o) { return g32(a->u + (size_t)b * BS, o); }
static int aok(Adf *a, uint32_t b) { return b > 1 && b < a->n; }
static ADate adate(Adf *a, uint32_t b) { ADate d = {AL(a, b, BS - 92), AL(a, b, BS - 88), AL(a, b, BS - 84)}; return d; }
static uint8_t *adfFile(Adf *a, uint32_t h, size_t *size) {
    size_t sz = AL(a, h, BS - 188), pos = 0;
    uint8_t *data = (uint8_t *)malloc(sz ? sz : 1);
    uint32_t b = h;
    for (int guard = 0; aok(a, b) && pos < sz && guard < 4000; guard++) {
        uint32_t cnt = AL(a, b, 8);
        if (cnt > HT) cnt = HT;
        for (uint32_t i = 0; i < cnt && pos < sz; i++) {
            uint32_t d = AL(a, b, BS - 204 - (int)i * 4);
            if (!aok(a, d)) { free(data); return NULL; }
            size_t l = a->ffs ? BS : AL(a, d, 12);
            if (!a->ffs && l > BS - 24) l = BS - 24;
            if (l > sz - pos) l = sz - pos;
            memcpy(data + pos, a->u + (size_t)d * BS + (a->ffs ? 0 : 24), l);
            pos += l;
        }
        b = AL(a, b, BS - 8);
    }
    if (pos < sz) { free(data); return NULL; }
    *size = sz;
    return data;
}
static void adfWalk(Adf *a, uint32_t dir, const char *path, int depth) {
    if (depth > 20) return;
    for (int i = 0; i < HT; i++) {
        uint32_t b = AL(a, dir, 24 + i * 4);
        while (aok(a, b) && !a->seen[b] && AL(a, b, 0) == T_HEADER) {
            a->seen[b] = 1;
            uint32_t st = AL(a, b, BS - 4);
            char nm[32], cm[80], p[1024], full[1400];
            bcpl(a->u + (size_t)b * BS, BS - 80, nm, 30);
            bcpl(a->u + (size_t)b * BS, BS - 184, cm, 79);
            snprintf(p, sizeof p, "%s%s%s", path, path[0] ? "/" : "", nm);
            snprintf(full, sizeof full, "%s%s", a->prefix, p);
            if (st == ST_USERDIR) { addItem(a->out, full, 1, NULL, 0, adate(a, b), NULL); adfWalk(a, b, p, depth + 1); }
            else if (st == ST_FILE) {
                size_t sz = 0;
                uint8_t *data = adfFile(a, b, &sz);
                if (!data) { Str t = {0}; latin1To(&t, p); sadd(a->problems, "%s / %s : fichier incomplet (disquette abîmée ?)\n", a->arcName, t.s); free(t.s); }
                else if (depth == 0 && !strcmp(nm, "Disk.info")) { free(a->icon); a->icon = data; a->iconLen = sz; }
                else addItem(a->out, full, 0, data, sz, adate(a, b), cm);
            }
            b = AL(a, b, BS - 16);
        }
    }
}
/* Contenu d'une disquette dans le dossier folder ; 0 si ce n'est pas une disquette AmigaDOS */
static int readAdf(const uint8_t *u, size_t len, const char *destPrefix, const char *folder, Items *out, Str *problems, const char *arcName) {
    size_t n = len / BS;
    if (n < 4 || u[0] != 'D' || u[1] != 'O' || u[2] != 'S') return 0;
    Adf a = {u, n, u[3] & 1, out, NULL, problems, arcName, (uint8_t *)calloc(n, 1), NULL, 0};
    uint32_t root = (uint32_t)(n / 2);
    if (AL(&a, root, 0) != T_HEADER || AL(&a, root, BS - 4) != ST_ROOT) { free(a.seen); return 0; }
    char pre[1200];
    snprintf(pre, sizeof pre, "%s%s/", destPrefix, folder);
    a.prefix = pre;
    ADate now = fromUnix(time(NULL));
    char top[1200];
    snprintf(top, sizeof top, "%s%s", destPrefix, folder);
    addItem(out, top, 1, NULL, 0, now, NULL);
    adfWalk(&a, root, "", 0);
    /* L'icône de la disquette devient celle du tiroir (do_Type : WBDISK → WBDRAWER) */
    if (a.icon && a.iconLen > 48) { a.icon[48] = 2; char ic[1210]; snprintf(ic, sizeof ic, "%s.info", top); addItem(out, ic, 0, a.icon, a.iconLen, now, NULL); }
    else free(a.icon);
    free(a.seen);
    return 1;
}

/* ---------- chemins Amiga ---------- */
static char *cleanPath(const char *p) {
    size_t n = strlen(p);
    char *r = (char *)malloc(n + 2), *o = r;
    const char *s = p;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/' && *e != '\\') e++;
        size_t l = (size_t)(e - s);
        if (l && !(l == 1 && s[0] == '.') && !(l == 2 && s[0] == '.' && s[1] == '.')) {
            if (o != r) *o++ = '/';
            size_t k = l > 30 ? 30 : l;
            for (size_t i = 0; i < k; i++) *o++ = s[i] == ':' ? '_' : s[i];
        }
        s = *e ? e + 1 : e;
    }
    *o = 0;
    return r;
}
static const char *baseName(const char *p) {
    const char *b = p;
    for (const char *s = p; *s; s++) if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}
static int endsWith(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) { char x = s[a - b + i]; if (x >= 'A' && x <= 'Z') x += 32; if (x != suf[i]) return 0; }
    return 1;
}
static void sizeText(Str *s, double b) {
    if (b < 1024) sadd(s, "%.0f octets", b);
    else if (b < 1048576) sadd(s, "%.0f Ko", b / 1024);
    else if (b < 1073741824.0) sadd(s, "%.1f Mo", b / 1048576);
    else sadd(s, "%.2f Go", b / 1073741824.0);
}
static void outText(Str *s, char *out, int outLen) {
    if (!out || outLen <= 0) return;
    snprintf(out, (size_t)outLen, "%s", s->s ? s->s : "");
}

int hdt_partitions(const char *hdf, char *out, int outLen) {
    Disk d;
    Str s = {0};
    jmp_buf jb;
    if (!diskOpen(&d, hdf, 0)) { sadd(&s, "disque illisible"); outText(&s, out, outLen); free(s.s); diskClose(&d); return -1; }
    d.fail = &jb;
    int n = 0;
    Part parts[16];
    if (setjmp(jb)) { s.len = 0; sadd(&s, "%s", d.why); outText(&s, out, outLen); free(s.s); diskClose(&d); return -1; }
    n = listParts(&d, parts, 16);
    for (int i = 0; i < n; i++) {
        uint64_t fr = 0;
        if (parts[i].ok) { Vol v; volOpen(&v, &d, &parts[i]); fr = freeCount(&v) * BS; free(v.maps); }
        latin1To(&s, parts[i].name); sadd(&s, "\t"); latin1To(&s, parts[i].volume);
        sadd(&s, "\t%s\t%llu\t%d\t%s\n", parts[i].fs, (unsigned long long)fr, parts[i].ok, parts[i].reason);
    }
    outText(&s, out, outLen);
    free(s.s);
    diskClose(&d);
    return n;
}

int hdt_copy(const char *hdf, int part, const char *dest, const char *const *src, const char *const *dst, int n,
             int unpack, char *report, int reportLen) {
    Str rep = {0}, problems = {0};
    Items items = {0};
    char *volatile base = NULL;
    volatile int toDest = 0;   /* au moins une source copiée dans dest (sinon : chemins exacts) */
    {
        char *l = toLatin1(dest ? dest : "");
        base = cleanPath(l);
        free(l);
    }
    char prefix[300];
    snprintf(prefix, sizeof prefix, "%s%s", base, base[0] ? "/" : "");
    /* Sources → éléments */
    for (int i = 0; i < n; i++) {
        size_t len = 0;
        uint8_t *u = readFile(src[i], &len);
        const char *bn = baseName(src[i]);
        if (!u) { sadd(&problems, "%s : lecture impossible\n", bn); continue; }
        char *latName = toLatin1(bn);
        ADate date = fromUnix(mtimeOf(src[i]));
        if (dst && dst[i] && dst[i][0]) {
            char *l = toLatin1(dst[i]);
            addItem(&items, l, 0, u, len, date, NULL);
            free(l); free(latName);
            continue;
        }
        toDest = 1;
        if (unpack && (endsWith(bn, ".lha") || endsWith(bn, ".lzh"))) {
            if (!readLha(u, len, prefix, &items, &problems, bn)) sadd(&problems, "%s : ce n'est pas une archive LhA\n", bn);
            free(u);
        } else if (unpack && endsWith(bn, ".zip")) {
            readZip(u, len, prefix, &items, &problems, bn);
            free(u);
        } else if (unpack && (endsWith(bn, ".adf") || endsWith(bn, ".adz") || endsWith(bn, ".adf.gz"))) {
            uint8_t *disk = u;
            size_t dlen = len;
            if (!endsWith(bn, ".adf")) { disk = inflateRaw(u, len, 0, 1, &dlen); free(u); }
            char folder[64];
            size_t cut = strlen(latName) - (endsWith(latName, ".adf.gz") ? 7 : 4);
            char *nm = (char *)malloc(cut + 1);
            memcpy(nm, latName, cut); nm[cut] = 0;
            char *c = cleanPath(nm);
            snprintf(folder, sizeof folder, "%s", c[0] ? c : "Disquette");
            free(c); free(nm);
            if (!disk || !readAdf(disk, dlen, prefix, folder, &items, &problems, bn))
                sadd(&problems, "%s : pas de fichiers AmigaDOS sur cette disquette (jeu à chargeur spécial) ; utilisez-la directement dans un lecteur\n", bn);
            free(disk);
        } else if (unpack && endsWith(bn, ".dms")) {
            sadd(&problems, "%s : les disquettes .dms ne s'ouvrent pas ici ; convertissez-la en .adf (ou utilisez-la dans un lecteur)\n", bn);
            free(u);
        } else {
            char full[400];
            snprintf(full, sizeof full, "%s%s", prefix, latName);
            addItem(&items, full, 0, u, len, date, NULL);
        }
        free(latName);
    }
    int copied = -1;
    Disk d;
    jmp_buf jb;
    Vol *v = (Vol *)calloc(1, sizeof(Vol));   /* sur le tas : intact après longjmp */
    if (!items.n) {
        sadd(&rep, "Rien à copier.");
        goto done;
    }
    if (!diskOpen(&d, hdf, 1)) { sadd(&rep, "Copie impossible : disque illisible ou protégé en écriture. Le disque n'a pas été modifié."); diskClose(&d); goto done; }
    d.fail = &jb;
    if (setjmp(jb)) {
        rep.len = 0;
        sadd(&rep, "Copie impossible : %s. Le disque n'a pas été modifié.", d.why);
        free(v->maps); v->maps = NULL;
        diskClose(&d);
        goto done;
    }
    {
        Part parts[16];
        int np = listParts(&d, parts, 16);
        if (part < 0 || part >= np) fail(&d, "partition introuvable");
        Part *p = &parts[part];
        if (!p->ok) fail(&d, "partition non modifiable : %s", p->reason);
        volOpen(v, &d, p);
        /* Place nécessaire : chaque fichier, plus un bloc par dossier à créer (estimation large) */
        uint64_t need = 0;
        for (int i = 0; i < items.n; i++) need += items.v[i].dir ? 1 : blocksFor(v, items.v[i].len) + 1;
        uint64_t fr = freeCount(v);
        if (need > fr) {
            Str a = {0}, b = {0};
            sizeText(&a, (double)need * BS); sizeText(&b, (double)fr * BS);
            fail(&d, "pas assez de place : %s nécessaires, %s libres", a.s, b.s);
        }
        int count = 0;
        double bytes = 0;
        for (int i = 0; i < items.n; i++) {
            char *c = cleanPath(items.v[i].path);
            if (!c[0]) { free(c); continue; }
            if (items.v[i].dir) { dirPath(v, c); free(c); continue; }
            char *slash = strrchr(c, '/');
            uint32_t dir = v->p->root;
            const char *fname = c;
            if (slash) { *slash = 0; dir = dirPath(v, c); fname = slash + 1; }
            writeFileEntry(v, dir, fname, items.v[i].data, items.v[i].len, items.v[i].date, items.v[i].comment);
            count++;
            bytes += (double)items.v[i].len;
            free(c);
        }
        volFinish(v);
        sadd(&rep, "%d fichier%s copié%s %s ", count, count > 1 ? "s" : "", count > 1 ? "s" : "", toDest ? "dans" : "sur");
        latin1To(&rep, p->name);
        sadd(&rep, ":");
        if (toDest) latin1To(&rep, base);
        sadd(&rep, " (");
        sizeText(&rep, bytes);
        sadd(&rep, ").");
        copied = count;
        free(v->maps); v->maps = NULL;
        diskClose(&d);
    }
done:
    free(v);
    if (problems.len) sadd(&rep, "\nIgnorés :\n%s", problems.s);
    outText(&rep, report, reportLen);
    free(rep.s); free(problems.s); free((char *)base);
    freeItems(&items);
    return copied;
}
