/* Pont entre l'application PC (Windows, Linux) et le frontal libretro de la version web.
 *
 * Le frontal (amiga1200/wasm/frontend.c) relie le cœur Amiberry à des fonctions simples « fe_* ». Ce fichier y ajoute
 * ce que la page web (et les ponts Android et Mac) faisaient eux-mêmes : le fil de l'émulation cadencé par le son, le
 * son (sortie ouverte par l'application, qui demande ses échantillons à amb_audio_render), le journal du cœur, et des fonctions « amb_* » appelées par l'application (native/bridge.h). L'image
 * est lue par l'application (amb_frame) et affichée par SDL. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include "libretro.h"
#include "bridge.h"

/* ---------- frontal (frontend.c) ---------- */
void fe_set_option(const char *k, const char *v);
void fe_set_dirs(const char *sys, const char *save);
uint8_t *fe_video_ptr(void);
unsigned fe_video_w(void);
unsigned fe_video_h(void);
unsigned long fe_frame_count(void);
unsigned fe_audio_read(float *dst, unsigned max);
unsigned fe_audio_available(void);
void fe_mouse_move(int dx, int dy);
void fe_mouse_buttons(int mask);
void fe_joy(int port, unsigned mask);
void fe_key(int down, unsigned retrok);
int fe_start(const char *path);
void fe_run(void);
double fe_fps(void);
double fe_sample_rate(void);
void fe_drive_insert(int num, const char *path);
void fe_drive_eject(int num);
void fe_flush(void);
unsigned fe_state_size(void);
int fe_state_save(void *dst, unsigned size);
int fe_state_load(const void *src, unsigned size);
void fe_thread_warp(int w);
void fe_midi_enable(int out, int in);
unsigned fe_midi_out_read(uint8_t *dst, unsigned max);
void fe_midi_in_write(const uint8_t *src, unsigned len);
void amb_set_floppy_sound(int on);

/* ---------- journal ----------
 * Le frontal ne garde que les avertissements du cœur. Ici, tout ce que le cœur écrit va dans un fichier (menu
 * Amiga → Journal) et sur la sortie standard : le frontal est compilé avec retro_set_environment renommé en
 * amb_frontend_set_environment, qui donne au cœur notre propre fonction de journal. */
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file;
static long log_bytes;
#define LOG_MAX (4L << 20)

static void log_line(const char *msg) {
  size_t n = strlen(msg);
  pthread_mutex_lock(&log_mtx);
  fputs(msg, stdout);
  if (!n || msg[n - 1] != '\n') fputc('\n', stdout);
  if (log_file && log_bytes < LOG_MAX) {
    fputs(msg, log_file);
    if (!n || msg[n - 1] != '\n') { fputc('\n', log_file); n++; }
    fflush(log_file);
    log_bytes += (long)n;
  }
  pthread_mutex_unlock(&log_mtx);
}
static void core_log(enum retro_log_level level, const char *fmt, ...) {
  (void)level;
  char buf[2048];
  va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
  log_line(buf);
}
static retro_environment_t real_env;
static volatile int emu_turbo;
static bool env_wrap(unsigned cmd, void *data) {
  if (cmd == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) { ((struct retro_log_callback *)data)->log = core_log; return true; }
  /* Turbo : le cœur passe en mode turbo (sans lui, avec le processeur « au maximum », chaque image dure une image réelle) */
  if (cmd == RETRO_ENVIRONMENT_GET_FASTFORWARDING) { *(bool *)data = emu_turbo != 0; return true; }
  return real_env(cmd, data);
}
void amb_frontend_set_environment(retro_environment_t cb) {
  real_env = cb;
  retro_set_environment(env_wrap);
}
void amb_set_log(const char *path) {
  pthread_mutex_lock(&log_mtx);
  if (log_file) fclose(log_file);
  log_file = path ? fopen(path, "w") : NULL;
  log_bytes = 0;
  pthread_mutex_unlock(&log_mtx);
}
void amb_log(const char *msg) { if (msg) log_line(msg); }

void amb_set_dirs(const char *sys, const char *save) { fe_set_dirs(sys, save); }
void amb_set_option(const char *k, const char *v) { fe_set_option(k, v); }

/* ---------- son ----------
 * Lecture sans grésillement, comme sur Android : le cœur livre le son par paquets d'une image, irrégulièrement, alors
 * que la sortie en demande de petits morceaux à intervalles réguliers. Une réserve visée (50 ms au départ) est
 * gardée : la lecture attend qu'elle soit pleine, puis lit un peu plus vite ou plus lentement (au plus 0,5 %,
 * inaudible) pour la garder à sa cible. Un manque relève la cible de 10 ms (jusqu'à 150 ms), une minute sans manque
 * la redescend. La sortie (SDL) convertit la fréquence du cœur vers la sienne. */
static int aunit;
static void (*hook_open)(double rate);
static void (*hook_close)(void);
void amb_set_audio(void (*open)(double rate), void (*close)(void)) { hook_open = open; hook_close = close; }
static volatile int audio_mute, audio_off;
#define AQ_FRAMES 65536
static float aq[AQ_FRAMES * 2];
static unsigned aq_n;
static double aq_pos;
static int aq_playing;
static double aq_target_ms = 50, aq_rate = 44100;
static long aq_calm;
static volatile unsigned long aq_pulls;

static void aq_drop(unsigned n) {
  if (n >= aq_n) { aq_n = 0; aq_pos = 0; return; }
  memmove(aq, aq + n * 2, (size_t)(aq_n - n) * 2 * sizeof(float));
  aq_n -= n;
  aq_pos -= n;
  if (aq_pos < 0) aq_pos = 0;
}

void amb_audio_render(float *d, unsigned frames) {
  aq_pulls++;
  if (aq_n < AQ_FRAMES) aq_n += fe_audio_read(aq + aq_n * 2, AQ_FRAMES - aq_n);
  unsigned target = (unsigned)(aq_rate * aq_target_ms / 1000);
  if (aq_n > target * 3) aq_drop(aq_n - target);
  if (audio_mute || !aq_playing) {
    memset(d, 0, (size_t)frames * 2 * sizeof(float));
    if (!audio_mute && aq_n >= target) aq_playing = 1;
    return;
  }
  double err = ((double)aq_n - target) / (target ? target : 1);
  if (err > 1) err = 1; else if (err < -1) err = -1;
  double step = 1 + 0.005 * err;
  unsigned i = 0;
  for (; i < frames; i++) {
    unsigned k = (unsigned)aq_pos;
    if (k + 1 >= aq_n) break;
    float f = (float)(aq_pos - k);
    d[i * 2] = aq[k * 2] + (aq[k * 2 + 2] - aq[k * 2]) * f;
    d[i * 2 + 1] = aq[k * 2 + 1] + (aq[k * 2 + 3] - aq[k * 2 + 1]) * f;
    aq_pos += step;
  }
  if (i < frames) {
    memset(d + i * 2, 0, (size_t)(frames - i) * 2 * sizeof(float));
    aq_playing = 0;
    if (aq_target_ms < 150) aq_target_ms += 10;
    aq_calm = 0;
  } else if ((aq_calm += frames) > aq_rate * 60L && aq_target_ms > 50) {
    aq_target_ms -= 10;
    aq_calm = 0;
  }
  aq_drop((unsigned)aq_pos);
}

static void audio_open(void) {
  if (aunit || audio_off) return;
  aq_rate = fe_sample_rate() > 0 ? fe_sample_rate() : 44100;
  if (!hook_open) return;
  aunit = 1;
  hook_open(aq_rate);
}
static void audio_close(void) {
  if (!aunit) return;
  if (hook_close) hook_close();
  aunit = 0;
}
void amb_mute_audio(int m) { audio_off = m; }

/* ---------- fil de l'émulation, cadencé par le son ----------
 * Une image est calculée dès que la réserve de son descend sous sa cible : l'émulation suit exactement l'horloge du
 * son, à la vraie cadence de l'Amiga (NTSC, écran RTG…). Sans son, elle revient à une horloge fixe. Ce qui touche à
 * l'état du cœur depuis l'interface (sauvegardes d'état, disquettes) s'exécute dans ce fil, entre deux images. */
static pthread_t emu_thr;
static pthread_mutex_t emu_mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile int emu_state, emu_stop, emu_paused;
static char emu_path[2048];
static volatile unsigned stat_frames;
static volatile double stat_busy_us;
#define KQ 256
static unsigned kq[KQ];
static volatile unsigned kq_w, kq_r;
static pthread_mutex_t cmd_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cmd_cv = PTHREAD_COND_INITIALIZER;
static int cmd_code, cmd_res, cmd_done;   /* 1 sauvegarder, 2 reprendre */
static void *cmd_buf;
static unsigned cmd_len;

/* MIDI du port série : le cœur lit et écrit les files du frontal dans ce fil-ci, entre deux images ; l'application
 * (MIDI du système) échange avec deux autres files, à un seul lecteur et un seul écrivain chacune (comme sur Android) */
#define MQ 16384
static uint8_t mo[MQ], mi[MQ];
static unsigned mo_w, mo_r, mi_w, mi_r;
static void midi_pump(void) {
  uint8_t b[512];
  for (unsigned n; (n = fe_midi_out_read(b, sizeof b));)
    for (unsigned i = 0; i < n; i++) {
      unsigned w = __atomic_load_n(&mo_w, __ATOMIC_RELAXED), nw = (w + 1) % MQ;
      if (nw == __atomic_load_n(&mo_r, __ATOMIC_ACQUIRE)) break;  /* l'application ne lit plus : octets perdus */
      mo[w] = b[i]; __atomic_store_n(&mo_w, nw, __ATOMIC_RELEASE);
    }
  unsigned n = 0, r = __atomic_load_n(&mi_r, __ATOMIC_RELAXED), w = __atomic_load_n(&mi_w, __ATOMIC_ACQUIRE);
  while (r != w && n < sizeof b) { b[n++] = mi[r]; r = (r + 1) % MQ; }
  __atomic_store_n(&mi_r, r, __ATOMIC_RELEASE);
  if (n) fe_midi_in_write(b, n);
}
void amb_midi_enable(int out, int in) { fe_midi_enable(out, in); }
unsigned amb_midi_read(uint8_t *dst, unsigned max) {
  unsigned n = 0, r = __atomic_load_n(&mo_r, __ATOMIC_RELAXED), w = __atomic_load_n(&mo_w, __ATOMIC_ACQUIRE);
  while (r != w && n < max) { dst[n++] = mo[r]; r = (r + 1) % MQ; }
  __atomic_store_n(&mo_r, r, __ATOMIC_RELEASE);
  return n;
}
void amb_midi_write(const uint8_t *src, unsigned len) {
  for (unsigned i = 0; i < len; i++) {
    unsigned w = __atomic_load_n(&mi_w, __ATOMIC_RELAXED), nw = (w + 1) % MQ;
    if (nw == __atomic_load_n(&mi_r, __ATOMIC_ACQUIRE)) break;
    mi[w] = src[i]; __atomic_store_n(&mi_w, nw, __ATOMIC_RELEASE);
  }
}

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

static void emu_frame(void) {
  double t0 = now_ms();
  pthread_mutex_lock(&emu_mtx);
  /* Une touche par image, dans l'ordre (l'Amiga voit chaque appui et chaque relâchement) */
  if (kq_r != kq_w) { unsigned k = kq[kq_r]; kq_r = (kq_r + 1) % KQ; fe_key((k & 0x80000000u) != 0, k & 0x7fffffffu); }
  fe_run();
  midi_pump();
  pthread_mutex_unlock(&emu_mtx);
  stat_busy_us += (now_ms() - t0) * 1000.0;
  stat_frames++;
}
/* Disquettes, manettes, bruit des lecteurs : demandés par l'interface, faits par ce fil entre deux images. L'interface
 * ne prend jamais le verrou du cœur : une image longue (ou un cœur bloqué) ne gèle pas l'application. */
#define OPQ 16
struct op { int kind, a, b; char path[1024]; };   /* 1 disquette, 2 type de manette, 3 bruit des lecteurs */
static struct op opq[OPQ];
static unsigned op_w, op_r;
static pthread_mutex_t op_mtx = PTHREAD_MUTEX_INITIALIZER;
static void op_push(int kind, int a, int b, const char *path) {
  pthread_mutex_lock(&op_mtx);
  unsigned n = (op_w + 1) % OPQ;
  if (n != op_r) {
    struct op *o = &opq[op_w];
    o->kind = kind; o->a = a; o->b = b;
    snprintf(o->path, sizeof o->path, "%s", path ? path : "");
    op_w = n;
  }
  pthread_mutex_unlock(&op_mtx);
}
static void ops_run(void) {
  for (;;) {
    struct op o;
    pthread_mutex_lock(&op_mtx);
    if (op_r == op_w) { pthread_mutex_unlock(&op_mtx); return; }
    o = opq[op_r]; op_r = (op_r + 1) % OPQ;
    pthread_mutex_unlock(&op_mtx);
    pthread_mutex_lock(&emu_mtx);
    if (o.kind == 1) { if (o.path[0]) fe_drive_insert(o.a, o.path); else fe_drive_eject(o.a); }
    else if (o.kind == 2) retro_set_controller_port_device((unsigned)o.a, (unsigned)o.b);
    else if (o.kind == 3) amb_set_floppy_sound(o.a);
    pthread_mutex_unlock(&emu_mtx);
  }
}

/* Sauvegardes d'état : le fil de l'émulation sérialise lui-même (taille comprise) ; l'interface attend au plus
 * 3 secondes, puis abandonne : le fil n'exécute plus la commande */
static void emu_cmd_run(void) {
  ops_run();
  pthread_mutex_lock(&cmd_mtx);
  if (cmd_code && !cmd_done) {
    pthread_mutex_lock(&emu_mtx);
    if (cmd_code == 1) {
      unsigned n = fe_state_size();
      void *b = n ? malloc(n) : NULL;
      cmd_res = b && fe_state_save(b, n);
      if (cmd_res) { cmd_buf = b; cmd_len = n; } else free(b);
    } else cmd_res = fe_state_load(cmd_buf, cmd_len);
    pthread_mutex_unlock(&emu_mtx);
    cmd_done = 1;
    pthread_cond_broadcast(&cmd_cv);
  }
  pthread_mutex_unlock(&cmd_mtx);
}
/* code 1 : sauvegarde, le tampon produit revient dans *buf (à libérer) ; code 2 : reprise depuis buf */
static int emu_cmd(int code, void **buf, unsigned *len) {
  if (emu_state != 2) return 0;
  pthread_mutex_lock(&cmd_mtx);
  cmd_code = code; cmd_buf = code == 2 ? *buf : NULL; cmd_len = code == 2 ? *len : 0; cmd_done = 0; cmd_res = 0;
  for (int waited = 0; !cmd_done && emu_state == 2 && !emu_stop && waited < 30; waited++) {
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_nsec += 100000000; if (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&cmd_cv, &cmd_mtx, &t);
  }
  int r = cmd_done ? cmd_res : 0;
  if (code == 1 && r) { *buf = cmd_buf; *len = cmd_len; }
  if (!cmd_done) cmd_done = 1;
  cmd_code = 0;
  pthread_mutex_unlock(&cmd_mtx);
  return r;
}

static void *emu_main(void *arg) {
  (void)arg;
  pthread_mutex_lock(&emu_mtx);
  int ok = fe_start(emu_path[0] ? emu_path : NULL);
  pthread_mutex_unlock(&emu_mtx);
  if (!ok) { emu_state = -1; return NULL; }
  audio_open();
  emu_state = 2;
  double period = 1000.0 / (fe_fps() > 0 ? fe_fps() : 50.0), next = now_ms();
  unsigned long last_pulls = aq_pulls;
  double last_pull_t = now_ms();
  while (!emu_stop) {
    emu_cmd_run();
    if (emu_paused) { usleep(4000); next = now_ms(); continue; }
    double now = now_ms();
    if (emu_turbo) { emu_frame(); next = now_ms(); continue; }   /* turbo : sans attendre */
    if (aq_pulls != last_pulls) { last_pulls = aq_pulls; last_pull_t = now; }
    if (aunit && now - last_pull_t < 200) {
      unsigned queued = fe_audio_available() + aq_n;
      unsigned want = (unsigned)(aq_rate * (aq_target_ms + 25) / 1000);
      /* Au plus deux fois la cadence normale : un Amiga muet ne s'emballe pas */
      if (queued < want && now >= next) { emu_frame(); next = now + period / 2; } else usleep(1000);
      continue;
    }
    if (now < next) { double dd = next - now; usleep(dd > 2.0 ? (useconds_t)((dd - 1.0) * 1000) : 200); continue; }
    if (now - next > 250) next = now;
    emu_frame();
    next += period;
  }
  return NULL;
}

int amb_start(const char *content) {
  snprintf(emu_path, sizeof emu_path, "%s", content ? content : "");
  emu_state = 1;
  pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 16u << 20);
  if (pthread_create(&emu_thr, &a, emu_main, NULL)) { emu_state = 0; return 0; }
  return 1;
}
int amb_state(void) { return emu_state; }
void amb_pause(int p) { emu_paused = p; audio_mute = p; if (p) fe_flush(); }
void amb_turbo(int on) { emu_turbo = on; fe_thread_warp(on ? 8 : 1); }
void amb_stop(void) { audio_close(); emu_stop = 1; fe_flush(); }

const uint8_t *amb_frame(unsigned *w, unsigned *h) { *w = fe_video_w(); *h = fe_video_h(); return fe_video_ptr(); }
unsigned long amb_frame_count(void) { return fe_frame_count(); }

/* ---------- entrées ---------- */
void amb_mouse_move(int dx, int dy) { fe_mouse_move(dx, dy); }
void amb_mouse_buttons(int m) { fe_mouse_buttons(m); }
void amb_joy(int port, unsigned m) { fe_joy(port, m); }
void amb_key(int down, unsigned retrok) {
  unsigned n = (kq_w + 1) % KQ;
  if (n == kq_r) return;
  kq[kq_w] = retrok | (down ? 0x80000000u : 0);
  __atomic_store_n(&kq_w, n, __ATOMIC_RELEASE);
}
void amb_port_device(unsigned port, unsigned device) { op_push(2, (int)port, (int)device, NULL); }

/* ---------- disquettes, sauvegardes ---------- */
void amb_insert_disk(int drive, const char *path) { op_push(1, drive, 0, path); }
void amb_floppy_sound(int on) { op_push(3, on, 0, NULL); }
int amb_save_state(const char *path) {
  int ok = 0;
  void *buf = NULL; unsigned n = 0;
  if (emu_cmd(1, &buf, &n)) {
    FILE *f = fopen(path, "wb");
    if (f) { ok = fwrite(buf, 1, n, f) == n; fclose(f); }
  }
  free(buf);
  return ok;
}
int amb_load_state(const char *path) {
  int ok = 0;
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  void *buf = n > 0 ? malloc((size_t)n) : NULL;
  if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) { unsigned len = (unsigned)n; ok = emu_cmd(2, &buf, &len); }
  fclose(f); free(buf);
  return ok;
}

/* ---------- mesures ---------- */
unsigned amb_frames(void) { return __atomic_exchange_n(&stat_frames, 0, __ATOMIC_RELAXED); }
double amb_busy_us(void) { double b = stat_busy_us; stat_busy_us = 0; return b; }
double amb_fps(void) { return fe_fps(); }
