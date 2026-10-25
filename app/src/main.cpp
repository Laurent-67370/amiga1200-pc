// Amiga 1200 pour Windows et Linux. Trois façons de démarrer :
//   (sans argument)               l'écran d'accueil
//   --emu <contenu> [--resume]    l'émulation (lancée par l'accueil, dans son propre processus), reprise ou non
//   --selftest <images>           essai sans fenêtre : AROS sur un A1200, pour la compilation automatique
#include "app.h"
#include "bridge.h"
#include "config.h"
#include "platform.h"
#include "store.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Essai sans fenêtre ni son : le cœur démarre AROS sur un A1200 (68040 rapide, RTG), tourne sans frein, et l'on
// vérifie qu'il calcule des images et qu'elles ne sont pas noires
int runSelfTest(int frames) {
    Settings &s = Settings::shared();
    s.setModel("A1200"); s.setFastCpu(true); s.setRtg(true); s.setBool("floppySound", false);
    paths::installResources();
    amb_mute_audio(1);
    amb_set_log(paths::log().c_str());
    std::string cfg = config::write(s, true);
    if (!coreStart(cfg)) { printf("ÉCHEC : le fil de l'émulation n'a pas démarré\n"); return 1; }
    Uint64 t0 = SDL_GetTicks64();
    while (amb_state() == 1 && SDL_GetTicks64() - t0 < 60000) SDL_Delay(50);
    if (amb_state() != 2) { printf("ÉCHEC : le cœur a refusé la configuration (état %d)\n", amb_state()); return 1; }
    amb_turbo(1);
    Uint64 t1 = SDL_GetTicks64();
    amb_frames(); amb_busy_us();
    int done = 0;
    while (done < frames && SDL_GetTicks64() - t1 < 180000) { SDL_Delay(100); done += (int)amb_frames(); }
    double secs = (SDL_GetTicks64() - t1) / 1000.0;
    unsigned w = 0, h = 0;
    long lit = 0;
    if (const uint8_t *p = amb_frame(&w, &h); p && w && h)
        for (size_t i = 0; i < (size_t)w * h; i += 7) if (p[i * 4] > 16 || p[i * 4 + 1] > 16 || p[i * 4 + 2] > 16) lit++;
    amb_stop();
    SDL_Delay(200);
    printf("Essai : %d images en %.1f s (%.0f images/s sans frein), image %ux%u, %ld points allumés\n", done, secs, done / (secs > 0 ? secs : 1), w, h, lit);
    std::ifstream log(paths::log());
    std::string line;
    while (std::getline(log, line)) if (line.find("CPU=") != std::string::npos || line.find("JIT") != std::string::npos) { printf("Cœur : %s\n", line.c_str()); break; }
    if (done < frames) { printf("ÉCHEC : trop peu d'images\n"); return 1; }
    if (!w || !h || !lit) { printf("ÉCHEC : image vide\n"); return 1; }
    printf("OK\n");
    return 0;
}

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto has = [&](const char *a) { for (auto &x : args) if (x == a) return true; return false; };
    auto after = [&](const char *a) -> std::string {
        for (size_t i = 0; i + 1 < args.size(); i++) if (args[i] == a) return args[i + 1];
        return "";
    };
    platform::initTimers();
    SDL_SetHint(SDL_HINT_APP_NAME, "Amiga 1200");
    // Écrans haute résolution sous Windows : l'application dessine elle-même à la taille réelle (police nette)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    // Touche Windows et raccourcis du système capturés quand la souris est capturée (voir l'émulation)
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
    if (has("--selftest")) {
        if (SDL_Init(SDL_INIT_TIMER) != 0) { printf("SDL : %s\n", SDL_GetError()); return 1; }
        std::string n = after("--selftest");
        int r = runSelfTest(n.empty() ? 600 : std::atoi(n.c_str()));
        SDL_Quit();
        return r;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        platform::message("Amiga 1200", std::string("SDL ne démarre pas : ") + SDL_GetError());
        return 1;
    }
    int r;
    std::string content = after("--emu");
    if (!content.empty()) r = runEmu(content, has("--resume"));
    else r = runHome();
    SDL_Quit();
    return r;
}
