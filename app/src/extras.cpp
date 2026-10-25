#include "extras.h"
#include "bridge.h"
#include "config.h"
#include "library.h"
#include "platform.h"
#include "zip.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::string fileDate(const std::string &f) {
    std::error_code ec;
    auto ft = fs::last_write_time(fs::u8path(f), ec);
    if (ec) return "";
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    time_t t = std::chrono::system_clock::to_time_t(sys);
    char b[32];
    strftime(b, sizeof b, "%d/%m/%Y %H:%M", localtime(&t));
    return b;
}

namespace states {
std::string key(const std::string &game) {
    std::string k = game;
    auto d = k.rfind('.');
    if (d != std::string::npos && d > 0) k = k.substr(0, d);
    for (auto &c : k) if (c == '/' || c == '\\' || c == ':') c = '_';
    return k;
}
std::string state(const std::string &game, int slot) { return paths::dir("etats") + key(game) + "-" + std::to_string(slot + 1) + ".uss"; }
std::string thumb(const std::string &game, int slot) { return paths::dir("etats") + key(game) + "-" + std::to_string(slot + 1) + ".jpg"; }
bool save(const std::string &game, int slot) {
    bool ok = amb_save_state(state(game, slot).c_str()) != 0;
    if (ok) captures::writeFrame(thumb(game, slot), 320);
    return ok;
}
bool load(const std::string &game, int slot) {
    std::string f = state(game, slot);
    return paths::exists(f) && amb_load_state(f.c_str()) != 0;
}
std::string date(const std::string &game, int slot) {
    std::string f = state(game, slot);
    return paths::exists(f) ? fileDate(f) : "";
}
}  // namespace states

namespace configs {
static const char *keys[] = {"model", "jit", "rtg", "ntsc", "shared", "chip", "slow", "fast", "accel", "df0", "df1", "df2", "df3", "hds", "cd"};

std::vector<std::string> names() {
    std::vector<std::string> r;
    for (auto &k : Settings::shared().keysWithPrefix("config:")) r.push_back(k.substr(7));
    std::sort(r.begin(), r.end());
    return r;
}
void save(Settings &s, const std::string &name) {
    std::string v;
    for (auto k : keys) {
        std::string x = s.get(k, "\x01");
        if (x == "\x01") continue;
        for (auto &c : x) if (c == '\n') c = ' ';
        v += std::string(k) + "=" + x + "\n";
    }
    s.set("config:" + name, v);
}
bool restore(Settings &s, const std::string &name) {
    std::string v = s.get("config:" + name);
    if (v.empty()) return false;
    std::istringstream is(v);
    std::string line;
    for (auto k : keys) s.remove(k);
    while (std::getline(is, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) s.set(line.substr(0, eq), line.substr(eq + 1));
    }
    return true;
}
void remove(Settings &s, const std::string &name) { s.remove("config:" + name); }

bool exportUae(Settings &s, const std::string &file) {
    std::ifstream in(fs::u8path(config::write(s)));
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(fs::u8path(file), std::ios::trunc);
    out << "; Configuration exportée par amiga1200-pc\n" << body;
    return (bool)out;
}

// Import : machine (modèle, processeur, RTG, NTSC, mémoire) et disques, retrouvés à leur place ou par leur nom
std::string importUae(Settings &s, const std::string &file) {
    std::ifstream in(fs::u8path(file));
    if (!in) return "Lecture impossible";
    std::vector<std::pair<std::string, std::string>> kv;
    std::string line;
    auto trim = [](std::string x) {
        while (!x.empty() && (x.back() == ' ' || x.back() == '\r' || x.back() == '\t')) x.pop_back();
        size_t a = x.find_first_not_of(" \t");
        return a == std::string::npos ? std::string() : x.substr(a);
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv.push_back({trim(line.substr(0, eq)), trim(line.substr(eq + 1))});
    }
    auto value = [&](const std::string &k) -> std::string { for (auto &[a, b] : kv) if (a == k) return b; return ""; };
    auto lib = library::all();
    std::vector<std::string> missing;
    auto find = [&](std::string p) -> std::string {
        if (!p.empty() && p.front() == '"') p = p.substr(1, p.size() - 2);
        if (paths::exists(p)) return p;
        std::string n = p.substr(p.find_last_of("/\\") == std::string::npos ? 0 : p.find_last_of("/\\") + 1), ln = n;
        std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
        for (auto &it : lib) { std::string m = it.name(); std::transform(m.begin(), m.end(), m.begin(), ::tolower); if (m == ln) return it.path; }
        if (!n.empty()) missing.push_back(n);
        return "";
    };
    std::string q = value("quickstart");
    q = q.substr(0, q.find(','));
    std::transform(q.begin(), q.end(), q.begin(), ::toupper);
    if (q == "A500" || q == "A600" || q == "A1200" || q == "A3000" || q == "A4000" || q == "CD32" || q == "CDTV") s.set("model", q);
    else if (q == "A1000" || q == "A2000" || q == "A500+") s.set("model", "A500");
    else if (q == "A4000T") s.set("model", "A4000");
    std::string cpuSpeed = value("cpu_speed");
    if (!value("cachesize").empty() || !cpuSpeed.empty()) s.setBool("jit", atoi(value("cachesize").c_str()) > 0 || cpuSpeed == "max");
    bool rtg = false;
    for (auto &[a, b] : kv) if (a == "gfxcard_size" && atoi(b.c_str()) > 0) rtg = true;
    s.setBool("rtg", rtg);
    if (!value("ntsc").empty()) { std::string n = value("ntsc"); s.setBool("ntsc", n == "true" || n == "TRUE"); }
    if (!value("chipmem_size").empty()) s.setInt("chip", atoi(value("chipmem_size").c_str()));
    if (!value("bogomem_size").empty()) s.setInt("slow", atoi(value("bogomem_size").c_str()));
    if (!s.fastCpu() && !value("fastmem_size").empty()) {
        int v = atoi(value("fastmem_size").c_str());
        s.setInt("fast", v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 0);
    }
    for (int i = 0; i < 4; i++) { std::string f = value("floppy" + std::to_string(i)); s.setFloppy(i, f.empty() ? "" : find(f)); }
    std::vector<std::string> hds;
    for (auto &[a, b] : kv) {
        if (a != "hardfile2" && a != "hardfile") continue;
        std::vector<std::string> f;
        std::stringstream ss(b);
        std::string part;
        while (std::getline(ss, part, ',')) f.push_back(part);
        if (f.size() < 2) continue;
        std::string p = f[1];
        // « DH0:chemin » ; sous Windows le chemin a lui-même un « C: »
        auto colon = p.find(':');
        if (colon != std::string::npos && colon >= 2) p = p.substr(colon + 1);
        std::string h = find(p);
        if (!h.empty()) hds.push_back(h);
    }
    s.setList("hds", hds);
    std::string cd = value("cdimage0");
    if (!cd.empty()) s.set("cd", find(cd.substr(0, cd.find(",image"))));
    if (missing.empty()) return "Configuration importée";
    std::string m = "Configuration importée. Introuvables (ajoutez-les à la Bibliothèque puis importez à nouveau) : ";
    for (size_t i = 0; i < missing.size(); i++) m += (i ? ", " : "") + missing[i];
    return m;
}
}  // namespace configs

namespace backup {
static const char *dirs[] = {"sys", "save", "etats", "captures", "Disques"};

static void addTree(zip::Writer &w, const fs::path &root, const fs::path &dir, const std::string &prefix, int &files) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        std::string rel = fs::relative(it->path(), root, ec).generic_u8string();
        // Fichiers recopiés depuis l'application (AROS, WHDBoot hors sauvegardes des jeux) : inutiles
        if (rel.rfind("sys/aros-", 0) == 0) continue;
        if (rel.rfind("save/WHDBoot/", 0) == 0 && rel.rfind("save/WHDBoot/save-data/", 0) != 0) continue;
        if (w.addFile(it->path().u8string(), prefix + rel)) files++;
    }
}

std::string exportTo(const std::string &zipFile) {
    zip::Writer w(zipFile);
    if (!w.ok()) return "Export impossible";
    Settings &s = Settings::shared();
    s.load();
    std::ifstream in(fs::u8path(paths::support() + "reglages.ini"));
    std::string prefs((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Dossier de l'application, pour réécrire les chemins sur l'autre PC
    w.addData(paths::support() + "\n", "dossier.txt");
    w.addData(prefs, "reglages.ini");
    int files = 0;
    fs::path root = fs::u8path(paths::support());
    for (auto d : dirs) if (fs::exists(root / d)) addTree(w, root, root / d, "", files);
    fs::path shared = fs::u8path(paths::shared());
    addTree(w, shared.parent_path(), shared, "", files);
    if (!w.close()) return "Export impossible";
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(zipFile), ec);
    return "Sauvegarde exportée (" + std::to_string(size >> 20) + " Mo, " + std::to_string(files) +
           " fichiers). Elle contient vos Kickstart : gardez-la pour vous.";
}

std::string importFrom(const std::string &zipFile) {
    std::vector<zip::Entry> es;
    if (!zip::list(zipFile, es)) return "Ce fichier n'est pas une sauvegarde d'Amiga 1200 (ou il est abîmé).";
    auto find = [&](const std::string &n) -> const zip::Entry * { for (auto &e : es) if (e.name == n) return &e; return nullptr; };
    const zip::Entry *pe = find("reglages.ini"), *de = find("dossier.txt");
    if (!pe) return "Ce fichier n'est pas une sauvegarde d'Amiga 1200 (ou il est abîmé).";
    std::string tmp = paths::dir("cfg") + "import-";
    if (!zip::extract(zipFile, *pe, tmp + "reglages.ini")) return "Sauvegarde abîmée.";
    std::string oldDir;
    if (de && zip::extract(zipFile, *de, tmp + "dossier.txt")) { std::ifstream f(fs::u8path(tmp + "dossier.txt")); std::getline(f, oldDir); }
    // Fichiers : ajoutés ou remplacés (SHARED/ va dans Documents/Amiga1200)
    int files = 0;
    fs::path root = fs::u8path(paths::support()), shared = fs::u8path(paths::shared()).parent_path();
    for (auto &e : es) {
        if (e.dir() || e.name == "reglages.ini" || e.name == "dossier.txt" || e.name.find("..") != std::string::npos) continue;
        std::string top = e.name.substr(0, e.name.find('/'));
        fs::path dest;
        if (top == "SHARED") dest = shared / fs::u8path(e.name);
        else if (std::find_if(std::begin(dirs), std::end(dirs), [&](const char *d) { return top == d; }) != std::end(dirs)) dest = root / fs::u8path(e.name);
        else continue;
        if (zip::extract(zipFile, e, dest.u8string())) files++;
    }
    // Réglages : remplacés ; les chemins de l'ancien dossier de l'application pointent vers celui de ce PC
    std::ifstream in(fs::u8path(tmp + "reglages.ini"));
    std::string prefs((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (!oldDir.empty() && oldDir != paths::support()) {
        // Chemins écrits avec des barres doublées dans le fichier (\\) sous Windows
        auto replaceAll = [&](std::string from, std::string to) {
            for (size_t p = prefs.find(from); p != std::string::npos; p = prefs.find(from, p + to.size())) prefs.replace(p, from.size(), to);
        };
        auto esc = [](std::string x) { std::string r; for (char c : x) { if (c == '\\') r += "\\\\"; else r += c; } return r; };
        replaceAll(esc(oldDir), esc(paths::support()));
    }
    std::ofstream(fs::u8path(paths::support() + "reglages.ini"), std::ios::trunc) << prefs;
    std::error_code ec;
    fs::remove(fs::u8path(tmp + "reglages.ini"), ec);
    fs::remove(fs::u8path(tmp + "dossier.txt"), ec);
    Settings::shared().load();
    std::vector<std::string> lost;
    for (auto &it : library::all()) if (!it.exists()) lost.push_back(it.name());
    std::string m = "Sauvegarde importée : réglages et " + std::to_string(files) + " fichier" + (files > 1 ? "s." : ".");
    if (!lost.empty()) {
        m += " Disques à rajouter à la Bibliothèque (introuvables sur ce PC) : ";
        for (size_t i = 0; i < lost.size(); i++) m += (i ? ", " : "") + lost[i];
    }
    return m;
}
}  // namespace backup
