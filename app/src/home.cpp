// Écran d'accueil : machine, Kickstart, disques, bouton « Allumer » (même rôle que les accueils Android et Mac).
// L'émulation tourne dans un second processus (le cœur garde un état global) : l'accueil reste ouvert.
#include "app.h"
#include "config.h"
#include "kickstarts.h"
#include "platform.h"
#include "store.h"
#include "ui.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <functional>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct Home {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    bool quit = false;
    Settings &s = Settings::shared();
    long emuPid = 0;
    // Message à montrer (titre, texte), l'un après l'autre
    std::vector<std::pair<std::string, std::string>> alerts;
    std::string crashReport;
    std::vector<Rom> roms;
    double nextCheck = 0;
    int busy = 6;

    void alert(const std::string &t, const std::string &m) { alerts.push_back({t, m}); }
    void refresh() { s.load(); roms = kickstarts::list(); }

    bool cdtvReady() { Rom a, b; return kickstarts::cdtvExt(a) && kickstarts::best("CDTV", b); }

    std::string summary() {
        std::string m = s.model();
        bool big = Settings::bigBox(m), console = Settings::cdConsole(m);
        std::vector<std::string> parts = {console ? m : "Amiga " + m.substr(1)};
        if (big) parts.push_back(s.fastCpu() ? "68040 rapide" : s.accel() == "030" ? "68030" : s.accel() == "040" ? "68040" : m == "A1200" ? "68020" : "68030");
        if (big && s.rtg()) parts.push_back("RTG");
        Rom k;
        if (m == "CD32") parts.push_back(kickstarts::cd32Rom() ? "ROM CD32" : "ROM CD32 manquante");
        else if (m == "CDTV") parts.push_back(cdtvReady() ? "Kickstart + extension CDTV" : "ROM du CDTV manquante");
        else if (config::kick(s, m, k)) { std::string l = k.label(); parts.push_back(l.substr(0, l.find(" ("))); }
        else parts.push_back("AROS");
        if (!console) {
            if (!s.hds().empty()) parts.push_back(std::to_string(s.hds().size()) + (s.hds().size() > 1 ? " disques durs" : " disque dur"));
            if (!s.floppy(0).empty()) parts.push_back((Settings::isWhd(s.floppy(0)) ? "WHDLoad : " : "DF0: ") + paths::name(s.floppy(0)));
        } else parts.push_back(s.cd().empty() ? "sans CD" : "CD : " + paths::name(s.cd()));
        std::string r;
        for (size_t i = 0; i < parts.size(); i++) r += (i ? " · " : "") + parts[i];
        return r;
    }

    // Fichiers choisis ou glissés : un Kickstart est installé, un disque dur branché, une disquette mise dans DF0:
    void addFiles(const std::vector<std::string> &files) {
        for (auto &f : files) {
            std::string e = paths::ext(f);
            std::error_code ec;
            auto size = fs::file_size(fs::u8path(f), ec);
            if (e == "rom" || ((size == 262144 || size == 524288 || size == 1048576) && e != "adf" && e != "iso")) { installRom(f); continue; }
            if (e == "hdf" || e == "hdz" || e == "vhd") { auto h = s.hds(); if (std::find(h.begin(), h.end(), f) == h.end() && h.size() < 8) { h.push_back(f); s.setList("hds", h); } continue; }
            if (e == "iso" || e == "chd" || e == "cue") { s.set("cd", f); continue; }
            s.setFloppy(0, f);
            s.applyProfile();
        }
        refresh();
    }

    void installRom(const std::string &p) {
        std::string why;
        if (!kickstarts::install(p, why)) alert("Kickstart refusé", paths::name(p) + " : " + why);
        else s.set("rom", "user");
        refresh();
    }

    void addHds() {
        auto list = s.hds();
        for (auto &p : platform::openFiles("Disques durs .hdf (8 au plus)", true, {"Disques durs|*.hdf;*.hdz;*.vhd"}))
            if (std::find(list.begin(), list.end(), p) == list.end() && list.size() < 8) list.push_back(p);
        // Le Workbench d'AGS démarre : placé en premier
        std::stable_sort(list.begin(), list.end(), [](const std::string &a, const std::string &b) {
            auto wb = [](const std::string &x) { std::string n = paths::name(x); std::transform(n.begin(), n.end(), n.begin(), ::tolower); return n == "workbench.hdf" ? 0 : 1; };
            return wb(a) < wb(b);
        });
        s.setList("hds", list);
    }

    void stockMemory() { s.setInt("chip", 0); s.setInt("slow", -1); s.setInt("fast", 0); s.set("accel", "none"); }

    // L'émulation dans un nouveau processus de cette application
    void launch(const std::string &content, const std::string &game = "", const std::string &hdf = "", bool resume = false) {
        std::vector<std::string> args = {"--emu", content};
        if (resume) args.push_back("--resume");
        emuPid = platform::spawnSelf(args, {{"AMIGA1200_JEU", game}, {"AMIGA1200_HDF", hdf}});
        if (!emuPid) alert("Démarrage impossible", "L'émulation n'a pas pu être lancée.");
    }

    void powerOn() {
        std::string m = s.model();
        if (m == "CD32" && !kickstarts::cd32Rom()) { alert("ROM de la CD32 nécessaire", "La CD32 démarre sur sa propre ROM : ajoutez kick40060.CD32 (puis son extension), ou une ROM complète de 1 Mo."); return; }
        if (m == "CDTV" && !cdtvReady()) { alert("ROM du CDTV nécessaires", "Le CDTV démarre sur un Kickstart 1.3 et sur son extension (ROM de 256 Ko)."); return; }
        if (Settings::isWhd(s.floppy(0)) && !kickstarts::hasForWhd()) { alert("Kickstart nécessaire", "WHDLoad a besoin d'un vrai Kickstart (3.1 de l'A1200 de préférence) : AROS ne lui suffit pas."); return; }
        launch(config::content(s));
    }

    void resumeGame() {
        std::string g = s.get("resumeGame");
        std::vector<std::string> p;
        size_t a = 0, b;
        while ((b = g.find('\n', a)) != std::string::npos) { p.push_back(g.substr(a, b - a)); a = b + 1; }
        p.push_back(g.substr(a));
        if (p.size() == 3 && p[0] == "whd") launch(p[1], "", "", true);
        else if (p.size() == 3 && p[0] == "hdf") launch(config::write(s, false, p[1]), p[2], p[1], true);
        else launch(config::content(s), "", "", true);
    }

    // ---------- éléments ----------
    bool card(const char *id, const char *title) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * scale, 12 * scale));
        // Le contenu est toujours donné : une carte qui s'ajuste à sa hauteur part de zéro à sa première image
        ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PushFont(ui::bold);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Spacing();
        return true;
    }
    void endCard() { ImGui::EndChild(); ImGui::Spacing(); }
    bool check(const char *label, bool v, const std::function<void(bool)> &set) {
        bool b = v;
        if (ImGui::Checkbox(label, &b)) { set(b); return true; }
        return false;
    }
    void combo(const char *label, const std::vector<std::pair<int, const char *>> &items, int cur, const std::function<void(int)> &set, float w) {
        const char *preview = items[0].second;
        for (auto &[v, n] : items) if (v == cur) preview = n;
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo(label, preview)) {
            for (auto &[v, n] : items) if (ImGui::Selectable(n, v == cur)) set(v);
            ImGui::EndCombo();
        }
    }
    float scale = 1;

    void draw() {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##accueil", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        std::string m = s.model();
        bool big = Settings::bigBox(m), console = Settings::cdConsole(m);

        // En-tête : titre, résumé de la machine, Allumer et Reprendre
        ImGui::PushFont(ui::title);
        ImGui::TextColored(ImVec4(0.96f, 0.55f, 0.13f, 1), "Amiga 1200");
        ImGui::PopFont();
        ImGui::TextDisabled("%s", summary().c_str());
        ImGui::Spacing();
        if (ui::primaryButton("▶  Allumer l'Amiga", ImVec2(230 * scale, 40 * scale))) powerOn();
        std::string rf = Settings::resumeFile();
        std::string g = s.get("resumeGame");
        bool gameResume = std::count(g.begin(), g.end(), '\n') == 2;
        if (paths::exists(rf) && (gameResume || s.get("resumeSig") == s.signature())) {
            ImGui::SameLine();
            std::error_code ec;
            auto ft = fs::last_write_time(fs::u8path(rf), ec);
            auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            time_t tt = std::chrono::system_clock::to_time_t(sys);
            char d[32]; strftime(d, sizeof d, "%d/%m %H:%M", localtime(&tt));
            std::string name = gameResume ? g.substr(g.rfind('\n') + 1) : "";
            if (!name.empty()) name = name.substr(0, name.rfind('.')) + " ";
            std::string label = "↻  Reprendre " + (gameResume ? name : std::string("où j'en étais ")) + "(" + d + ")";
            if (ImGui::Button(label.c_str(), ImVec2(0, 40 * scale))) resumeGame();
        }
        ImGui::Spacing();

        if (card("pre", "Préréglages")) {
            if (ImGui::Button("AGS / Workbench")) { s.setModel("A1200"); s.setFastCpu(true); s.setRtg(true); }
            ImGui::SameLine();
            if (ImGui::Button("A1200 d'origine")) { s.setModel("A1200"); s.setFastCpu(false); s.setRtg(false); stockMemory(); }
            ImGui::SameLine();
            if (ImGui::Button("Amiga 500")) { s.setModel("A500"); s.setFastCpu(false); s.setRtg(false); stockMemory(); }
            ImGui::SameLine();
            if (ImGui::Button("CD32")) s.setModel("CD32");
        }
        endCard();

        if (card("kick", "Kickstart")) {
            Rom k;
            bool hk = config::kick(s, m, k);
            std::string name = m == "A500" ? "l'Amiga 500" : "l'" + m;
            if (m == "CD32") ImGui::TextWrapped("%s", kickstarts::cd32Rom() ? "CD32 : ✓ ROM de la console" : "La CD32 a besoin de sa ROM (kick40060.CD32 + extension, ou ROM complète de 1 Mo).");
            else if (m == "CDTV") ImGui::TextWrapped("%s", cdtvReady() ? "CDTV : ✓ Kickstart et extension" : "Le CDTV a besoin d'un Kickstart 1.3 et de son extension (ROM de 256 Ko).");
            else if (hk) ImGui::TextWrapped("Pour %s : %s", name.c_str(), k.label().c_str());
            else if (s.rom() == "aros" && !roms.empty()) ImGui::TextWrapped("AROS (ROM libre) choisi à la place de vos Kickstart.");
            else if (!roms.empty()) ImGui::TextWrapped("⚠ Aucun de vos Kickstart ne convient à %s : AROS démarre à sa place.", name.c_str());
            else ImGui::TextWrapped("AROS, ROM libre fournie. Pour les jeux, WHDLoad et AGS, ajoutez vos Kickstart : 3.1 pour l'A1200, 1.3 pour l'Amiga 500.");
            int i = 0;
            for (auto &r : roms) {
                ImGui::PushID(i++);
                ImGui::Text("%s %s", r.path == k.path && hk ? "✓" : "•", r.label().c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30 * scale + ImGui::GetCursorPosX() - ImGui::GetCursorPosX());
                if (ImGui::SmallButton("✕")) { kickstarts::remove(r); refresh(); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (kickstarts::cd32Rom()) ImGui::Text("• ROM de la CD32%s", paths::exists(paths::sys() + "cd32ext.rom") ? " et son extension" : "");
            if (ImGui::Button("Ajouter des Kickstart…"))
                for (auto &p : platform::openFiles("Kickstart (fichiers ROM : 1.3 pour l'A500, 3.1 pour l'A1200, ROM de la CD32, extension du CDTV…)", true, {"ROM|*.rom;*.bin;*.a500;*.a1200;*.a4000;*.cd32"})) installRom(p);
            if (!roms.empty()) {
                ImGui::SameLine();
                if (ImGui::Button(s.rom() == "aros" ? "Utiliser mes Kickstart" : "Utiliser AROS")) s.set("rom", s.rom() == "aros" ? "user" : "aros");
            }
            ui::note("Installez-en plusieurs : chacun sert à la machine qui lui convient, reconnu à son contenu (1.3 pour l'Amiga 500, 3.1 pour l'A1200, 3.1.4 et 3.2 compris).");
        }
        endCard();

        if (card("disques", "Disques")) {
            for (int d = 0; d < 2; d++) {
                ImGui::PushID(d);
                std::string f = s.floppy(d);
                ImGui::Text("DF%d: %s", d, f.empty() ? "vide" : paths::name(f).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Choisir…")) {
                    auto r = platform::openFiles(d == 0 ? "Disquette DF0: (ADF, DMS, IPF…) ou jeu WHDLoad (.lha)" : "Disquette DF1:", false,
                                                 {"Disquettes et jeux WHDLoad|*.adf;*.adz;*.dms;*.ipf;*.scp;*.zip;*.lha;*.lzh;*.lzx"});
                    if (!r.empty()) { s.setFloppy(d, r[0]); if (d == 0) s.applyProfile(); }
                }
                if (!f.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("Éjecter")) s.setFloppy(d, ""); }
                ImGui::PopID();
            }
            ImGui::Text("CD : %s", s.cd().empty() ? "aucun" : paths::name(s.cd()).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Choisir un CD…")) { auto r = platform::openFiles("CD de la CD32 ou du CDTV (.iso, .chd, .cue)", false, {"CD|*.iso;*.chd;*.cue"}); if (!r.empty()) s.set("cd", r[0]); }
            auto hds = s.hds();
            if (hds.empty()) ImGui::TextUnformatted("Disques durs : aucun");
            for (size_t i = 0; i < hds.size(); i++) ImGui::Text("DH%zu: %s", i, paths::name(hds[i]).c_str());
            if (ImGui::Button("Ajouter des disques durs…")) addHds();
            ImGui::SameLine();
            if (ImGui::Button("Retirer les disques durs")) s.setList("hds", {});
            ui::note("Un jeu WHDLoad (.lha) mis dans DF0: se lance seul. Pour AGS, ajoutez tous ses .hdf d'un coup (Workbench.hdf passe en premier). Vous pouvez aussi glisser vos fichiers sur cette fenêtre.");
        }
        endCard();

        if (card("machine", "Machine")) {
            static const char *models[] = {"A500", "A600", "A1200", "A3000", "A4000", "CD32", "CDTV"};
            ImGui::SetNextItemWidth(160 * scale);
            if (ImGui::BeginCombo("Modèle", m.c_str())) {
                for (auto x : models) if (ImGui::Selectable(x, m == x)) s.setModel(x);
                ImGui::EndCombo();
            }
            check("Processeur rapide : 68040 à toute vitesse, 64 Mo de Fast (A1200, A3000, A4000)", s.fastCpu(), [&](bool v) { s.setFastCpu(v); });
            check("Carte graphique RTG : haute résolution d'AGS et du Workbench", s.rtg(), [&](bool v) { s.setRtg(v); });
            check("Volume SHARED: (Documents/Amiga1200/SHARED)", s.sharedVol(), [&](bool v) { s.setBool("shared", v); });
            check("NTSC 60 Hz (sinon PAL 50 Hz)", s.ntsc(), [&](bool v) { s.setBool("ntsc", v); });
            check("Bruit des lecteurs de disquettes", s.floppySound(), [&](bool v) { s.setBool("floppySound", v); });
            if (!console) {
                float w = 150 * scale;
                combo("Chip", {{0, "Celle du modèle"}, {1, "512 Ko"}, {2, "1 Mo"}, {4, "2 Mo"}}, s.chip(), [&](int v) { s.setInt("chip", v); s.saveProfile(); }, w);
                ImGui::SameLine();
                combo("Slow", {{-1, "Celle du modèle"}, {0, "Aucune"}, {2, "512 Ko"}, {4, "1 Mo"}}, s.slow(), [&](int v) { s.setInt("slow", v); s.saveProfile(); }, w);
                ImGui::SameLine();
                combo("Fast", {{0, "Aucune"}, {2, "2 Mo"}, {4, "4 Mo"}, {8, "8 Mo"}}, s.fast(), [&](int v) { s.setInt("fast", v); s.saveProfile(); }, w);
                if (big) {
                    int a = s.accel() == "030" ? 1 : s.accel() == "040" ? 2 : 0;
                    combo("Processeur (sans le mode rapide)", {{0, "Celui du modèle"}, {1, "Carte 68030 + FPU 68882"}, {2, "Carte 68040"}}, a,
                          [&](int v) { s.set("accel", v == 1 ? "030" : v == 2 ? "040" : "none"); s.saveProfile(); }, 230 * scale);
                }
            }
            ui::note("Un jeu qui affiche « Error code 103 » manque de mémoire ; certains vieux jeux A500 ne marchent qu'avec 512 Ko de Chip et sans Slow. Le mode rapide a déjà 64 Mo de Fast et un 68040.");
        }
        endCard();

        if (card("image", "Image")) {
            combo("Filtre", {{0, "Lisse"}, {1, "Nette (pixels francs)"}, {2, "Écran cathodique (lignes de balayage)"}}, s.filter(), [&](int v) { s.setInt("filter", v); }, 300 * scale);
            check("Pixels réguliers : agrandissement en multiple entier", s.integer(), [&](bool v) { s.setBool("integer", v); });
            check("Image sur toute la fenêtre (sinon en 4:3)", s.fill(), [&](bool v) { s.setBool("fill", v); });
        }
        endCard();

        ui::note("Pendant le jeu : cliquez dans l'image pour capturer la souris, F12 pour la libérer et voir le menu (disquettes, sauvegarde rapide Ctrl+S / Ctrl+L, image, plein écran F11, réinitialisation). Touches Amiga : Windows ou Page préc./suiv. ; Help : Inser.");
        ImGui::End();

        // Messages
        if (!alerts.empty() && !ImGui::IsPopupOpen("##alerte")) ImGui::OpenPopup("##alerte");
        ImGui::SetNextWindowSizeConstraints(ImVec2(360 * scale, 0), ImVec2(560 * scale, io.DisplaySize.y * 0.8f));
        if (ImGui::BeginPopupModal("##alerte", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            if (!alerts.empty()) {
                ImGui::PushFont(ui::bold);
                ImGui::TextUnformatted(alerts[0].first.c_str());
                ImGui::PopFont();
                ImGui::PushTextWrapPos(520 * scale);
                ImGui::TextUnformatted(alerts[0].second.c_str());
                ImGui::PopTextWrapPos();
            }
            if (ui::primaryButton("OK", ImVec2(100 * scale, 0))) { if (!alerts.empty()) alerts.erase(alerts.begin()); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        // Plantage de l'émulation : rapport proposé
        if (!crashReport.empty() && !ImGui::IsPopupOpen("##plantage") && alerts.empty()) ImGui::OpenPopup("##plantage");
        if (ImGui::BeginPopupModal("##plantage", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::PushFont(ui::bold);
            ImGui::TextUnformatted("L'émulation s'est arrêtée brutalement");
            ImGui::PopFont();
            ImGui::PushTextWrapPos(520 * scale);
            ImGui::TextUnformatted("Un rapport a été préparé (la fin du journal). Transmettez-le pour aider à corriger le problème.");
            ImGui::PopTextWrapPos();
            if (ui::primaryButton("Signaler sur GitHub…")) {
                SDL_SetClipboardText(crashReport.c_str());
                platform::open("https://github.com/laurent-67370/amiga1200-pc/issues/new?title=Plantage%20de%20l%27%C3%A9mulation&body=Ce%20que%20je%20faisais%20%3A%0A%0A%0A(Le%20rapport%20est%20copi%C3%A9%20%3A%20collez-le%20ici%20avec%20Ctrl%2BV.)");
            }
            ImGui::SameLine();
            if (ImGui::Button("Copier le rapport")) SDL_SetClipboardText(crashReport.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Ouvrir le rapport")) platform::open(crashFile());
            ImGui::SameLine();
            if (ImGui::Button("Fermer")) { crashReport.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    int run() {
        paths::installResources();
        win = SDL_CreateWindow("Amiga 1200", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 760, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!win) { platform::message("Démarrage impossible", SDL_GetError()); return 1; }
        SDL_SetWindowMinimumSize(win, 560, 480);
        ui::setIcon(win);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        scale = ui::displayScale(win);
        ui::init(win, ren, scale);
        refresh();
        crashCheck(crashReport);
        while (!quit) {
            SDL_Event e;
            // Au repos, l'accueil attend les événements (pas de calcul inutile) ; quelques images suivent chaque
            // événement, le temps que la mise en page se stabilise
            if (busy > 0 ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 500)) {
                busy = 6;
                do {
                    ImGui_ImplSDL2_ProcessEvent(&e);
                    if (e.type == SDL_QUIT) quit = true;
                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) refresh();
                    if (e.type == SDL_DROPFILE) { addFiles({e.drop.file}); SDL_free(e.drop.file); }
                } while (SDL_PollEvent(&e));
            }
            // Fin de l'émulation : réglages relus (reprise), plantage éventuel
            if (emuPid && SDL_GetTicks64() / 1000.0 > nextCheck) {
                nextCheck = SDL_GetTicks64() / 1000.0 + 1;
                if (!platform::processAlive(emuPid)) { emuPid = 0; refresh(); crashCheck(crashReport); }
            }
            if (busy > 0) busy--;
            ui::newFrame();
            draw();
            SDL_SetRenderDrawColor(ren, 28, 28, 31, 255);
            SDL_RenderClear(ren);
            ui::render(ren);
            SDL_RenderPresent(ren);
        }
        ui::shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return 0;
    }
};
}  // namespace

int runHome() {
    Home h;
    return h.run();
}
