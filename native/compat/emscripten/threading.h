/* Remplaçant d'Emscripten pour macOS (voir emscripten.h) */
#pragma once
#include <sched.h>

/* Sous Emscripten, le fil principal traite ici les accès aux fichiers relayés par le cœur ; sous macOS, le cœur
   lit ses fichiers lui-même : on cède seulement la main en attendant le fil de l'émulation */
static inline void emscripten_main_thread_process_queued_calls(void) { sched_yield(); }
