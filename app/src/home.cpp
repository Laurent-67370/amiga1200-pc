// Écran d'accueil : machine, Kickstart, disques, bouton « Allumer » (même rôle que les accueils Android et Mac).
// L'émulation tourne dans un second processus (le cœur garde un état global) : l'accueil reste ouvert.
#include "app.h"
#include "config.h"
#include "extras.h"
#include "kickstarts.h"
#include "library.h"
#include "blankdisk.h"
#include "midi.h"
#include "platform.h"
#include "store.h"
#include "ui.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
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
    std::vector<std::string> dropped;
    // Bibliothèque : onglet, éléments, images des jeux (textures gardées, refaites si l'image change)
    std::string libKind = library::WHD;
    std::vector<LibItem> lib;
    struct Thumb { SDL_Texture *tex = nullptr; int w = 0, h = 0; long long stamp = -1; };
    std::map<std::string, Thumb> thumbs;
    // Analyse des .zip et des .hdf, à part : ses comptes rendus sont repris par la boucle principale
    std::mutex mtx;
    std::vector<std::function<void()>> pending;
    int working = 0;
    char cfgBuf[96] = {0};
    bool moreDrives = false, showHelp = false;
    std::vector<std::vector<std::string>> midiNames;
    std::string importFile;
    void later(std::function<void()> f) { std::lock_guard<std::mutex> l(mtx); pending.push_back(std::move(f)); SDL_Event e{}; e.type = SDL_USEREVENT; SDL_PushEvent(&e); }

    void alert(const std::string &t, const std::string &m) { alerts.push_back({t, m}); }
    void refresh() { s.load(); roms = kickstarts::list(); lib = library::all(); }

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

    // Fichiers choisis ou glissés : un Kickstart est installé, le reste rangé dans la Bibliothèque ; les .zip et les
    // .hdf sont analysés à part (disque dur, jeu WHDLoad ou disquette ; jeu WHDLoad installé sur un disque dur)
    void addFiles(const std::vector<std::string> &files) {
        int added = 0;
        bool roms = false;
        std::string last;
        std::vector<std::string> zips, hdfs;
        for (auto &f : files) {
            std::string k = library::kind(f);
            std::error_code ec;
            auto size = fs::file_size(fs::u8path(f), ec);
            if (k == library::ROM || (k.empty() && (size == 262144 || size == 524288 || size == 1048576))) { installRom(f); roms = true; continue; }
            if (paths::ext(f) == "zip") { zips.push_back(f); added++; continue; }
            if (library::add(f)) { added++; last = library::kind(f); if (last == library::HDF) hdfs.push_back(f); }
        }
        if (!last.empty()) setTab(last);
        if (!files.empty() && added == 0 && !roms)
            alert("Fichiers non reconnus", "La Bibliothèque accepte les jeux WHDLoad (.lha, .zip), les disquettes (ADF, DMS, IPF…), les disques durs (.hdf) et les CD.");
        refresh();
        if (zips.empty() && hdfs.empty()) return;
        working++;
        std::thread([this, zips, hdfs] {
            std::vector<std::pair<std::string, zipimport::Result>> results;
            for (auto &z : zips) results.push_back({z, zipimport::inspect(z)});
            std::vector<std::string> games;
            for (auto &h : hdfs) if (zipimport::bootsWhdload(h)) games.push_back(h);
            later([this, results, games] {
                working--;
                std::string notes;
                auto note = [&](const std::string &n) { notes += (notes.empty() ? "" : "\n\n") + n; };
                for (auto &g : games) { library::add(g, library::WHDHDF); note(paths::name(g) + " : jeu WHDLoad sur disque dur, rangé dans Jeux (▶ Jouer)"); setTab(library::WHD); }
                for (auto &[z, r] : results) {
                    for (auto &f : r.found) {
                        library::add(f.path, f.kind);
                        note(paths::name(f.path) + (f.kind == library::WHDHDF ? " : jeu WHDLoad sur disque dur, rangé dans Jeux (▶ Jouer)"
                                                    : f.kind == library::WHD ? " : jeu WHDLoad, rangé dans Jeux" : " : disque dur, rangé dans Disques durs"));
                        setTab(f.kind == library::HDF ? library::HDF : library::WHD);
                    }
                    if (r.whdArchive) { library::add(z, library::WHD); note(paths::name(z) + " : jeu WHDLoad, rangé dans Jeux (▶ Jouer)"); setTab(library::WHD); }
                    if (!r.problem.empty()) note(r.problem);
                    if (r.found.empty() && r.problem.empty() && !r.whdArchive) { library::add(z, library::FLOPPY); setTab(library::FLOPPY); }
                }
                refresh();
                if (!notes.empty()) alert("Fichiers ajoutés", notes);
            });
        }).detach();
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
        if (!s.floppy(0).empty()) library::add(s.floppy(0));
        for (auto &h : s.hds()) library::add(h);
        launch(config::content(s));
    }

    // Jeu installé sur un .hdf qui démarre WHDLoad tout seul : un A1200 avec Kickstart 3.x et 8 Mo de Fast, ce seul disque
    void playWhdHdf(const LibItem &it) {
        if (!kickstarts::hasForWhd()) { alert("Kickstart nécessaire", "WHDLoad a besoin d'un vrai Kickstart (3.1 de l'A1200 de préférence) : AROS ne lui suffit pas."); return; }
        library::touch(it.path);
        launch(config::write(s, false, it.path), it.name(), it.path);
    }
    void playWhd(const LibItem &it) {
        if (!kickstarts::hasForWhd()) { alert("Kickstart nécessaire", "WHDLoad a besoin d'un vrai Kickstart (3.1 de l'A1200 de préférence) : AROS ne lui suffit pas."); return; }
        library::touch(it.path);
        launch(it.path);
    }
    // CD de la CD32 ou du CDTV : la console démarre dessus (il lui faut ses ROM)
    void playCd(const LibItem &it, const std::string &console) {
        if (console == "CD32" && !kickstarts::cd32Rom()) { alert("ROM de la CD32 nécessaire", "La CD32 démarre sur sa propre ROM : ajoutez kick40060.CD32 (puis son extension), ou une ROM complète de 1 Mo, avec « Ajouter des Kickstart… »."); return; }
        if (console == "CDTV" && !cdtvReady()) { alert("ROM du CDTV nécessaires", "Le CDTV démarre sur un Kickstart 1.3 et sur son extension (ROM de 256 Ko) : ajoutez-les avec « Ajouter des Kickstart… »."); return; }
        library::touch(it.path);
        s.set("cd", it.path); s.setModel(console);
        powerOn();
    }

    // Image d'un jeu (capture), gardée en texture
    Thumb *thumb(const std::string &file) {
        std::error_code ec;
        long long stamp = paths::exists(file) ? (long long)fs::last_write_time(fs::u8path(file), ec).time_since_epoch().count() : 0;
        Thumb &t = thumbs[file];
        if (t.stamp == stamp) return t.tex ? &t : nullptr;
        if (t.tex) { SDL_DestroyTexture(t.tex); t.tex = nullptr; }
        t.stamp = stamp;
        std::vector<uint8_t> px;
        if (stamp && captures::load(file, px, t.w, t.h)) {
            t.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
            if (t.tex) { SDL_UpdateTexture(t.tex, nullptr, px.data(), t.w * 4); SDL_SetTextureScaleMode(t.tex, SDL_ScaleModeLinear); }
        }
        return t.tex ? &t : nullptr;
    }

    void drawLibrary() {
        static const std::pair<const char *, const char *> tabs[] = {{"whd", "Jeux"}, {"floppy", "Disquettes"}, {"hdf", "Disques durs"}, {"cd", "CD"}};
        auto inTab = [](const LibItem &it, const std::string &k) { return it.kind == k || (k == library::WHD && it.kind == library::WHDHDF); };
        if (ImGui::BeginTabBar("##bib")) {
            for (auto &[k, n] : tabs) {
                int count = (int)std::count_if(lib.begin(), lib.end(), [&](const LibItem &it) { return inTab(it, k); });
                std::string label = std::string(n) + (count ? " (" + std::to_string(count) + ")" : "") + "###" + k;
                if (ImGui::BeginTabItem(label.c_str(), nullptr, wantTab == k ? ImGuiTabItemFlags_SetSelected : 0)) {
                    // Onglet demandé (fichiers ajoutés) : pris dès qu'il est affiché ; sinon celui que l'on touche
                    if (wantTab.empty() || wantTab == k) { libKind = k; if (wantTab == k) wantTab.clear(); }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        std::vector<LibItem> shown;
        for (auto &it : lib) if (inTab(it, libKind)) shown.push_back(it);
        if (shown.empty()) {
            ui::note(libKind == library::WHD ? "Aucun jeu. Ajoutez des archives WHDLoad (.lha, .zip) ou des jeux WHDLoad sur .hdf : un clic les lance."
                     : libKind == library::FLOPPY ? "Aucune disquette (ADF, DMS, IPF…)."
                     : libKind == library::CD ? "Aucun CD de CD32 ou de CDTV (.iso, .chd, .cue)." : "Aucun disque dur (.hdf).");
        }
        float tw = 64 * scale, th = 48 * scale;
        int i = 0;
        for (auto &it : shown) {
            ImGui::PushID(i++);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (Thumb *t = thumb(captures::of(it.name()))) ImGui::Image((ImTextureID)(intptr_t)t->tex, ImVec2(tw, th));
            else {
                ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + tw, p0.y + th), IM_COL32(50, 50, 56, 255), 4 * scale);
                ImGui::Dummy(ImVec2(tw, th));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 2 * scale));
            if (it.exists()) ImGui::TextUnformatted(it.title().c_str());
            else ImGui::TextDisabled("%s  (introuvable)", it.title().c_str());
            if (it.kind == library::WHD || it.kind == library::WHDHDF) {
                if (ui::primaryButton("▶ Jouer")) { if (it.kind == library::WHD) playWhd(it); else playWhdHdf(it); }
            } else if (it.kind == library::CD) {
                if (ui::primaryButton("▶ CD32")) playCd(it, "CD32");
                Rom e;
                if (kickstarts::cdtvExt(e)) { ImGui::SameLine(); if (ui::primaryButton("▶ CDTV")) playCd(it, "CDTV"); }
            } else if (it.kind == library::FLOPPY) {
                if (ui::primaryButton("▶ Jouer")) { library::touch(it.path); s.setFloppy(0, it.path); s.applyProfile(); powerOn(); }
                ImGui::SameLine();
                if (ImGui::Button("DF0:")) { library::touch(it.path); s.setFloppy(0, it.path); s.applyProfile(); }
                ImGui::SameLine();
                if (ImGui::Button("DF1:")) { library::touch(it.path); s.setFloppy(1, it.path); }
            } else {
                auto hds = s.hds();
                bool on = std::find(hds.begin(), hds.end(), it.path) != hds.end();
                if (ImGui::Button(on ? "✓ Branché" : "+ Brancher")) {
                    if (on) hds.erase(std::remove(hds.begin(), hds.end(), it.path), hds.end());
                    else if (hds.size() < 8) { library::touch(it.path); hds.push_back(it.path); }
                    s.setList("hds", hds);
                    if (!on) s.applyProfile();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("✕")) { library::remove(it.path); refresh(); }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        if (ImGui::Button("Ajouter des fichiers…"))
            addFiles(platform::openFiles("Jeux WHDLoad (.lha, .zip), disquettes, disques durs (.hdf), CD ou Kickstart", true,
                                         {"Fichiers Amiga|*.lha;*.lzh;*.lzx;*.zip;*.adf;*.adz;*.dms;*.ipf;*.scp;*.hdf;*.hdz;*.iso;*.chd;*.cue;*.rom"}));
        if (working) { ImGui::SameLine(); ImGui::TextDisabled("Analyse des archives…"); }
        ui::note("Glissez aussi vos fichiers sur cette fenêtre. Un jeu WHDLoad se lance d'un clic ; les .zip sont analysés (disque dur ou jeu WHDLoad extraits, archive abîmée signalée) ; un Kickstart est installé comme ROM de la machine.");
    }
    std::string wantTab;
    void setTab(const std::string &k) { libKind = wantTab = k; }

    // Disquette formatée (880 Ko) pour les sauvegardes des jeux : rangée dans la Bibliothèque et mise dans DF1:
    void blankDisk() {
        std::string f = platform::saveFile("Disquette vierge formatée (880 Ko)", "Sauvegardes.adf", "adf");
        if (f.empty()) return;
        std::string label = paths::name(f);
        label = label.substr(0, label.rfind('.'));
        if (!blankdisk::write(f, label)) { alert("Disquette vierge", "Création impossible"); return; }
        library::add(f); s.setFloppy(1, f);
        setTab(library::FLOPPY);
        refresh();
    }

    // ---------- assistant de premier lancement : ce que l'on veut faire, les Kickstart, les fichiers, puis le geste ----------
    int wizStep = 0;     // 0 : fermé ; 1 : but ; 2 : Kickstart ; 3 : fichiers ; 4 : c'est prêt
    int wizGoal = 0;
    static constexpr const char *goals[] = {"Des jeux WHDLoad (.lha, .zip), lancés d'un clic", "Des disquettes de jeux ou de démos (.adf, .dms, .ipf)",
                                            "AGS ou un Workbench installé (disques .hdf)", "Des jeux sur CD (CD32 ou CDTV)", "Découvrir l'Amiga sans rien installer (AROS)"};
    enum { G_WHD, G_FLOPPY, G_AGS, G_CD, G_AROS };
    void wizStart() { wizStep = 1; wizGoal = 0; }
    bool wizHave() {
        if (wizGoal == G_CD) return kickstarts::cd32Rom() || cdtvReady();
        if (wizGoal == G_FLOPPY) { for (auto &r : roms) if (!r.cdtvExt() && !r.models().count("CD32")) return true; return false; }
        return kickstarts::hasForWhd();
    }
    // Étape 3 : la machine qui convient
    void wizMachine() {
        if (!roms.empty()) s.set("rom", "user");
        Rom k;
        switch (wizGoal) {
        case G_WHD: s.setModel("A1200"); s.setFastCpu(true); s.setRtg(true); setTab(library::WHD); break;
        case G_AGS: s.setModel("A1200"); s.setFastCpu(true); s.setRtg(true); s.setBool("shared", true); setTab(library::HDF); break;
        case G_FLOPPY: s.setModel(kickstarts::best("A500", k) || roms.empty() ? "A500" : "A1200"); s.setFastCpu(false); s.setRtg(false); stockMemory(); setTab(library::FLOPPY); break;
        default: s.setModel(!kickstarts::cd32Rom() && cdtvReady() ? "CDTV" : "CD32"); setTab(library::CD); break;
        }
    }
    void wizard() {
        if (!wizStep) return;
        if (!ImGui::IsPopupOpen("##assistant")) ImGui::OpenPopup("##assistant");
        ImGui::SetNextWindowSize(ImVec2(560 * scale, 0));
        if (!ImGui::BeginPopupModal("##assistant", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
        auto title = [&](const char *t) { ImGui::PushFont(ui::bold); ImGui::TextUnformatted(t); ImGui::PopFont(); ImGui::Spacing(); };
        auto close = [&] { wizStep = 0; ImGui::CloseCurrentPopup(); };
        ImGui::PushTextWrapPos(530 * scale);
        if (wizStep == 1) {
            title("Bienvenue ! Que voulez-vous faire ?");
            for (int i = 0; i < 5; i++) ImGui::RadioButton(goals[i], &wizGoal, i);
            ui::note("L'assistant règle la machine et vous guide pas à pas. Il reste disponible avec « ★ Assistant », en haut de la fenêtre.");
            if (ui::primaryButton("Continuer")) {
                if (wizGoal == G_AROS) { s.set("rom", "aros"); s.setModel("A1200"); s.setFastCpu(true); s.setRtg(true); wizStep = 4; }
                else wizStep = 2;
            }
            ImGui::SameLine();
            if (ImGui::Button("Plus tard")) close();
        } else if (wizStep == 2) {
            bool have = wizHave();
            title(have ? "Kickstart : ✓ déjà installés" : "Vos Kickstart");
            static const char *need[] = {
                "WHDLoad a besoin de votre Kickstart 3.1 de l'A1200 (ou 3.0, 3.1.4, 3.2) : avec la ROM libre AROS, il ne démarre pas.",
                "Les jeux sur disquette veulent le Kickstart de leur machine : 1.3 pour l'Amiga 500 (la plupart), 3.1 pour les jeux A1200. Choisissez-en plusieurs : chacun servira à la machine qui lui convient.",
                "AGS et le Workbench ont besoin de votre Kickstart 3.1 de l'A1200 (ou plus récent).",
                "La CD32 démarre sur sa ROM (kick40060.CD32 et son extension, ou la ROM complète de 1 Mo) ; le CDTV sur un Kickstart 1.3 et son extension."};
            if (have) ImGui::TextUnformatted("Vous pouvez en ajouter d'autres.");
            ImGui::TextUnformatted(need[wizGoal]);
            ui::note("Les Kickstart sont les ROM de l'Amiga : ils ne sont pas fournis. On les trouve dans Amiga Forever (Cloanto) ou on les copie de son propre Amiga.");
            if (ui::primaryButton(have ? "Continuer" : "Choisir mes Kickstart…")) {
                if (!have) for (auto &p : platform::openFiles("Kickstart (fichiers ROM)", true, {"ROM|*.rom;*.bin;*.a500;*.a1200;*.a4000;*.cd32"})) installRom(p);
                wizMachine(); wizStep = 3;
            }
            ImGui::SameLine();
            if (ImGui::Button(have ? "En ajouter…" : "Je n'en ai pas")) {
                if (have) for (auto &p : platform::openFiles("Kickstart (fichiers ROM)", true, {"ROM|*.rom;*.bin;*.a500;*.a1200;*.a4000;*.cd32"})) installRom(p);
                wizMachine(); wizStep = 3;
            }
            ImGui::SameLine();
            if (ImGui::Button("Annuler")) close();
        } else if (wizStep == 3) {
            title("Vos fichiers");
            static const char *what[] = {
                "Choisissez vos jeux WHDLoad (.lha, .zip), plusieurs d'un coup si vous voulez. Ils restent où ils sont : l'application les lit sur place.",
                "Choisissez vos disquettes, plusieurs d'un coup si vous voulez. La première ira dans le lecteur DF0:.",
                "Choisissez d'un coup tous les fichiers .hdf d'AGS (ou votre disque Workbench). Ils sont lus sur place, même s'ils font plusieurs Go.",
                "Choisissez vos CD (.iso, .chd ou .cue)."};
            ImGui::TextUnformatted(what[wizGoal]);
            ui::note("Vous pourrez aussi glisser des fichiers sur la fenêtre.");
            if (ui::primaryButton("Choisir…")) {
                if (wizGoal == G_AGS) addHds();
                else addFiles(platform::openFiles(what[wizGoal], true, {"Fichiers Amiga|*.lha;*.lzh;*.lzx;*.zip;*.adf;*.adz;*.dms;*.ipf;*.scp;*.hdf;*.iso;*.chd;*.cue"}));
                wizStep = 4;
            }
            ImGui::SameLine();
            if (ImGui::Button("Plus tard")) wizStep = 4;
        } else {
            // Étape 4 : ce qu'il reste à faire
            if (wizGoal == G_FLOPPY && s.floppy(0).empty()) for (auto &it : lib) if (it.kind == library::FLOPPY) { s.setFloppy(0, it.path); break; }
            if (wizGoal == G_CD && s.cd().empty()) for (auto &it : lib) if (it.kind == library::CD) { s.set("cd", it.path); break; }
            title("C'est prêt");
            std::string msg;
            bool canPower = true;
            switch (wizGoal) {
            case G_WHD: {
                bool any = std::any_of(lib.begin(), lib.end(), [](const LibItem &i) { return i.kind == library::WHD || i.kind == library::WHDHDF; });
                msg = any ? "Cliquez sur « ▶ Jouer » à côté d'un jeu, dans la Bibliothèque (onglet Jeux) : il démarre directement."
                          : "Ajoutez vos jeux .lha ou .zip avec « Ajouter des fichiers… » (Bibliothèque), puis cliquez sur « ▶ Jouer ».";
                canPower = false;
                break;
            }
            case G_FLOPPY: msg = !s.floppy(0).empty() ? paths::name(s.floppy(0)) + " est dans DF0:. Allumez ! Pour changer de disquette, cliquez sur « DF0: » à côté d'une autre dans la Bibliothèque."
                                                      : "Ajoutez vos disquettes à la Bibliothèque, cliquez sur « DF0: » à côté de l'une d'elles, puis allumez.";
                canPower = !s.floppy(0).empty(); break;
            case G_AGS: msg = !s.hds().empty() ? "Les disques sont branchés : allumez, AGS démarre." : "Ajoutez les disques .hdf avec « Ajouter des disques durs… », puis allumez.";
                canPower = !s.hds().empty(); break;
            case G_CD: msg = !s.cd().empty() ? paths::name(s.cd()) + " est prêt : allumez, ou cliquez sur « ▶ CD32 » / « ▶ CDTV » à côté d'un autre CD de la Bibliothèque."
                                             : "Ajoutez vos CD à la Bibliothèque, puis cliquez sur « ▶ CD32 » ou « ▶ CDTV ».";
                canPower = !s.cd().empty(); break;
            default: msg = "AROS, la ROM libre, démarre un Workbench à découvrir. Pour les jeux, ajoutez plus tard vos Kickstart (carte Kickstart).";
            }
            ImGui::TextUnformatted(msg.c_str());
            ui::note("Pendant le jeu : cliquez dans l'image pour capturer la souris, F12 pour la libérer et voir le menu (disquettes, sauvegardes d'état, image, touches de la manette, sortie d'un jeu WHDLoad).");
            if (canPower) { if (ui::primaryButton("▶ Allumer")) { close(); powerOn(); } ImGui::SameLine(); }
            if (ImGui::Button("Aide détaillée")) { close(); showHelp = true; }
            ImGui::SameLine();
            if (ImGui::Button("Fermer")) close();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }

    void help() {
        if (!showHelp) return;
        if (!ImGui::IsPopupOpen("##aide")) ImGui::OpenPopup("##aide");
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowSize(ImVec2(std::min(640 * scale, io.DisplaySize.x - 40), io.DisplaySize.y * 0.85f));
        if (!ImGui::BeginPopupModal("##aide", nullptr, ImGuiWindowFlags_NoTitleBar)) return;
        ImGui::PushFont(ui::bold); ImGui::TextUnformatted("Bien démarrer"); ImGui::PopFont();
        ImGui::BeginChild("##texte", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8 * scale));
        ImGui::TextWrapped("%s",
            "1. Kickstart : ajoutez vos fichiers ROM, plusieurs si vous voulez (3.1 pour l'A1200, 1.3 pour l'Amiga 500) : chacun sert à la machine qui lui convient. Sans eux, la ROM libre AROS démarre, mais WHDLoad, AGS et la plupart des jeux demandent un vrai Kickstart.\n"
            "\n"
            "2. Jeux et disques : ajoutez vos jeux WHDLoad (.lha, .zip), disquettes, disques durs (.hdf) et CD à la Bibliothèque, ou glissez-les sur la fenêtre. Ils sont lus sur place, sans copie. Un jeu WHDLoad se lance d'un clic (▶ Jouer). Pour AGS, ajoutez tous ses .hdf d'un coup.\n"
            "\n"
            "3. Préréglage : « AGS / Workbench » pour AGS et le Workbench, « Amiga 500 » pour les vieux jeux, « CD32 » pour la console. Carte Machine : A600, A3000, A4000 et CDTV aussi, mémoire et carte accélératrice.\n"
            "\n"
            "4. Allumez.\n"
            "\n"
            "Pendant le jeu :\n"
            "\n"
            "• souris : un clic dans l'image la capture, F12 la libère et affiche le menu\n"
            "\n"
            "• clavier : Windows ou Page préc./suiv. = touches Amiga, Inser = Help, Début/Fin = parenthèses du pavé\n"
            "\n"
            "• manettes : la première joue sur le port joystick, la deuxième sur le port souris ; menu Amiga › Touches de la manette pour changer des boutons en touches du clavier (pour ce jeu)\n"
            "\n"
            "• Ctrl+P pause · Ctrl+S / Ctrl+L sauvegarde et reprise rapides · Ctrl+E les 4 emplacements avec leur image\n"
            "\n"
            "• Ctrl+O disquette DF0: · Ctrl+N disquette vierge (pour les sauvegardes des jeux)\n"
            "\n"
            "• Ctrl+Q quitter un jeu WHDLoad (F10 ; * ou Échap dans Touche de sortie)\n"
            "\n"
            "• Ctrl+I filtre de l'image · Ctrl+J image et cadrage de ce jeu (zoom, position) · F11 plein écran · Ctrl+K capture · Ctrl+T turbo · Ctrl+R réinitialiser\n"
            "\n"
            "• fermer la fenêtre garde l'état : « Reprendre » le retrouve à l'accueil\n"
            "\n"
            "Ces raccourcis Ctrl marchent souris libre (après F12) ; souris capturée, toutes les touches vont à l'Amiga.\n"
            "\n"
            "Si l'émulation s'arrête brutalement, l'accueil prépare un rapport (fin du journal) à copier ou à signaler sur GitHub.\n"
            "\n"
            "À l'accueil aussi : DF2: et DF3:, Image (filtre, pixels réguliers), MIDI (synthétiseur ou clavier), configurations enregistrées et export .uae, et « Changer de PC » : tout exporter dans un fichier .zip, puis l'importer sur un autre PC.);");
        ImGui::EndChild();
        if (ui::primaryButton("Compris")) { showHelp = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
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
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8 * scale);
        float y = ImGui::GetCursorPosY();
        if (ImGui::Button("★ Assistant")) wizStart();
        ImGui::SameLine();
        ImGui::SetCursorPosY(y);
        if (ImGui::Button("? Aide")) showHelp = true;
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

        if (card("bib", "Bibliothèque")) drawLibrary();
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
            for (int d = 0; d < 4; d++) {
                // DF2: et DF3: : affichés quand ils servent, ou à la demande
                if (d >= 2 && s.floppy(2).empty() && s.floppy(3).empty() && !moreDrives) break;
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
            if (!moreDrives && s.floppy(2).empty() && s.floppy(3).empty()) { if (ImGui::SmallButton("+ DF2: et DF3:")) moreDrives = true; ImGui::SameLine(); }
            if (ImGui::SmallButton("Disquette vierge…")) blankDisk();
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
            ui::note("Pendant la partie, menu Image › Image et cadrage de ce jeu (Ctrl+J) : zoom et position verticale, retenus pour chaque jeu.");
        }
        endCard();

        if (card("midi", "MIDI")) {
            if (check("MIDI sur le port série de l'Amiga", s.midi(), [&](bool v) { s.setBool("midi", v); })) midiNames.clear();
            if (s.midi()) {
                if (midiNames.empty()) { midiNames = {midi::names(true), midi::names(false)}; }
                auto pick = [&](const char *label, const std::vector<std::string> &names, const std::string &key, const char *none) {
                    std::string cur = s.get(key);
                    ImGui::SetNextItemWidth(320 * scale);
                    if (ImGui::BeginCombo(label, cur.empty() ? none : cur.c_str())) {
                        if (ImGui::Selectable(none, cur.empty())) s.set(key, "");
                        for (auto &n : names) if (ImGui::Selectable(n.c_str(), n == cur)) s.set(key, n);
                        ImGui::EndCombo();
                    }
                };
                pick("Sortie", midiNames[0], "midiOut", "Premier appareil trouvé");
                pick("Entrée", midiNames[1], "midiIn", "Aucune");
                if (ImGui::SmallButton("Actualiser la liste")) midiNames.clear();
            }
#ifdef _WIN32
            ui::note("L'Amiga joue sur un synthétiseur USB ou sur le synthé de Windows (Microsoft GS Wavetable Synth) ; un clavier MIDI peut jouer sur l'Amiga (Bars & Pipes, Music-X, OctaMED…).");
#else
            ui::note("L'Amiga joue sur un synthétiseur USB ou un synthé logiciel (FluidSynth, TiMidity, Qsynth…) par le séquenceur ALSA ; un clavier MIDI peut jouer sur l'Amiga (Bars & Pipes, Music-X, OctaMED…).");
#endif
        }
        endCard();

        if (card("cfg", "Configurations")) {
            auto names = configs::names();
            if (names.empty()) ui::note("Aucune configuration enregistrée.");
            int k = 0;
            for (auto &n : names) {
                ImGui::PushID(k++);
                ImGui::TextUnformatted(n.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Charger")) { configs::restore(s, n); refresh(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("✕")) configs::remove(s, n);
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(220 * scale);
            ImGui::InputTextWithHint("##nom", "Nom de la configuration", cfgBuf, sizeof cfgBuf);
            ImGui::SameLine();
            if (ImGui::Button("Enregistrer") && cfgBuf[0]) { configs::save(s, cfgBuf); cfgBuf[0] = 0; }
            if (ImGui::Button("Exporter .uae…")) {
                std::string n = s.bootKey().empty() ? "amiga" : s.bootKey().substr(0, s.bootKey().rfind('.'));
                std::string f = platform::saveFile("Configuration WinUAE (.uae)", n + ".uae", "uae");
                if (!f.empty()) alert("Export", configs::exportUae(s, f) ? "Configuration exportée : " + paths::name(f) : "Export impossible");
            }
            ImGui::SameLine();
            if (ImGui::Button("Importer .uae…")) {
                auto f = platform::openFiles("Configuration WinUAE ou Amiberry (.uae)", false, {"Configurations|*.uae"});
                if (!f.empty()) { alert("Import", configs::importUae(s, f[0])); refresh(); }
            }
            ui::note("Une configuration garde la machine, la mémoire et ses disques. L'export .uae se relit dans WinUAE, Amiberry et les autres versions. La machine choisie est aussi retenue pour chaque disque de démarrage.");
        }
        endCard();

        if (card("pc", "Changer de PC")) {
            if (ImGui::Button("Tout exporter…")) {
                char d[16]; time_t t = time(nullptr); strftime(d, sizeof d, "%Y-%m-%d", localtime(&t));
                std::string f = platform::saveFile("Sauvegarde d'Amiga 1200", std::string("amiga1200-sauvegarde-") + d + ".zip", "zip");
                if (!f.empty()) { working++; std::thread([this, f] { std::string m = backup::exportTo(f); later([this, m] { working--; alert("Changer de PC", m); }); }).detach(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Importer…")) {
                auto f = platform::openFiles("Sauvegarde d'Amiga 1200 (.zip)", false, {"Sauvegardes|*.zip"});
                if (!f.empty()) { importFile = f[0]; ImGui::OpenPopup("##import"); }
            }
            if (ImGui::BeginPopupModal("##import", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                ImGui::PushFont(ui::bold); ImGui::TextUnformatted("Importer la sauvegarde ?"); ImGui::PopFont();
                ImGui::PushTextWrapPos(480 * scale);
                ImGui::TextUnformatted("Les réglages, la Bibliothèque et les configurations sont remplacés par ceux de la sauvegarde ; ses Kickstart, sauvegardes d'état et captures s'ajoutent. Éteignez l'Amiga avant.");
                ImGui::PopTextWrapPos();
                if (ui::primaryButton("Importer")) {
                    std::string f = importFile;
                    working++;
                    std::thread([this, f] { std::string m = backup::importFrom(f); later([this, m] { working--; refresh(); alert("Changer de PC", m); }); }).detach();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (working) { ImGui::SameLine(); ImGui::TextDisabled("En cours…"); }
            ui::note("Un seul fichier .zip : réglages, Bibliothèque, configurations, Kickstart, sauvegardes d'état, sauvegardes des jeux (WHDLoad, SHARED:), disques extraits des archives et captures. Les jeux rangés ailleurs n'y sont pas : sur l'autre PC, ajoutez-les à la Bibliothèque. Ce fichier contient vos Kickstart : ne le partagez pas.");
        }
        endCard();

        ui::note("Pendant le jeu : cliquez dans l'image pour capturer la souris, F12 pour la libérer et voir le menu (disquettes, sauvegarde rapide Ctrl+S / Ctrl+L, image, plein écran F11, réinitialisation). Touches Amiga : Windows ou Page préc./suiv. ; Help : Inser.");
        ImGui::End();

        wizard();
        help();
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
        // Premier lancement : l'assistant
        if (!s.getBool("aideVue", false)) { s.setBool("aideVue", true); wizStart(); }
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
                    if (e.type == SDL_DROPFILE) { dropped.push_back(e.drop.file); SDL_free(e.drop.file); }
                } while (SDL_PollEvent(&e));
            }
            // Fin de l'émulation : réglages relus (reprise), plantage éventuel
            if (emuPid && SDL_GetTicks64() / 1000.0 > nextCheck) {
                nextCheck = SDL_GetTicks64() / 1000.0 + 1;
                if (!platform::processAlive(emuPid)) { emuPid = 0; refresh(); crashCheck(crashReport); }
            }
            if (!dropped.empty()) { addFiles(dropped); dropped.clear(); }
            {
                std::vector<std::function<void()>> todo;
                { std::lock_guard<std::mutex> l(mtx); todo.swap(pending); }
                for (auto &f : todo) f();
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
