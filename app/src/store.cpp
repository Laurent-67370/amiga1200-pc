#include "store.h"
#include "platform.h"
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace paths {
static fs::path P(const std::string &s) { return fs::u8path(s); }
static std::string S(const fs::path &p) { return p.u8string(); }

std::string support() {
    static std::string s = [] {
        char *p = SDL_GetPrefPath(nullptr, "Amiga1200");
        std::string r = p ? p : "./";
        SDL_free(p);
        return r;
    }();
    return s;
}
std::string dir(const std::string &name) {
    std::string d = support() + name + "/";
    std::error_code ec;
    fs::create_directories(P(d), ec);
    return d;
}
std::string sys() { return dir("sys"); }
std::string save() { return dir("save"); }
std::string data() { return dir("data"); }
std::string log() { return support() + "journal.txt"; }
std::string config() { return dir("cfg") + "amiga.uae"; }
std::string shared() {
    std::string d = S(P(platform::documentsDir()) / "Amiga1200" / "SHARED");
    std::error_code ec;
    fs::create_directories(P(d) / "SaveGames", ec);
    return d;
}
std::string resources() { return platform::exeDir() + "resources/"; }
std::string name(const std::string &path) { return S(P(path).filename()); }
std::string ext(const std::string &path) {
    std::string e = S(P(path).extension());
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)tolower(c); });
    return e;
}
bool exists(const std::string &path) { std::error_code ec; return !path.empty() && fs::exists(P(path), ec); }

// Copie d'un dossier ; les sauvegardes des jeux (save-data) déjà présentes ne sont pas écrasées
static void copyTree(const fs::path &src, const fs::path &dst) {
    std::error_code ec;
    if (!fs::exists(src, ec)) return;
    if (!fs::is_directory(src, ec)) {
        if (fs::exists(dst, ec)) {
            if (S(dst).find("save-data") != std::string::npos) return;
            fs::remove(dst, ec);
        }
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, ec);
        return;
    }
    for (auto &e : fs::directory_iterator(src, ec)) copyTree(e.path(), dst / e.path().filename());
}

// Ressources copiées une fois par version : ROM AROS, WHDLoad d'Amiberry, bruits des lecteurs
void installResources() {
    std::error_code ec;
    std::string stamp = support() + ".ressources";
    std::string version = AMIGA1200_VERSION;
    {
        std::ifstream f(P(stamp));
        std::string v;
        if (std::getline(f, v) && v == version) return;
    }
    fs::path res = P(resources());
    for (auto n : {"aros-rom.bin", "aros-ext.bin"}) {
        fs::path d = P(sys()) / n;
        fs::remove(d, ec);
        fs::copy_file(res / "aros" / n, d, ec);
    }
    fs::path sounds = P(data()) / "floppy_sounds";
    fs::remove_all(sounds, ec);
    copyTree(res / "floppy_sounds", sounds);
    copyTree(res / "whdboot", P(save()) / "WHDBoot");
    for (auto d : {"Autoboots", "Debugs", "Kickstarts", "Savegames"}) fs::create_directories(P(save()) / "WHDBoot" / "save-data" / d, ec);
    std::ofstream(P(stamp)) << version << "\n";
}
}  // namespace paths

Settings &Settings::shared() {
    static Settings s;
    static bool loaded = false;
    if (!loaded) { loaded = true; s.load(); }
    return s;
}

void Settings::load() {
    file = paths::support() + "reglages.ini";
    kv.clear();
    std::ifstream f(fs::u8path(file));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        std::string v = line.substr(eq + 1), u;
        // \n et \\ : retours à la ligne et barres obliques inverses dans les valeurs
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i] == '\\' && i + 1 < v.size()) { char c = v[++i]; u += c == 'n' ? '\n' : c == 't' ? '\t' : c; }
            else u += v[i];
        }
        kv[line.substr(0, eq)] = u;
    }
}

void Settings::flush() {
    std::string tmp = file + ".tmp";
    {
        std::ofstream f(fs::u8path(tmp), std::ios::trunc);
        f << "# Réglages d'Amiga 1200\n";
        for (auto &[k, v] : kv) {
            std::string e;
            for (char c : v) { if (c == '\\') e += "\\\\"; else if (c == '\n') e += "\\n"; else if (c == '\t') e += "\\t"; else e += c; }
            f << k << "=" << e << "\n";
        }
    }
    std::error_code ec;
    fs::rename(fs::u8path(tmp), fs::u8path(file), ec);
}

std::string Settings::get(const std::string &k, const std::string &def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second;
}
// Lecture, changement, écriture : l'accueil et l'émulation (deux processus) écrivent tous deux dans ce fichier
void Settings::set(const std::string &k, const std::string &v) { load(); kv[k] = v; flush(); }
void Settings::remove(const std::string &k) { load(); kv.erase(k); flush(); }
bool Settings::getBool(const std::string &k, bool def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second == "1" || it->second == "true";
}
int Settings::getInt(const std::string &k, int def) const {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
double Settings::getDouble(const std::string &k, double def) const {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    std::istringstream is(it->second);
    is.imbue(std::locale::classic());
    double d;
    return is >> d ? d : def;
}
void Settings::setDouble(const std::string &k, double v) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << v;
    set(k, os.str());
}
std::vector<std::string> Settings::getList(const std::string &k) const {
    std::vector<std::string> r;
    std::string v = get(k), cur;
    for (char c : v) { if (c == '\t') { if (!cur.empty()) r.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) r.push_back(cur);
    return r;
}
void Settings::setList(const std::string &k, const std::vector<std::string> &v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) { if (i) s += '\t'; s += v[i]; }
    set(k, s);
}
std::vector<std::string> Settings::keysWithPrefix(const std::string &p) const {
    std::vector<std::string> r;
    for (auto &[k, v] : kv) if (k.compare(0, p.size(), p) == 0) r.push_back(k);
    return r;
}

std::string Settings::bootKey() const {
    std::string p = cdConsole(model()) ? cd() : !floppy(0).empty() ? floppy(0) : hds().empty() ? "" : hds()[0];
    return paths::name(p);
}

// Machine, mémoire et processeur retenus pour chaque disque de démarrage
static const char *profileKeys[] = {"model", "jit", "rtg", "chip", "slow", "fast", "accel"};
void Settings::saveProfile() {
    std::string k = bootKey();
    if (k.empty()) return;
    std::string p;
    for (auto key : profileKeys) if (kv.count(key)) p += std::string(key) + "=" + kv[key] + "\n";
    set("profil:" + k, p);
}
bool Settings::applyProfile() {
    load();
    std::string p = get("profil:" + bootKey());
    if (p.empty()) return false;
    std::istringstream is(p);
    std::string line;
    while (std::getline(is, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    flush();
    return true;
}

std::string Settings::signature() const {
    std::string s;
    for (auto k : {"model", "jit", "rtg", "ntsc", "rom", "romName", "df0", "df1", "df2", "df3", "cd", "hds", "chip", "slow", "fast", "accel"}) s += get(k) + "#";
    return s;
}
std::string Settings::resumeFile() { return paths::save() + "reprise.uss"; }

bool Settings::isWhd(const std::string &path) {
    std::string e = paths::ext(path);
    return e == "lha" || e == "lzh" || e == "lzx";
}
