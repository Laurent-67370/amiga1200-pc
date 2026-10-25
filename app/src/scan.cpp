#include "scan.h"
#include "extras.h"
#include "kickstarts.h"
#include "library.h"
#include "store.h"
#include "zip.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>

namespace fs = std::filesystem;

namespace scan {
const char *KEY = "key", *ZIP = "zip", *UAE = "uae";
static const size_t MAX = 20000;
static const uint32_t CD32_EXT = 0x87746BE2;

const std::vector<std::pair<std::string, std::string>> cats = {
    {library::ROM, "Kickstart et ROM"}, {KEY, "Clé rom.key (Amiga Forever)"}, {library::FLOPPY, "Disquettes"},
    {library::WHD, "Jeux WHDLoad"}, {ZIP, "Archives .zip (disques durs, jeux)"}, {library::HDF, "Disques durs"},
    {library::CD, "CD (CD32, CDTV)"}, {UAE, "Configurations .uae"}};
std::string cat(const std::string &k) { return k == library::WHDHDF ? std::string(library::WHD) : k; }

std::string Item::name() const { return paths::name(path); }

namespace {
std::string lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }

std::vector<uint8_t> readAll(const std::string &path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::string readText(const std::string &path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Archive LhA d'un jeu WHDLoad : le nom d'un .slave figure en clair dans les en-têtes
bool hasSlave(const std::string &path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::vector<char> buf(1 << 20);
    std::string tail;
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        auto n = f.gcount();
        if (n <= 0) break;
        std::string t = tail + lower(std::string(buf.data(), (size_t)n));
        if (t.find(".slave") != std::string::npos) return true;
        tail = t.size() > 16 ? t.substr(t.size() - 16) : t;
    }
    return false;
}

// Contenu d'un .zip : ZIP (disque dur ou jeu .lha, à extraire), WHD (dossier d'un jeu WHDLoad), FLOPPY, ou vide
std::string zipKind(const std::string &path) {
    std::vector<zip::Entry> es;
    if (!zip::list(path, es)) return "";
    bool slave = false, extract = false, floppy = false;
    for (auto &e : es) {
        if (e.dir()) continue;
        std::string n = lower(paths::name(e.name)), x = paths::ext(n);
        if (n.size() > 6 && n.compare(n.size() - 6, 6, ".slave") == 0) slave = true;
        if (x == "hdf" || x == "hdz" || x == "lha" || x == "lzh") extract = true;
        if (x == "adf" || x == "adz" || x == "dms" || x == "ipf") floppy = true;
    }
    return extract ? ZIP : slave ? std::string(library::WHD) : floppy ? std::string(library::FLOPPY) : "";
}

struct RomInfo { bool ok = false, aros = false, plain = true; std::vector<uint8_t> data; std::string label; std::set<std::string> tags; };

// Ce qu'est une ROM (déchiffrée avec key si elle vient d'Amiga Forever) ; ok faux si ce n'en est pas une
RomInfo romInfo(const std::vector<uint8_t> &b, const std::vector<uint8_t> &key) {
    RomInfo r;
    std::vector<uint8_t> p = b;
    if (b.size() >= 11 && std::string(b.begin(), b.begin() + 11) == "AMIROMTYPE1") {
        if (key.empty()) { r.ok = true; r.plain = false; r.label = "ROM chiffrée d'Amiga Forever : il faut son fichier rom.key dans le dossier"; return r; }
        p.assign(b.begin() + 11, b.end());
        for (size_t i = 0; i < p.size(); i++) p[i] ^= key[i % key.size()];
    }
    if (p.size() != 262144 && p.size() != 524288 && p.size() != 1048576) return r;
    // AROS : déjà intégré, il ressemble à un Kickstart 3.1.4 mais n'en est pas un
    if (std::string(p.begin(), p.begin() + 256).find("AROS") != std::string::npos) { r.ok = r.aros = true; r.label = "AROS, déjà intégré à l'application"; return r; }
    uint32_t crc = kickstarts::crc32(p);
    std::string label;
    std::set<std::string> models;
    bool cdtv = false;
    if (kickstarts::knownRom(crc, label, models, cdtv)) {
        r.ok = true; r.label = label; r.data = p;
        if (cdtv) r.tags.insert("cdtvext");
        else if (models.count("CD32")) r.tags.insert(crc == CD32_EXT ? "cd32ext" : "cd32main");
        else {
            if (models.count("A1200")) r.tags.insert("a1200");
            if (label.rfind("Kickstart 1.3", 0) == 0) r.tags.insert("a500");
        }
        return r;
    }
    int v = 0;
    if (p[0] == 0x11 && (p[1] == 0x11 || p[1] == 0x14 || p[1] == 0x16)) {
        v = p[12] << 8 | p[13];
        if (v < 30 || v > 60) v = 0;
    }
    if (!v) return r;
    r.ok = true; r.data = p;
    // ROM de 1 Mo non répertoriée : la ROM complète d'une CD32
    if (p.size() == 1048576) { r.label = "ROM CD32 complète (1 Mo)"; r.tags.insert("cd32main"); return r; }
    r.label = "Kickstart " + kickstarts::family(v) + " (" + std::to_string(v) + ")";
    if (v == 34) r.tags.insert("a500"); else if (v >= 39) r.tags.insert("a1200");
    return r;
}

std::vector<std::string> cueTracks(const std::string &text) {
    std::vector<std::string> r;
    static const std::regex re(R"re((?:^|\n)\s*FILE\s+(?:"([^"]+)"|(\S+)))re", std::regex::icase);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
        std::string f = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
        auto slash = f.find_last_of("/\\");
        r.push_back(slash == std::string::npos ? f : f.substr(slash + 1));
    }
    return r;
}
std::string parentOf(const std::string &p) { return fs::u8path(p).parent_path().u8string(); }
}  // namespace

std::vector<Item> walk(const std::string &root, std::atomic<bool> &stop, std::atomic<int> &count) {
    std::vector<Item> out;
    std::error_code ec;
    fs::path base = fs::u8path(root);
    std::string top = base.filename().u8string();
    if (top.empty()) top = base.parent_path().filename().u8string();
    auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (stop || out.size() >= MAX) break;
        std::string n = it->path().filename().u8string();
        bool dir = it->is_directory(ec);
        if (n.empty() || n[0] == '.' || n == "__MACOSX" || n == "$RECYCLE.BIN" || n == "System Volume Information") {
            if (dir) it.disable_recursion_pending();
            continue;
        }
        if (dir) { if (it.depth() >= 12) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        Item i;
        i.path = it->path().u8string();
        i.rel = top + "/" + fs::relative(it->path(), base, ec).generic_u8string();
        i.size = (uint64_t)it->file_size(ec);
        out.push_back(std::move(i));
        count = (int)out.size();
    }
    // Les fichiers les moins enfouis d'abord : l'original passe avant ses copies rangées plus loin
    std::stable_sort(out.begin(), out.end(), [](const Item &a, const Item &b) {
        auto da = std::count(a.rel.begin(), a.rel.end(), '/'), db = std::count(b.rel.begin(), b.rel.end(), '/');
        return da != db ? da < db : a.rel < b.rel;
    });
    return out;
}

void classify(std::vector<Item> &items, std::atomic<bool> &stop, std::atomic<int> &at) {
    auto norm = [](const std::string &k) { return k == library::WHDHDF ? std::string(library::HDF) : k; };
    std::set<std::string> lib;
    for (auto &it : library::all()) lib.insert(norm(it.kind) + "|" + lower(it.name()));
    std::set<uint32_t> roms;
    for (auto &r : kickstarts::list()) roms.insert(r.crc);
    for (auto n : {"cd32.rom", "cd32ext.rom"}) { auto d = readAll(paths::sys() + n); if (!d.empty()) roms.insert(kickstarts::crc32(d)); }
    std::set<std::string> cfgs;
    for (auto &n : configs::names()) cfgs.insert(lower(n));
    std::vector<uint8_t> key;
    for (auto &i : items) if (paths::ext(i.path) == "key" && i.size > 0 && i.size < 65536) { key = readAll(i.path); break; }
    std::map<std::string, std::vector<size_t>> byDir;
    for (size_t k = 0; k < items.size(); k++) byDir[parentOf(items[k].path)].push_back(k);
    std::set<std::string> seen, tracks;
    static const std::regex desc(R"((?:^|\n)\s*config_description\s*=\s*([^\r\n]+))", std::regex::icase);
    for (size_t k = 0; k < items.size(); k++) {
        if (stop) break;
        at = (int)k;
        Item &it = items[k];
        std::string n = lower(it.name()), x = paths::ext(n);
        if (x == "key") { if (it.size > 0 && it.size < 65536) { it.kind = KEY; it.why = "sert à lire les ROM chiffrées d'Amiga Forever du dossier"; } }
        else if (x == "adf" || x == "adz" || x == "dms" || x == "ipf" || x == "scp" || (n.size() > 7 && n.compare(n.size() - 7, 7, ".adf.gz") == 0)) it.kind = library::FLOPPY;
        else if (x == "lha" || x == "lzh") { if (hasSlave(it.path)) it.kind = library::WHD; }
        else if (x == "hdf" || x == "hdz" || x == "vhd") {
            it.kind = library::HDF;
            if (zipimport::bootsWhdload(it.path)) { it.kind = library::WHDHDF; it.label = "jeu WHDLoad sur disque dur (▶ Jouer)"; }
        }
        else if (x == "iso" || x == "chd") it.kind = library::CD;
        else if (x == "cue" && it.size > 0 && it.size < 65536) {
            auto wanted = cueTracks(readText(it.path));
            if (!wanted.empty()) {
                it.kind = library::CD;
                std::string missing;
                for (auto &w : wanted) {
                    bool got = false;
                    for (size_t j : byDir[parentOf(it.path)]) if (lower(items[j].name()) == lower(w)) { got = true; tracks.insert(items[j].path); }
                    if (!got) missing += (missing.empty() ? "" : ", ") + w;
                }
                if (!missing.empty()) it.why = "pistes absentes du dossier : " + missing;
            }
        }
        else if (x == "uae" && it.size > 0 && it.size < (1 << 20)) {
            it.kind = UAE;
            std::string text = readText(it.path);
            std::smatch m;
            std::string d = std::regex_search(text, m, desc) ? m[1].str() : "";
            while (!d.empty() && (d.back() == ' ' || d.back() == '\t')) d.pop_back();
            it.label = (d.empty() ? it.name().substr(0, it.name().rfind('.')) : d).substr(0, 60);
            if (cfgs.count(lower(it.label))) it.why = "déjà dans les configurations";
        }
        else if (x == "zip") {
            std::string zk = zipKind(it.path);
            if (!zk.empty()) {
                it.kind = zk;
                it.label = zk == ZIP ? "disque dur ou jeu à extraire" : zk == library::WHD ? "dossier d'un jeu WHDLoad" : "disquette zippée";
            }
        }
        // Kickstart : .rom, .bin, ou un fichier de la taille d'une ROM
        bool romSize = it.size == 262144 || it.size == 524288 || it.size == 1048576 || it.size == 262155 || it.size == 524299 || it.size == 1048587;
        if (it.kind.empty() && (((x == "rom" || x == "bin") && it.size > 0 && it.size <= 2 * 1048576 + 11) || romSize)) {
            RomInfo info = romInfo(readAll(it.path), key);
            if (info.ok) {
                it.kind = library::ROM; it.label = info.label; it.tags = info.tags; it.rom = info.data;
                if (info.aros) it.why = "inutile";
                else if (!info.plain) it.why = "illisible sans rom.key";
                else {
                    uint32_t c = kickstarts::crc32(info.data);
                    if (roms.count(c)) it.why = "déjà installé";
                    else if (!seen.insert("rom|" + std::to_string(c)).second) it.why = "en double dans le dossier";
                }
            }
        }
        if (it.kind.empty()) continue;
        const std::string &kd = it.kind;
        if (it.why.empty() && (kd == library::FLOPPY || kd == library::WHD || kd == library::WHDHDF || kd == library::HDF || kd == library::CD || kd == ZIP)) {
            std::string id = norm(kd == ZIP ? std::string(library::FLOPPY) : kd) + "|" + n;
            // Empreinte des disquettes et des jeux (petits fichiers) : même contenu sous un autre nom
            std::string c = (kd == library::FLOPPY || kd == library::WHD) && it.size < (64u << 20) ? "crc|" + std::to_string(kickstarts::crc32(readAll(it.path))) : "";
            if (lib.count(id) || (kd == ZIP && (lib.count(std::string(library::HDF) + "|" + n) || lib.count(std::string(library::WHD) + "|" + n)))) {
                it.why = "déjà dans la Bibliothèque";
                if (!c.empty()) seen.insert(c);
            }
            else if (!seen.insert(id).second || (!c.empty() && !seen.insert(c).second)) it.why = "en double dans le dossier";
        }
        if (!it.why.empty()) it.on = false;
    }
    // Les pistes d'un .cue ne comptent pas à part
    for (auto &it : items) if (it.kind.empty() && tracks.count(it.path)) it.why = "piste";
}

std::set<std::string> libTags() {
    std::set<std::string> t;
    for (auto &r : kickstarts::list()) {
        if (r.cdtvExt()) { t.insert("cdtvext"); continue; }
        if (r.models().count("CD32")) continue;
        if (kickstarts::fits(r, "A1200")) t.insert("a1200");
        if (r.version == 34 || r.label().rfind("Kickstart 1.3", 0) == 0) t.insert("a500");
    }
    if (kickstarts::cd32Rom()) t.insert("cd32main");
    return t;
}
std::string kickLine(const std::set<std::string> &t) {
    const std::pair<const char *, const char *> parts[] = {{"Amiga 1200 (3.x)", "a1200"}, {"Amiga 500 (1.3)", "a500"}, {"CD32", "cd32main"}, {"CDTV (extension)", "cdtvext"}};
    std::string r;
    for (auto &[n, k] : parts) r += std::string(r.empty() ? "" : " · ") + (t.count(k) ? "✓ " : "✗ ") + n;
    return r;
}

Report importAll(std::vector<Item> &items, std::atomic<bool> &stop, std::atomic<int> &at) {
    Report r;
    std::map<std::string, int> counts;
    std::vector<std::string> order;
    auto count = [&](const std::string &k) { if (!counts.count(k)) order.push_back(k); counts[k]++; };
    int i = 0;
    for (auto &it : items) {
        if (stop) break;
        if (it.kind.empty() || !it.on || !it.why.empty()) continue;
        at = i++;
        if (it.kind == library::ROM) {
            if (it.rom.empty()) continue;
            bool cd32 = it.tags.count("cd32main") || it.tags.count("cd32ext");
            if (cd32) {
                std::ofstream o(fs::u8path(paths::sys() + (it.tags.count("cd32ext") ? "cd32ext.rom" : "cd32.rom")), std::ios::binary);
                o.write((const char *)it.rom.data(), (std::streamsize)it.rom.size());
                count(it.kind);
            } else if (kickstarts::installData(it.rom)) count(it.kind);
            else r.notes.push_back(it.name() + " : pas la taille d'un Kickstart");
        } else if (it.kind == ZIP) {
            auto res = zipimport::inspect(it.path);
            for (auto &f : res.found) r.lib.push_back({f.path, f.kind});
            if (!res.problem.empty()) r.notes.push_back(res.problem);
            if (res.whdArchive) r.lib.push_back({it.path, library::WHD});
            else if (res.found.empty() && res.problem.empty()) r.lib.push_back({it.path, library::FLOPPY});
            if (!res.found.empty() || res.whdArchive) count(it.kind);
        } else if (it.kind == UAE) {
            r.uae.push_back(it.path + "\n" + it.label);
            count(it.kind);
        } else if (it.kind != KEY) {
            r.lib.push_back({it.path, it.kind});
            count(it.kind);
        }
    }
    // Compte rendu dans l'ordre des rubriques
    std::vector<std::string> ord;
    for (auto &c : cats) { ord.push_back(c.first); if (c.first == library::WHD) ord.push_back(library::WHDHDF); }
    for (auto &k : ord) if (counts.count(k)) r.added.push_back({k, counts[k]});
    return r;
}

std::string apply(Settings &s, Report &r, bool stopped) {
    for (auto &[p, k] : r.lib) library::add(p, k);
    // Configurations .uae : enregistrées sous leur nom, sans changer la machine en cours
    for (auto &u : r.uae) {
        auto nl = u.find('\n');
        std::string file = u.substr(0, nl), name = u.substr(nl + 1);
        if (name.empty()) name = paths::name(file).substr(0, paths::name(file).rfind('.'));
        configs::save(s, "(scan en cours)");
        configs::importUae(s, file);
        configs::save(s, name);
        configs::restore(s, "(scan en cours)");
        configs::remove(s, "(scan en cours)");
    }
    bool roms = false;
    for (auto &a : r.added) if (a.first == library::ROM) roms = true;
    Rom best;
    if (roms && s.rom() == "aros" && kickstarts::best("A1200", best)) s.set("rom", "user");
    static const std::map<std::string, std::pair<const char *, const char *>> names = {
        {library::ROM, {"Kickstart ou ROM", "Kickstart ou ROM"}}, {library::FLOPPY, {"disquette", "disquettes"}},
        {library::WHD, {"jeu WHDLoad", "jeux WHDLoad"}}, {library::WHDHDF, {"jeu WHDLoad sur disque dur", "jeux WHDLoad sur disque dur"}},
        {ZIP, {"archive", "archives"}}, {library::HDF, {"disque dur", "disques durs"}}, {library::CD, {"CD", "CD"}},
        {UAE, {"configuration", "configurations"}}};
    std::string parts;
    for (auto &[k, n] : r.added) {
        auto it = names.find(k);
        parts += (parts.empty() ? "" : ", ") + std::to_string(n) + " " + (it == names.end() ? k : n > 1 ? it->second.second : it->second.first);
    }
    std::string m = parts.empty() ? "Rien n'a été ajouté." : "Ajouté : " + parts + ".";
    if (stopped) m += "\n\nAjout arrêté avant la fin.";
    for (size_t i = 0; i < r.notes.size() && i < 30; i++) m += (i ? "\n" : "\n\n") + r.notes[i];
    return m;
}
}  // namespace scan
