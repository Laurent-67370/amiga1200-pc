#include "library.h"
#include "bridge.h"
#include "platform.h"
#include "store.h"
#include "zip.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include "stb_image_resize2.h"

namespace fs = std::filesystem;

std::string LibItem::name() const { return paths::name(path); }
std::string LibItem::title() const { std::string n = name(); auto d = n.rfind('.'); return d == std::string::npos || d == 0 ? n : n.substr(0, d); }
bool LibItem::exists() const { return paths::exists(path); }

namespace library {
const char *WHD = "whd", *FLOPPY = "floppy", *HDF = "hdf", *ROM = "rom", *CD = "cd", *WHDHDF = "whdhdf";

static double now() { return (double)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string kind(const std::string &path) {
    std::string n = paths::name(path);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)tolower(c); });
    std::string e = paths::ext(path);
    if (n.find("kickstart") != std::string::npos || e == "rom") return ROM;
    if (e == "lha" || e == "lzh" || e == "lzx") return WHD;
    if (e == "hdf" || e == "hdz" || e == "vhd") return HDF;
    if (e == "iso" || e == "chd" || e == "cue" || e == "ccd" || e == "nrg" || e == "mds") return CD;
    if (e == "adf" || e == "adz" || e == "dms" || e == "ipf" || e == "fdi" || e == "scp" || e == "zip" || e == "gz" || e == "7z" || e == "exe") return FLOPPY;
    return "";
}

// Réglage « bibliotheque » : une ligne par élément, « type, ajouté, utilisé, chemin » séparés par des tabulations
std::vector<LibItem> all() {
    std::vector<LibItem> r;
    std::istringstream is(Settings::shared().get("bibliotheque"));
    std::string line;
    while (std::getline(is, line)) {
        LibItem it;
        size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
        if (a == std::string::npos || b == std::string::npos || c == std::string::npos) continue;
        it.kind = line.substr(0, a);
        it.added = atof(line.substr(a + 1, b - a - 1).c_str());
        it.used = atof(line.substr(b + 1, c - b - 1).c_str());
        it.path = line.substr(c + 1);
        if (it.kind != ROM && !it.path.empty()) r.push_back(it);
    }
    std::stable_sort(r.begin(), r.end(), [](const LibItem &x, const LibItem &y) { return std::max(x.used, x.added) > std::max(y.used, y.added); });
    return r;
}

static void save(const std::vector<LibItem> &items) {
    std::string s;
    char b[64];
    for (auto &i : items) {
        snprintf(b, sizeof b, "\t%.0f\t%.0f\t", i.added, i.used);
        s += i.kind + b + i.path + "\n";
    }
    Settings::shared().set("bibliotheque", s);
}

bool add(const std::string &path, const std::string &given) {
    auto list = all();
    auto old = std::find_if(list.begin(), list.end(), [&](const LibItem &i) { return i.path == path; });
    // Un élément déjà rangé garde son type (un .hdf reconnu comme jeu WHDLoad le reste)
    std::string k = !given.empty() ? given : old != list.end() ? old->kind : kind(path);
    if (k.empty() || k == ROM) return false;
    LibItem it{path, k, old != list.end() ? old->added : now(), old != list.end() ? old->used : 0};
    if (old != list.end()) list.erase(old);
    list.push_back(it);
    save(list);
    return true;
}
void touch(const std::string &path) {
    auto list = all();
    for (auto &i : list) if (i.path == path) i.used = now();
    save(list);
}
void remove(const std::string &path) {
    auto list = all();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const LibItem &i) { return i.path == path; }), list.end());
    save(list);
}
}  // namespace library

namespace captures {
static std::string stem(const std::string &name) {
    std::string n = name;
    auto d = n.rfind('.');
    if (d != std::string::npos && d > 0) n = n.substr(0, d);
    for (auto &c : n) if (c == '/' || c == '\\' || c == ':') c = '_';
    return n;
}
std::string of(const std::string &name) { return paths::dir("captures") + stem(name) + ".jpg"; }

static void writeCb(void *ctx, void *data, int size) { fwrite(data, 1, (size_t)size, (FILE *)ctx); }

bool writeFrame(const std::string &file, int maxWidth) {
    unsigned w = 0, h = 0;
    const uint8_t *p = amb_frame(&w, &h);
    if (!p || !w || !h) return false;
    // Copie en RGB (l'image du cœur peut changer pendant l'écriture)
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; i++) { rgb[i * 3] = p[i * 4]; rgb[i * 3 + 1] = p[i * 4 + 1]; rgb[i * 3 + 2] = p[i * 4 + 2]; }
    int ow = (int)w, oh = (int)h;
    if (maxWidth > 0 && ow > maxWidth) {
        int nh = oh * maxWidth / ow;
        std::vector<uint8_t> small((size_t)maxWidth * nh * 3);
        stbir_resize_uint8_srgb(rgb.data(), ow, oh, 0, small.data(), maxWidth, nh, 0, STBIR_RGB);
        rgb.swap(small); ow = maxWidth; oh = nh;
    }
#ifdef _WIN32
    FILE *f = _wfopen(fs::u8path(file).wstring().c_str(), L"wb");
#else
    FILE *f = fopen(file.c_str(), "wb");
#endif
    if (!f) return false;
    int ok = paths::ext(file) == "png" ? stbi_write_png_to_func(writeCb, f, ow, oh, 3, rgb.data(), ow * 3)
                                       : stbi_write_jpg_to_func(writeCb, f, ow, oh, 3, rgb.data(), 85);
    fclose(f);
    return ok != 0;
}

bool save(const std::string &name, bool gallery, bool onlyIfMissing) {
    std::string f = of(name);
    if (onlyIfMissing && paths::exists(f)) return false;
    bool ok = writeFrame(f, 320);
    if (gallery) {
        // Images/Amiga1200 (Windows : Images ; Linux : dossier Images de xdg-user-dirs)
        std::string pics = platform::picturesDir() + "/Amiga1200";
        std::error_code ec;
        fs::create_directories(fs::u8path(pics), ec);
        ok = writeFrame(pics + "/" + stem(name) + "-" + std::to_string((long long)time(nullptr)) + ".png", 0) || ok;
    }
    return ok;
}

bool load(const std::string &file, std::vector<uint8_t> &rgba, int &w, int &h) {
    std::ifstream f(fs::u8path(file), std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.empty()) return false;
    int n = 0;
    uint8_t *p = stbi_load_from_memory(d.data(), (int)d.size(), &w, &h, &n, 4);
    if (!p) return false;
    rgba.assign(p, p + (size_t)w * h * 4);
    stbi_image_free(p);
    return true;
}
}  // namespace captures

namespace zipimport {
Result inspect(const std::string &zipPath) {
    Result r;
    std::string name = paths::name(zipPath);
    // Archive complète et sans erreur (sommes CRC vérifiées) ?
    std::vector<zip::Entry> es;
    if (!zip::list(zipPath, es) || !zip::test(zipPath)) {
        r.problem = name + " est incomplet ou abîmé (téléchargement interrompu ?) : retéléchargez-le.";
        return r;
    }
    bool slave = false;
    for (auto &e : es) {
        if (e.dir()) continue;
        std::string base = paths::name(e.name), ext = paths::ext(e.name);
        std::string lower = e.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() > 6 && lower.compare(lower.size() - 6, 6, ".slave") == 0) slave = true;
        std::string k = ext == "hdf" || ext == "hdz" ? library::HDF : ext == "lha" || ext == "lzh" ? library::WHD : "";
        if (k.empty()) continue;
        std::string dest = paths::dir("Disques") + base, part = dest + ".part";
        if (!zip::extract(zipPath, e, part)) { r.problem = name + " : extraction de " + base + " impossible."; continue; }
        std::error_code ec;
        fs::remove(fs::u8path(dest), ec);
        fs::rename(fs::u8path(part), fs::u8path(dest), ec);
        r.found.push_back({dest, k == library::HDF && bootsWhdload(dest) ? library::WHDHDF : k});
    }
    r.whdArchive = r.found.empty() && slave;
    return r;
}

bool bootsWhdload(const std::string &path) {
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    if (ec || size == 0 || size > (64u << 20)) return false;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // « WHDLoad Jeu.Slave Preload » : WHDLoad, au plus 80 caractères sans retour à la ligne, puis .slave
    std::string low = all;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)tolower(c); });
    for (size_t p = low.find("whdload"); p != std::string::npos; p = low.find("whdload", p + 7)) {
        size_t end = std::min(low.size(), p + 7 + 80 + 6);
        for (size_t q = p + 7; q < end; q++) {
            if (low[q] == '\n') break;
            if (low.compare(q, 6, ".slave") == 0) return true;
        }
    }
    return false;
}
}  // namespace zipimport
