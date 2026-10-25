#include "kickstarts.h"
#include "store.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <tuple>
#include <cstdio>

namespace fs = std::filesystem;

namespace {
struct Known { const char *label; std::set<std::string> models; bool cdtvExt; };
// ROM de Commodore et d'Hyperion (somme CRC32 du fichier), d'après la table d'Amiberry comme la version web.
// « A500 » couvre aussi l'A1000, l'A2000, l'A500+ et le CDTV ; « A4000 » l'A4000T.
const std::map<uint32_t, Known> &known() {
    static const std::map<uint32_t, Known> k = {
        {0xA6CE1636, {"Kickstart 1.2 (A500)", {"A500"}, false}},
        {0xC4F0F55F, {"Kickstart 1.3 (A500)", {"A500"}, false}},
        {0xE0F37258, {"Kickstart 1.3 (A3000)", {"A3000"}, false}},
        {0xBC0EC13F, {"Kickstart 1.4 (A3000)", {"A3000"}, false}},
        {0xC3BDB240, {"Kickstart 2.04 (A500+)", {"A500"}, false}},
        {0x234A7233, {"Kickstart 2.04 (A3000)", {"A3000"}, false}},
        {0x83028FB5, {"Kickstart 2.05 (A600)", {"A600"}, false}},
        {0x64466C2A, {"Kickstart 2.05 (A600HD)", {"A600"}, false}},
        {0x43B0DF7B, {"Kickstart 2.05 (A600HD)", {"A600"}, false}},
        {0x6C9B07D2, {"Kickstart 3.0 (A1200)", {"A1200"}, false}},
        {0x9E6AC152, {"Kickstart 3.0 (A4000)", {"A4000"}, false}},
        {0xFC24AE0D, {"Kickstart 3.1 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0x1483A091, {"Kickstart 3.1 (A1200)", {"A1200"}, false}},
        {0xEFB239CC, {"Kickstart 3.1 (A3000)", {"A3000"}, false}},
        {0xD6BAE334, {"Kickstart 3.1 (A4000)", {"A4000"}, false}},
        {0x2B4566F1, {"Kickstart 3.1 (A4000)", {"A4000"}, false}},
        {0x43B6DD22, {"Kickstart 3.1 (A4000)", {"A4000"}, false}},
        {0x75932C3A, {"Kickstart 3.1 (A4000T)", {"A4000"}, false}},
        {0x3AC99EDC, {"Kickstart 3.X (A4000)", {"A4000"}, false}},
        {0xD52B52FD, {"Kickstart 3.1.4 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0xF17FA97F, {"Kickstart 3.1.4 (A1200)", {"A1200"}, false}},
        {0x50C3529C, {"Kickstart 3.1.4 (A3000)", {"A3000"}, false}},
        {0xD47E18FD, {"Kickstart 3.1.4 (A4000)", {"A4000"}, false}},
        {0x75A2B2A5, {"Kickstart 3.1.4 (A4000T)", {"A4000"}, false}},
        {0x568F8786, {"Kickstart 3.1.4.2 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0xB87506A7, {"Kickstart 3.1.4.2 (A1200)", {"A1200"}, false}},
        {0xBA35F8EB, {"Kickstart 3.1.4.2 (A3000)", {"A3000"}, false}},
        {0x1B84CB33, {"Kickstart 3.1.4.2 (A4000)", {"A4000"}, false}},
        {0xD6D0EF3E, {"Kickstart 3.1.4.2 (A4000T)", {"A4000"}, false}},
        {0x8173D7B6, {"Kickstart 3.2 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0xBD1FF75E, {"Kickstart 3.2 (A1200)", {"A1200"}, false}},
        {0xF3AF46CC, {"Kickstart 3.2 (A3000)", {"A3000"}, false}},
        {0x9BB8FC93, {"Kickstart 3.2 (A4000)", {"A4000"}, false}},
        {0x9188A509, {"Kickstart 3.2 (A4000T)", {"A4000"}, false}},
        {0x4F078456, {"Kickstart 3.2.1 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0x2B653371, {"Kickstart 3.2.1 (A1200)", {"A1200"}, false}},
        {0x0078F607, {"Kickstart 3.2.1 (A3000)", {"A3000"}, false}},
        {0xF3CED3B8, {"Kickstart 3.2.1 (A4000)", {"A4000"}, false}},
        {0xAF3452EC, {"Kickstart 3.2.1 (A4000T)", {"A4000"}, false}},
        {0xE4458462, {"Kickstart 3.2.2 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0x5C40328A, {"Kickstart 3.2.2 (A1200)", {"A1200"}, false}},
        {0x46335B57, {"Kickstart 3.2.2 (A3000)", {"A3000"}, false}},
        {0x4BEA9798, {"Kickstart 3.2.2 (A4000)", {"A4000"}, false}},
        {0x36BBCD8A, {"Kickstart 3.2.2 (A4000T)", {"A4000"}, false}},
        {0xE1F50B0B, {"Kickstart 3.2.3 (A500, A600, A2000)", {"A500", "A600"}, false}},
        {0xB18D3B67, {"Kickstart 3.2.3 (A1200)", {"A1200"}, false}},
        {0x74C0B23F, {"Kickstart 3.2.3 (A3000)", {"A3000"}, false}},
        {0xB6A4698E, {"Kickstart 3.2.3 (A4000)", {"A4000"}, false}},
        {0x588A5E6D, {"Kickstart 3.2.3 (A4000T)", {"A4000"}, false}},
        // Extension du CDTV : s'ajoute au Kickstart 1.3 de la console
        {0x42BAA124, {"Extension du CDTV 1.0", {"CDTV"}, true}},
        {0xCEAE68D2, {"Extension du CDTV 2.7", {"CDTV"}, true}},
        {0x30B54232, {"Extension du CDTV 2.30", {"CDTV"}, true}},
        {0xAB6274E7, {"Extension du CDTV 47.1", {"CDTV"}, true}},
        // ROM de la CD32 : rangées à part (sys/cd32.rom et cd32ext.rom), jamais prises pour un autre Amiga
        {0x1E62D4A5, {"ROM de la CD32", {"CD32"}, false}},
        {0x87746BE2, {"Extension de la CD32", {"CD32"}, false}},
    };
    return k;
}

std::vector<uint8_t> readAll(const std::string &path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool info(const std::string &path, Rom &r) {
    auto b = readAll(path);
    if (b.empty()) return false;
    r.path = path;
    r.crc = kickstarts::crc32(b);
    r.version = r.revision = 0;
    // En-tête d'une ROM de 256 ou 512 Ko (11 11, 11 14 ou 11 16) : version et révision aux octets 12 à 15
    if (b.size() >= 16 && b[0] == 0x11 && (b[1] == 0x11 || b[1] == 0x14 || b[1] == 0x16)) {
        r.version = b[12] << 8 | b[13]; r.revision = b[14] << 8 | b[15];
        if (r.version < 30 || r.version > 60) r.version = r.revision = 0;
    }
    return true;
}

// ROM faites pour cette machine, par ordre de préférence (l'A3000 et l'A4000 se prêtent leurs ROM ; le CDTV démarre
// sur le Kickstart de l'Amiga 500)
std::vector<std::string> groups(const std::string &model) {
    if (model == "A3000") return {"A3000", "A4000"};
    if (model == "A4000") return {"A4000", "A3000"};
    if (model == "CDTV") return {"A500"};
    return {model};
}

// 0 : ne convient pas ; plus c'est grand, mieux elle convient
int rank(const Rom &r, const std::string &model) {
    if (r.cdtvExt()) return 0;
    auto ms = r.models();
    if (!ms.empty()) {
        auto g = groups(model);
        for (size_t i = 0; i < g.size(); i++) if (ms.count(g[i])) return (int)(g.size() - i + 1);
        return 0;
    }
    // ROM inconnue : d'après sa version. 1.x : Amiga 500 ; 2.0 : A500 et A600 ; 3.x : toutes
    int v = r.version;
    bool ok = false;
    if (model == "A500" || model == "CDTV") ok = (v >= 30 && v <= 37) || v >= 39;
    else if (model == "A600") ok = v == 37 || v >= 39;
    else if (Settings::bigBox(model)) ok = v >= 39;
    return ok ? 1 : 0;
}
}  // namespace

std::string Rom::label() const {
    auto it = known().find(crc);
    if (it != known().end()) return it->second.label;
    char b[96];
    if (version > 0) { snprintf(b, sizeof b, "Kickstart %s (%d.%d)", kickstarts::family(version).c_str(), version, revision); return b; }
    std::error_code ec;
    snprintf(b, sizeof b, "Kickstart (%d Ko)", (int)(fs::file_size(fs::u8path(path), ec) / 1024));
    return b;
}
std::set<std::string> Rom::models() const {
    auto it = known().find(crc);
    return it == known().end() ? std::set<std::string>{} : it->second.models;
}
bool Rom::cdtvExt() const {
    auto it = known().find(crc);
    return it != known().end() && it->second.cdtvExt;
}

namespace kickstarts {
std::string family(int v) {
    if (v == 34) return "1.3";
    if (v == 33) return "1.2";
    if (v < 33) return "1.x";
    if (v >= 35 && v <= 37) return "2.0";
    if (v == 39) return "3.0";
    if (v == 40) return "3.1";
    if (v >= 45) return "3.x";
    return std::to_string(v);
}

uint32_t crc32(const std::vector<uint8_t> &d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFF;
    for (uint8_t x : d) c = table[(c ^ x) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

std::vector<Rom> list() {
    std::vector<Rom> r;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(fs::u8path(paths::sys()), ec)) {
        std::string n = e.path().filename().u8string();
        if (n.rfind("kick-", 0) == 0 && paths::ext(n) == "rom") {
            Rom k;
            if (info(e.path().u8string(), k)) r.push_back(k);
        }
    }
    std::sort(r.begin(), r.end(), [](const Rom &a, const Rom &b) { return a.version < b.version; });
    return r;
}

bool install(const std::string &path, std::string &why, Rom *out) {
    auto d = readAll(path);
    if (d.empty()) { why = "fichier illisible"; return false; }
    if (d.size() >= 11 && std::string(d.begin(), d.begin() + 11) == "AMIROMTYPE1") {
        why = "ROM chiffrée d'Amiga Forever : copiez la ROM déchiffrée"; return false;
    }
    if (d.size() != 262144 && d.size() != 524288 && d.size() != 1048576) {
        why = "ce fichier n'a pas la taille d'un Kickstart (256 Ko, 512 Ko ou 1 Mo)"; return false;
    }
    return installData(d, out);
}

bool knownRom(uint32_t crc, std::string &label, std::set<std::string> &models, bool &cdtv) {
    auto it = known().find(crc);
    if (it == known().end()) return false;
    label = it->second.label; models = it->second.models; cdtv = it->second.cdtvExt;
    return true;
}

bool installData(const std::vector<uint8_t> &d, Rom *out) {
    if (d.size() != 262144 && d.size() != 524288 && d.size() != 1048576) return false;
    uint32_t crc = crc32(d);
    std::string f;
    if (crc == 0x1E62D4A5 || crc == 0x87746BE2) f = paths::sys() + (crc == 0x87746BE2 ? "cd32ext.rom" : "cd32.rom");
    else { char n[32]; snprintf(n, sizeof n, "kick-%08x.rom", crc); f = paths::sys() + n; }
    if (!paths::exists(f) || crc == 0x1E62D4A5 || crc == 0x87746BE2) {
        std::ofstream o(fs::u8path(f), std::ios::binary);
        o.write((const char *)d.data(), (std::streamsize)d.size());
    }
    Rom r;
    info(f, r);
    if (out) *out = r;
    return true;
}

void remove(const Rom &r) { std::error_code ec; fs::remove(fs::u8path(r.path), ec); }
bool fits(const Rom &r, const std::string &model) { return rank(r, model) > 0; }

// Le meilleur Kickstart pour la machine (celui qui lui est destiné, sinon le plus récent qui convient ; le 1.3 d'abord
// pour le CDTV, celui de la vraie console)
bool best(const std::string &model, Rom &out) {
    bool found = false;
    std::tuple<int, int, int, int> bk{};
    for (auto &r : list()) {
        if (!fits(r, model)) continue;
        auto k = std::make_tuple(rank(r, model), model == "CDTV" && r.version == 34 ? 1 : 0, r.version, r.revision);
        if (!found || k > bk) { bk = k; out = r; found = true; }
    }
    return found;
}
bool cdtvExt(Rom &out) {
    for (auto &r : list()) if (r.cdtvExt()) { out = r; return true; }
    return false;
}
bool hasForWhd() {
    for (auto &r : list()) {
        auto m = r.models();
        if (r.cdtvExt() || m.count("CD32")) continue;
        if (r.version >= 39 || m.count("A1200") || m.count("A3000") || m.count("A4000")) return true;
    }
    return false;
}
bool cd32Rom() { return paths::exists(paths::sys() + "cd32.rom"); }

std::string whdName(uint32_t crc) {
    switch (crc) {
    case 0xA6CE1636: return "kick33180.A500";
    case 0xC4F0F55F: return "kick34005.A500";
    case 0xC3BDB240: return "kick37175.A500";
    case 0xFC24AE0D: return "kick40063.A600";
    case 0x6C9B07D2: return "kick39106.A1200";
    case 0x1483A091: return "kick40068.A1200";
    case 0xD6BAE334: return "kick40068.A4000";
    default: return "";
    }
}
}  // namespace kickstarts
