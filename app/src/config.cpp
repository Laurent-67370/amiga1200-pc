#include "config.h"
#include "kickstarts.h"
#include "bridge.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace config {

bool kick(Settings &s, const std::string &model, Rom &out) { return s.rom() == "user" && kickstarts::best(model, out); }

std::string content(Settings &s) {
    if (Settings::isWhd(s.floppy(0))) return s.floppy(0);
    return write(s);
}

// Disque dur avec table de partitions (RDB, comme AGS) ; sinon l'image d'une seule partition, qui demande sa géométrie
bool hasRdb(const std::string &path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return true;
    std::vector<char> b(16 * 512);
    f.read(b.data(), (std::streamsize)b.size());
    size_t n = (size_t)f.gcount() / 512;
    for (size_t k = 0; k < n; k++) if (std::string(b.data() + k * 512, 4) == "RDSK") return true;
    return false;
}

// Dossier WHDCommon/Kickstarts : vos Kickstart sous les noms qu'attend WHDLoad et les tables de relogement (.RTB)
static std::string whdCommon() {
    std::vector<Rom> roms;
    for (auto &r : kickstarts::list()) if (!kickstarts::whdName(r.crc).empty()) roms.push_back(r);
    if (roms.empty()) return "";
    std::error_code ec;
    fs::path root = fs::u8path(paths::dir("whdcommon")), dir = root / "Kickstarts";
    fs::create_directories(dir, ec);
    for (auto &r : roms) {
        fs::path f = dir / kickstarts::whdName(r.crc);
        if (!fs::exists(f, ec)) fs::copy_file(fs::u8path(r.path), f, ec);
    }
    fs::path rtb = fs::u8path(paths::save()) / "WHDBoot" / "save-data" / "Kickstarts";
    for (auto &e : fs::directory_iterator(rtb, ec)) {
        if (paths::ext(e.path().u8string()) != "rtb") continue;
        fs::path f = dir / e.path().filename();
        if (!fs::exists(f, ec)) fs::copy_file(e.path(), f, ec);
    }
    return root.u8string();
}

static void log(const std::string &m) { amb_log(m.c_str()); }

std::string write(Settings &s, bool arosOnly, const std::string &whdHdf) {
    std::string sys = paths::sys();
    bool whd = !whdHdf.empty();
    std::string model = whd ? "A1200" : s.model();
    std::vector<std::string> l = {"quickstart=" + model + ",0"};
    std::vector<std::string> floppies(4);
    if (!arosOnly && !whd) for (int i = 0; i < 4; i++) floppies[i] = s.floppy(i);
    std::vector<std::string> hds = arosOnly ? std::vector<std::string>{} : whd ? std::vector<std::string>{whdHdf} : s.hds();
    std::string ntsc = s.ntsc() ? "true" : "false";
    if (model == "CD32") {
        // CD32 : la console telle qu'elle est, avec ses propres ROM (512 Ko + extension, ou ROM complète de 1 Mo)
        l.push_back("kickstart_rom_file=" + sys + "cd32.rom");
        if (paths::exists(sys + "cd32ext.rom")) l.push_back("kickstart_ext_rom_file=" + sys + "cd32ext.rom");
        if (!s.cd().empty()) l.push_back("cdimage0=" + s.cd() + ",image");
        l.push_back("ntsc=" + ntsc); l.push_back("gfx_resolution=hires");
        hds.clear();
    } else if (model == "CDTV") {
        // CDTV : la console (68000, ECS, 1 Mo de Chip, sans lecteur de disquettes), son Kickstart et son extension
        Rom k, e;
        bool hk = kickstarts::best("CDTV", k);
        s.set("romName", hk ? k.label() : "");
        if (hk) l.push_back("kickstart_rom_file=" + k.path);
        if (kickstarts::cdtvExt(e)) l.push_back("kickstart_ext_rom_file=" + e.path);
        if (!s.cd().empty()) l.push_back("cdimage0=" + s.cd() + ",image");
        l.push_back("ntsc=" + ntsc); l.push_back("gfx_resolution=hires");
        hds.clear();
    } else {
        // Le Kickstart qui convient à cette machine parmi ceux installés ; sinon AROS
        Rom k;
        bool hk = arosOnly ? false : whd ? kickstarts::best(model, k) : kick(s, model, k);
        if (!whd) s.set("romName", hk ? k.label() : "");
        if (hk) l.push_back("kickstart_rom_file=" + k.path);
        else {
            l.push_back("kickstart_rom_file=" + sys + "aros-rom.bin");
            l.push_back("kickstart_ext_rom_file=" + sys + "aros-ext.bin");
            // AROS ne tient pas dans la mémoire d'un A500 ou d'un A600
            if (model == "A500") l.push_back("fastmem_size=2");
            if (model == "A600") l.push_back("fastmem_size=4");
        }
        // Disquette avec un Kickstart 1.x (jeux A500) : les disques durs restent débranchés (erreur 103)
        if (!floppies[0].empty() && hk && k.version >= 1 && k.version <= 35 && !hds.empty()) {
            log("Disquette avec le " + k.label() + " : disques durs non branchés");
            hds.clear();
        }
        // Quatre lecteurs si DF2: ou DF3: sont utilisés
        int n = floppies[2].empty() && floppies[3].empty() ? 2 : 4;
        l.push_back("nr_floppies=" + std::to_string(n));
        for (int i = 1; i < n; i++) l.push_back("floppy" + std::to_string(i) + "type=0");
        for (int i = 0; i < n; i++) if (!floppies[i].empty()) l.push_back("floppy" + std::to_string(i) + "=" + floppies[i]);
        if (s.floppySound()) for (int i = 0; i < n; i++) l.push_back("floppy" + std::to_string(i) + "sound=1");
        bool fastCpu = s.fastCpu() && Settings::bigBox(model) && !whd;
        if (whd) l.push_back("fastmem_size=8");
        else if (!arosOnly) {
            // Mémoire et processeur choisis
            if (s.chip() > 0) l.push_back("chipmem_size=" + std::to_string(s.chip()));
            if (s.slow() >= 0) l.push_back("bogomem_size=" + std::to_string(s.slow()));
            // (AROS demande déjà 2 ou 4 Mo à l'A500 et à l'A600 : on garde la plus grande des deux)
            int have = 0;
            for (auto &x : l) if (x.rfind("fastmem_size=", 0) == 0) have = std::max(have, std::stoi(x.substr(13)));
            if (s.fast() > have && !fastCpu) {
                l.erase(std::remove_if(l.begin(), l.end(), [](const std::string &x) { return x.rfind("fastmem_size=", 0) == 0; }), l.end());
                l.push_back("fastmem_size=" + std::to_string(s.fast()));
            }
        }
        std::string accel = !whd && !arosOnly && Settings::bigBox(model) && !s.fastCpu() ? s.accel() : "none";
        if (accel == "030") for (auto x : {"cpu_model=68030", "fpu_model=68882", "cpu_24bit_addressing=false", "cpu_multiplier=8"}) l.push_back(x);
        if (accel == "040") for (auto x : {"cpu_model=68040", "fpu_model=68040", "cpu_24bit_addressing=false", "cpu_multiplier=8"}) l.push_back(x);
        if (fastCpu || (arosOnly && Settings::bigBox(model))) {
            // Processeur rapide : 68040 sans cycles exacts, à toute la vitesse du PC (pas de JIT sur PC x64), et 64 Mo de
            // Fast Zorro III
            for (auto x : {"cpu_model=68040", "fpu_model=68040", "cpu_24bit_addressing=false", "cpu_compatible=false",
                           "cycle_exact=false", "cpu_memory_cycle_exact=false", "cpu_speed=max", "z3mem_size=64"}) l.push_back(x);
        } else {
            l.push_back("cycle_exact=false"); l.push_back("cpu_compatible=false");
        }
        l.push_back("ntsc=" + ntsc);
        l.push_back("gfx_resolution=hires");
        // Carte RTG sur le Zorro III : il lui faut un processeur 32 bits (le 68EC020 de l'A1200 n'adresse que 16 Mo)
        if ((s.rtg() || arosOnly) && Settings::bigBox(model) && !whd) {
            l.push_back("gfxcard_type=ZorroIII"); l.push_back("gfxcard_size=32");
            if (!s.fastCpu() && model == "A1200" && accel == "none") { l.push_back("cpu_model=68020"); l.push_back("cpu_24bit_addressing=false"); }
        }
        // Disques durs sur le pilote d'UAE (uaehf.device) : démarrent AGS et les Workbench préinstallés
        for (size_t i = 0; i < hds.size() && i < 8; i++) {
            bool rdb = hasRdb(hds[i]);
            log(paths::name(hds[i]) + " : " + (rdb ? "table de partitions (RDB)" : "partition seule (32 secteurs, 1 tête, 2 blocs réservés)"));
            l.push_back("hardfile2=rw,DH" + std::to_string(i) + ":" + hds[i] + "," + (rdb ? "0,0,0,512" : "32,1,2,512") + "," +
                        (i == 0 ? "0" : "-1") + ",,uae" + std::to_string(i));
        }
        if (!hds.empty() && s.sharedVol() && !whd) l.push_back("filesystem2=rw,DH12:SHARED:" + paths::shared() + ",-128");
        // Jeu WHDLoad sur .hdf : vos Kickstart dans WHDCommon:Kickstarts (WHDLoad y cherche après Devs:Kickstarts)
        if (whd) { std::string w = whdCommon(); if (!w.empty()) l.push_back("filesystem2=ro,DH11:WHDCommon:" + w + ",-128"); }
        // Sans disque dur, ni ROM de démarrage ni carte d'UAE, comme la version web : toute la mémoire au jeu
        if (hds.empty()) { l.push_back("boot_rom_uae=disabled"); l.push_back("uaeboard=disabled"); }
    }
    // MIDI du port série vers l'application, comme les autres versions
    if (s.midi() && !arosOnly) {
        l.push_back("amiberry.midiout_device_name=Libretro MIDI");
        if (!s.midiIn().empty()) l.push_back("amiberry.midiin_device_name=Libretro MIDI");
    }
    std::string f = arosOnly ? paths::dir("cfg") + "essai.uae" : paths::config();
    std::string all;
    for (auto &x : l) all += x + "\n";
    std::ofstream(fs::u8path(f), std::ios::trunc) << all;
    log("Configuration :\n" + all);
    return f;
}
}  // namespace config
