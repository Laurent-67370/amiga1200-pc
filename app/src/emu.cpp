// Fenêtre de l'émulation (processus lancé par l'accueil avec --emu) : l'image, le son, le clavier, la souris, les
// manettes, et un menu (F12 libère la souris et l'affiche)
#include "app.h"
#include "bridge.h"
#include "config.h"
#include "extras.h"
#include "library.h"
#include "keymap.h"
#include "platform.h"
#include "store.h"
#include "ui.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <chrono>
#include <cmath>
#include <map>
#include <string>
#include <thread>

namespace {
SDL_AudioDeviceID audioDev;

void audioCallback(void *, Uint8 *stream, int len) { amb_audio_render((float *)stream, (unsigned)len / (2 * sizeof(float))); }
void audioOpen(double rate) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { amb_log("son : SDL indisponible"); return; }
    SDL_AudioSpec want{}, have{};
    want.freq = (int)rate; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 512;
    want.callback = audioCallback;
    audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audioDev) { amb_log("son : aucune sortie"); return; }
    SDL_PauseAudioDevice(audioDev, 0);
    char m[160];
    snprintf(m, sizeof m, "son : SDL (%s), cœur %.0f Hz", SDL_GetCurrentAudioDriver(), rate);
    amb_log(m);
}
void audioClose() {
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    audioDev = 0;
}
}  // namespace

bool coreStart(const std::string &content) {
    paths::installResources();
    amb_set_log(paths::log().c_str());
    amb_log(("amiga1200-pc " AMIGA1200_VERSION ", " + std::string(SDL_GetPlatform())).c_str());
    amb_log(("Contenu : " + content).c_str());
    // Bruits des lecteurs : le cœur cherche data/floppy_sounds dans AMIBERRY_DATA_DIR
    SDL_setenv("AMIBERRY_DATA_DIR", paths::data().c_str(), 1);
    amb_set_dirs(paths::sys().c_str(), paths::save().c_str());
    amb_set_option("amiberry_crop_overscan", "enabled");
    amb_set_option("amiberry_statusline", "disabled");
    amb_set_option("amiberry_joyport_order", "21");
    Settings &s = Settings::shared();
    amb_set_option("amiberry_midi_output", s.midi() ? "libretro" : "disabled");
    // Jeu WHDLoad dans une archive autre que .lha (dossier zippé, .lzx…) : le cœur la lance comme une .lha
    std::string e = paths::ext(content);
    amb_set_option("amiberry_whdload_archive", (e == "zip" || e == "7z" || e == "lzx") ? "enabled" : "disabled");
    amb_set_audio(audioOpen, audioClose);
    return amb_start(content.c_str()) != 0;
}

namespace {
struct Emu {
    std::string content;
    bool resume;
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *tex = nullptr;
    int texW = 0, texH = 0;
    unsigned long lastFrame = 0;
    bool captured = false, paused = false, turbo = false, quit = false, restarting = false, showMenu = true;
    int buttons = 0;
    std::string toast;
    double toastUntil = 0;
    std::map<SDL_JoystickID, SDL_GameController *> pads;
    std::map<SDL_JoystickID, int> players;
    std::string game;

    static double now() { return SDL_GetTicks64() / 1000.0; }
    void flash(const std::string &t) { toast = t; toastUntil = now() + 2.5; }

    // Jeu en cours (sauvegardes, cadrage) : l'archive WHDLoad, sinon la disquette DF0:, sinon le premier disque dur
    std::string gameName() {
        if (paths::ext(content) != "uae") return paths::name(content);
        if (const char *g = SDL_getenv("AMIGA1200_JEU")) return g;
        Settings &s = Settings::shared();
        std::string p = !s.floppy(0).empty() ? s.floppy(0) : s.hds().empty() ? "" : s.hds()[0];
        return p.empty() ? "" : paths::name(p);
    }
    std::string stateKey() { std::string g = gameName(); return g.empty() ? Settings::shared().model() : g; }

    void capture() {
        if (captured) return;
        captured = true;
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }
    void release() {
        if (!captured) return;
        captured = false;
        buttons = 0; amb_mouse_buttons(0);
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }

    // Image : 4:3 centrée, ou toute la fenêtre ; pixels réguliers : multiple entier ; cadrage du jeu (zoom, décalage)
    SDL_Rect screenRect(int ow, int oh) {
        Settings &s = Settings::shared();
        double w = ow, h = oh;
        if (texW > 0 && s.integer()) {
            int k = std::min(ow / texW, oh / texH);
            if (k >= 1) { w = texW * k; h = texH * k; }
        } else if (!s.fill()) {
            if (w * 3 > h * 4) w = h * 4 / 3; else h = w * 3 / 4;
        }
        double zoom = game.empty() ? 1 : s.getDouble("zoom:" + game, 1), shift = game.empty() ? 0 : s.getDouble("decalage:" + game, 0);
        w *= zoom; h *= zoom;
        return SDL_Rect{(int)std::lround((ow - w) / 2), (int)std::lround((oh - h) / 2 + shift * h), (int)std::lround(w), (int)std::lround(h)};
    }

    void drawFrame() {
        unsigned w = 0, h = 0;
        const uint8_t *p = amb_frame(&w, &h);
        unsigned long fc = amb_frame_count();
        if (p && w > 0 && h > 0 && fc != lastFrame) {
            lastFrame = fc;
            if ((int)w != texW || (int)h != texH || !tex) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
                texW = (int)w; texH = (int)h;
            }
            SDL_UpdateTexture(tex, nullptr, p, (int)w * 4);
        }
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (!tex) return;
        Settings &s = Settings::shared();
        SDL_SetTextureScaleMode(tex, s.filter() == 0 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        // Menu affiché : l'image prend la place sous sa barre
        int top = !captured && showMenu ? (int)(ImGui::GetFrameHeight() * ow / std::max(1.0f, ImGui::GetIO().DisplaySize.x)) : 0;
        SDL_Rect r = screenRect(ow, oh - top);
        r.y += top;
        SDL_RenderCopy(ren, tex, nullptr, &r);
        // Écran cathodique : une ligne de l'Amiga sur deux assombrie
        if (s.filter() == 2 && texH > 0) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 90);
            double lh = (double)r.h / texH;
            if (lh >= 1.5) for (int y = 1; y < texH; y += 2) {
                SDL_Rect line{r.x, r.y + (int)(y * lh), r.w, std::max(1, (int)(lh))};
                SDL_RenderFillRect(ren, &line);
            }
        }
    }

    // Manettes : la première joue sur le port joystick, la deuxième sur le port souris
    void padAdded(int index) {
        SDL_GameController *c = SDL_GameControllerOpen(index);
        if (!c) return;
        SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
        pads[id] = c;
        players[id] = std::min((int)players.size(), 1);
        flash(std::string("Manette : ") + (SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "?"));
    }
    void padRemoved(SDL_JoystickID id) {
        if (pads.count(id)) { SDL_GameControllerClose(pads[id]); pads.erase(id); amb_joy(players[id], 0); players.erase(id); }
    }
    void padUpdate(SDL_JoystickID id) {
        auto it = pads.find(id);
        if (it == pads.end()) return;
        SDL_GameController *c = it->second;
        auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(c, x) != 0; };
        int ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        unsigned m = 0;
        if (b(SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) m |= 1 << 4;
        if (b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) m |= 1 << 5;
        if (b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) m |= 1 << 6;
        if (b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) m |= 1 << 7;
        if (b(SDL_CONTROLLER_BUTTON_A)) m |= 1 << 0;            // tir
        if (b(SDL_CONTROLLER_BUTTON_B)) m |= 1 << 8;            // 2e bouton
        if (b(SDL_CONTROLLER_BUTTON_X)) m |= 1 << 1;
        if (b(SDL_CONTROLLER_BUTTON_Y)) m |= 1 << 9;
        if (b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= 1 << 10;
        if (b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= 1 << 11;
        if (b(SDL_CONTROLLER_BUTTON_START)) m |= 1 << 3;
        if (b(SDL_CONTROLLER_BUTTON_BACK)) m |= 1 << 2;
        amb_joy(players[id], m);
    }

    // ---------- actions du menu ----------
    void togglePause() { paused = !paused; amb_pause(paused ? 1 : 0); flash(paused ? "Pause" : "Reprise"); }
    void toggleTurbo() { turbo = !turbo; amb_turbo(turbo ? 1 : 0); flash(turbo ? "Turbo" : "Vitesse normale"); }
    // Emplacement 1 du jeu en cours (sinon de la machine)
    void quickSave() { flash(states::save(stateKey(), 0) ? "État 1 sauvegardé" : "Sauvegarde impossible"); }
    void quickLoad() { flash(states::load(stateKey(), 0) ? "État 1 repris" : "Aucun état à reprendre"); }
    void screenshot() {
        bool ok = captures::save(gameName().empty() ? "Amiga" : gameName(), true);
        flash(ok ? "Capture enregistrée (Images/Amiga1200 et Bibliothèque)" : "Capture impossible");
    }

    // Fenêtre des emplacements : l'image de chacun, sa date, « Sauvegarder » et « Reprendre »
    bool showStates = false;
    SDL_Texture *slotTex[4] = {};
    void loadSlotThumbs() {
        for (int i = 0; i < 4; i++) {
            if (slotTex[i]) { SDL_DestroyTexture(slotTex[i]); slotTex[i] = nullptr; }
            std::vector<uint8_t> px;
            int w, h;
            if (captures::load(states::thumb(stateKey(), i), px, w, h)) {
                slotTex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
                if (slotTex[i]) SDL_UpdateTexture(slotTex[i], nullptr, px.data(), w * 4);
            }
        }
    }
    void openStates() { release(); loadSlotThumbs(); showStates = true; }
    void statesWindow() {
        if (!showStates) return;
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        std::string title = "Sauvegardes d'état : " + states::key(stateKey()) + "###etats";
        if (!ImGui::Begin(title.c_str(), &showStates, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
        float s = ImGui::GetFontSize() / 15.0f;
        for (int i = 0; i < 4; i++) {
            ImGui::PushID(i);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (slotTex[i]) ImGui::Image((ImTextureID)(intptr_t)slotTex[i], ImVec2(96 * s, 72 * s));
            else { ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + 96 * s, p0.y + 72 * s), IM_COL32(50, 50, 56, 255), 4 * s); ImGui::Dummy(ImVec2(96 * s, 72 * s)); }
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string d = states::date(stateKey(), i);
            ImGui::Text("Emplacement %d", i + 1);
            ImGui::TextDisabled("%s", d.empty() ? "vide" : d.c_str());
            if (ui::primaryButton("Sauvegarder")) { flash(states::save(stateKey(), i) ? "État " + std::to_string(i + 1) + " sauvegardé" : "Sauvegarde impossible"); showStates = false; }
            ImGui::SameLine();
            ImGui::BeginDisabled(d.empty());
            if (ImGui::Button("Reprendre")) { flash(states::load(stateKey(), i) ? "État " + std::to_string(i + 1) + " repris" : "Aucun état à reprendre"); showStates = false; }
            ImGui::EndDisabled();
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::End();
    }
    void insert(int drive) {
        release();
        auto f = platform::openFiles("Disquette pour DF" + std::to_string(drive) + ":", false,
                                     {"Disquettes|*.adf;*.adz;*.dms;*.ipf;*.scp;*.zip"});
        if (f.empty()) return;
        amb_insert_disk(drive, f[0].c_str());
        Settings::shared().setFloppy(drive, f[0]);
        flash("DF" + std::to_string(drive) + ": " + paths::name(f[0]));
    }
    void eject(int drive) { amb_insert_disk(drive, ""); Settings::shared().setFloppy(drive, ""); flash("DF" + std::to_string(drive) + ": éjectée"); }
    void setFilter(int f) {
        Settings::shared().setInt("filter", f);
        static const char *n[] = {"Image lisse", "Image nette", "Écran cathodique"};
        flash(n[f]);
    }
    void fullScreen() {
        bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    void quitGame() {
        unsigned k = (unsigned)Settings::shared().quitKey();
        amb_key(1, k);
        SDL_AddTimer(100, [](Uint32, void *p) -> Uint32 { amb_key(0, (unsigned)(uintptr_t)p); return 0; }, (void *)(uintptr_t)k);
        flash("Touche de sortie de WHDLoad");
    }
    // Le cœur ne se rallume pas dans le même processus : un nouveau processus prend la suite
    void restart() {
        restarting = true;
        platform::spawnSelf({"--emu", content});
        quit = true;
    }

    void menu() {
        if (captured || !showMenu) return;
        Settings &s = Settings::shared();
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("Amiga")) {
            if (ImGui::MenuItem("Pause", "Ctrl+P", paused)) togglePause();
            if (ImGui::MenuItem("Sauvegarde rapide", "Ctrl+S")) quickSave();
            if (ImGui::MenuItem("Reprise rapide", "Ctrl+L")) quickLoad();
            if (ImGui::MenuItem("Sauvegardes d'état…", "Ctrl+E")) openStates();
            if (ImGui::MenuItem("Capture d'écran", "Ctrl+K")) screenshot();
            ImGui::Separator();
            if (ImGui::MenuItem("Turbo", "Ctrl+T", turbo)) toggleTurbo();
            if (ImGui::MenuItem("Quitter le jeu (touche de sortie de WHDLoad)", "Ctrl+Q")) quitGame();
            if (ImGui::MenuItem("Réinitialiser (redémarrage à froid)", "Ctrl+R")) restart();
            ImGui::Separator();
            if (ImGui::MenuItem("Journal")) platform::open(paths::log());
            if (ImGui::MenuItem("Fermer l'Amiga", "Alt+F4")) quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Disquettes")) {
            for (int i = 0; i < 2; i++) {
                std::string cur = s.floppy(i);
                if (ImGui::MenuItem(("DF" + std::to_string(i) + ":…").c_str(), i == 0 ? "Ctrl+O" : nullptr)) insert(i);
                if (!cur.empty()) ImGui::TextDisabled("   %s", paths::name(cur).c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Éjecter DF0:")) eject(0);
            if (ImGui::MenuItem("Éjecter DF1:")) eject(1);
            ImGui::Separator();
            bool snd = s.floppySound();
            if (ImGui::MenuItem("Bruit des lecteurs", nullptr, snd)) { s.setBool("floppySound", !snd); amb_floppy_sound(!snd); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Image")) {
            int f = s.filter();
            if (ImGui::MenuItem("Lisse", nullptr, f == 0)) setFilter(0);
            if (ImGui::MenuItem("Nette (pixels francs)", nullptr, f == 1)) setFilter(1);
            if (ImGui::MenuItem("Écran cathodique (lignes de balayage)", nullptr, f == 2)) setFilter(2);
            ImGui::Separator();
            if (ImGui::MenuItem("Pixels réguliers (agrandissement entier)", nullptr, s.integer())) s.setBool("integer", !s.integer());
            if (ImGui::MenuItem("Image sur toute la fenêtre (sinon 4:3)", nullptr, s.fill())) s.setBool("fill", !s.fill());
            ImGui::Separator();
            if (ImGui::MenuItem("Plein écran", "F11")) fullScreen();
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("   Cliquez dans l'image pour jouer · F12 : libérer la souris");
        ImGui::EndMainMenuBar();
    }

    void drawToast() {
        if (now() > toastUntil || toast.empty()) return;
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y - 30), ImGuiCond_Always, ImVec2(0.5f, 1));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12);
        ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::TextUnformatted(toast.c_str());
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Raccourcis Ctrl+… tant que la souris est libre (souris capturée, tout va à l'Amiga)
    bool shortcut(const SDL_KeyboardEvent &k) {
        if (k.keysym.scancode == SDL_SCANCODE_F12) { if (k.type == SDL_KEYDOWN) { if (captured) release(); else showMenu = !showMenu; } return true; }
        if (k.keysym.scancode == SDL_SCANCODE_F11) { if (k.type == SDL_KEYDOWN && !k.repeat) fullScreen(); return true; }
        if (captured || !(k.keysym.mod & KMOD_CTRL)) return false;
        if (k.type != SDL_KEYDOWN) return true;
        switch (k.keysym.scancode) {
        case SDL_SCANCODE_P: togglePause(); return true;
        case SDL_SCANCODE_S: quickSave(); return true;
        case SDL_SCANCODE_L: quickLoad(); return true;
        case SDL_SCANCODE_E: openStates(); return true;
        case SDL_SCANCODE_K: screenshot(); return true;
        case SDL_SCANCODE_O: insert(0); return true;
        case SDL_SCANCODE_T: toggleTurbo(); return true;
        case SDL_SCANCODE_Q: quitGame(); return true;
        case SDL_SCANCODE_R: restart(); return true;
        default: return false;
        }
    }

    void stats(double &next) {
        if (now() < next) return;
        next = now() + 1;
        unsigned n = amb_frames();
        double busy = amb_busy_us(), fps = amb_fps() > 0 ? amb_fps() : 50;
        char t[256];
        if (paused) snprintf(t, sizeof t, "Amiga 1200 — en pause");
        else if (turbo) snprintf(t, sizeof t, "Amiga 1200 — turbo : %u images/s", n);
        else snprintf(t, sizeof t, "Amiga 1200 — %u images/s · %d %% · %.1f ms/image", n, (int)(n * 100 / fps), n ? busy / n / 1000 : 0);
        std::string title = t;
        title += captured ? "  (F12 : libérer la souris)" : "  (cliquez pour capturer la souris)";
        SDL_SetWindowTitle(win, title.c_str());
    }

    int run() {
        crashStarted();
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
        win = SDL_CreateWindow("Amiga 1200 — démarrage…", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 800,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!win) { platform::message("Démarrage impossible", SDL_GetError()); return 1; }
        SDL_SetWindowMinimumSize(win, 400, 300);
        ui::setIcon(win);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        ui::init(win, ren, ui::displayScale(win));
        game = stateKey();
        if (!coreStart(content)) { fail("le fil de l'émulation n'a pas pu démarrer"); return 1; }
        double t0 = now(), nextStats = now() + 1;
        bool running = false;
        while (!quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                bool imguiMouse = ImGui::GetIO().WantCaptureMouse && !captured;
                ImGui_ImplSDL2_ProcessEvent(&e);
                switch (e.type) {
                case SDL_QUIT: quit = true; break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) release();
                    break;
                case SDL_KEYDOWN: case SDL_KEYUP: {
                    if (shortcut(e.key)) break;
                    if (!captured && ImGui::GetIO().WantCaptureKeyboard) break;
                    if (e.key.repeat) break;
                    unsigned k = retrok(e.key.keysym.scancode);
                    if (k) amb_key(e.type == SDL_KEYDOWN, k);
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
                    if (!captured) { if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !imguiMouse) capture(); break; }
                    int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                    buttons = e.type == SDL_MOUSEBUTTONDOWN ? buttons | bit : buttons & ~bit;
                    amb_mouse_buttons(buttons);
                    break;
                }
                case SDL_MOUSEMOTION:
                    if (captured && (e.motion.xrel || e.motion.yrel)) amb_mouse_move(e.motion.xrel, e.motion.yrel);
                    break;
                case SDL_CONTROLLERDEVICEADDED: padAdded(e.cdevice.which); break;
                case SDL_CONTROLLERDEVICEREMOVED: padRemoved(e.cdevice.which); break;
                case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: padUpdate(e.cbutton.which); break;
                case SDL_CONTROLLERAXISMOTION: padUpdate(e.caxis.which); break;
                }
            }
            int st = amb_state();
            if (!running) {
                if (st == 2) {
                    running = true;
                    if (resume) {
                        // L'état gardé est rechargé une fois l'Amiga allumé
                        SDL_Delay(1500);
                        std::string f = Settings::resumeFile();
                        bool ok = paths::exists(f) && amb_load_state(f.c_str());
                        std::remove(f.c_str());
                        flash(ok ? "Reprise où vous en étiez" : "Reprise impossible, l'Amiga redémarre normalement");
                    } else std::remove(Settings::resumeFile().c_str());   // les disques changent : l'ancien état ne vaut plus
                } else if (st == 1 && now() - t0 > 60) { fail("le cœur ne répond pas"); return 1; }
                else if (st != 1) { fail("le cœur a refusé la configuration (Kickstart ou disque illisible ?)"); return 1; }
            }
            stats(nextStats);
            ui::newFrame();
            menu();
            statesWindow();
            drawToast();
            drawFrame();
            ui::render(ren);
            SDL_RenderPresent(ren);
        }
        shutdown();
        return 0;
    }

    void fail(const std::string &why) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Démarrage impossible",
                                 (why + "\n\nLe journal est dans " + paths::log()).c_str(), win);
        amb_stop();
        crashEnded();
    }

    void shutdown() {
        Settings &s = Settings::shared();
        // Image du jeu pour la Bibliothèque, s'il n'en a pas encore
        if (amb_state() == 2 && !gameName().empty()) captures::save(gameName(), false, true);
        // « Reprendre où j'en étais » : l'état est gardé, avec ce qu'il faut pour rallumer la même chose
        if (amb_state() == 2 && !restarting && amb_save_state(Settings::resumeFile().c_str())) {
            s.set("resumeSig", s.signature());
            if (paths::ext(content) != "uae") s.set("resumeGame", "whd\n" + content + "\n" + paths::name(content));
            else if (const char *h = SDL_getenv("AMIGA1200_HDF")) {
                const char *g = SDL_getenv("AMIGA1200_JEU");
                s.set("resumeGame", std::string("hdf\n") + h + "\n" + (g ? g : paths::name(h)));
            } else s.set("resumeGame", "");
        }
        amb_stop();
        SDL_Delay(200);   // écriture des disques
        crashEnded();
        for (auto &[id, c] : pads) SDL_GameControllerClose(c);
        for (auto *t : slotTex) if (t) SDL_DestroyTexture(t);
        ui::shutdown();
        if (tex) SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
    }
};
}  // namespace

std::string stateFile(const std::string &game, int slot) {
    std::string k = game;
    auto dot = k.rfind('.');
    if (dot != std::string::npos && dot > 0) k = k.substr(0, dot);
    for (auto &c : k) if (c == '/' || c == '\\' || c == ':') c = '_';
    return k + "-" + std::to_string(slot + 1) + ".uss";
}

int runEmu(const std::string &content, bool resume) {
    Emu e;
    e.content = content;
    e.resume = resume;
    return e.run();
}
