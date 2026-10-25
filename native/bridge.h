/* Pont entre l'application PC et le frontal libretro de la version web (native/bridge.c). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Journal du cœur et de l'application (fichier, et sortie standard) */
void amb_set_log(const char *path);
void amb_log(const char *msg);

void amb_set_dirs(const char *sys, const char *save);
void amb_set_option(const char *key, const char *value);
/* Démarre le fil de l'émulation sur le contenu (configuration .uae, archive .lha…) et le son */
int amb_start(const char *content);
/* 1 en démarrage, 2 en marche, -1 refus du cœur, 0 échec */
int amb_state(void);
void amb_pause(int paused);
void amb_turbo(int on);
/* Sans son (essai sans fenêtre) : l'émulation suit l'horloge fixe */
void amb_mute_audio(int mute);
/* Son : l'application ouvre sa sortie à la fréquence du cœur et lui demande des échantillons (stéréo, flottants) */
void amb_set_audio(void (*open)(double rate), void (*close)(void));
void amb_audio_render(float *dst, unsigned frames);
void amb_stop(void);

/* Dernière image (RGBA, w × h), et le nombre d'images produites depuis le démarrage */
const uint8_t *amb_frame(unsigned *w, unsigned *h);
unsigned long amb_frame_count(void);

void amb_mouse_move(int dx, int dy);
/* 1 gauche, 2 droit, 4 milieu */
void amb_mouse_buttons(int mask);
/* bits RETRO_DEVICE_ID_JOYPAD_* */
void amb_joy(int port, unsigned mask);
void amb_key(int down, unsigned retrok);
void amb_port_device(unsigned port, unsigned device);

/* Chemin vide : éjecte */
void amb_insert_disk(int drive, const char *path);
int amb_save_state(const char *path);
int amb_load_state(const char *path);
void amb_floppy_sound(int on);

/* MIDI du port série : sortie et entrée ouvertes dans le cœur ; octets joués par l'Amiga, octets d'un clavier MIDI */
void amb_midi_enable(int out, int in);
unsigned amb_midi_read(uint8_t *dst, unsigned max);
void amb_midi_write(const uint8_t *src, unsigned len);

/* Images calculées et temps de calcul (µs) depuis le dernier appel */
unsigned amb_frames(void);
double amb_busy_us(void);
double amb_fps(void);

#ifdef __cplusplus
}
#endif
