#include "blankdisk.h"
#include <ctime>
#include <filesystem>
#include <fstream>

namespace blankdisk {
std::vector<uint8_t> adf(const std::string &label) {
    const int blocks = 1760;
    std::vector<uint8_t> img((size_t)blocks * 512, 0);
    auto put = [&](size_t off, uint32_t v) { for (int i = 0; i < 4; i++) img[off + i] = (uint8_t)(v >> (24 - 8 * i)); };
    auto get = [&](size_t off) { uint32_t v = 0; for (int i = 0; i < 4; i++) v = v << 8 | img[off + i]; return v; };
    // Somme de contrôle AmigaDOS : l'opposé de la somme des 128 mots longs du bloc (celui de la somme compté nul)
    auto checksum = [&](size_t block, int at) { uint32_t sum = 0; for (int i = 0; i < 128; i++) if (i != at) sum += get(block + i * 4); return (uint32_t)(0 - sum); };
    put(0, 0x444F5300);   // « DOS\0 » : bloc de démarrage d'une disquette OFS (non démarrable)
    // Date AmigaDOS : jours depuis le 1er janvier 1978, minutes, cinquantièmes de seconde
    time_t now = time(nullptr);
    struct tm lt = *localtime(&now);
    struct tm e{}; e.tm_year = 78; e.tm_mon = 0; e.tm_mday = 1; e.tm_hour = 12;
    struct tm n{}; n.tm_year = lt.tm_year; n.tm_mon = lt.tm_mon; n.tm_mday = lt.tm_mday; n.tm_hour = 12;
    uint32_t days = (uint32_t)((mktime(&n) - mktime(&e) + 43200) / 86400);
    uint32_t mins = (uint32_t)(lt.tm_hour * 60 + lt.tm_min), ticks = (uint32_t)(lt.tm_sec * 50);
    // Bloc racine (880)
    size_t root = 880 * 512;
    put(root, 2);                  // T_HEADER
    put(root + 3 * 4, 72);         // taille de la table de hachage
    put(root + 78 * 4, 0xFFFFFFFF);   // carte des blocs valide
    put(root + 79 * 4, 881);       // bloc de la carte
    put(root + 105 * 4, days); put(root + 106 * 4, mins); put(root + 107 * 4, ticks);
    std::string vol;
    for (unsigned char c : label) if (c >= 32 && c < 127 && c != ':' && c != '/' && vol.size() < 30) vol += (char)c;
    if (vol.empty()) vol = "Vierge";
    img[root + 432] = (uint8_t)vol.size();
    for (size_t i = 0; i < vol.size(); i++) img[root + 433 + i] = (uint8_t)vol[i];
    put(root + 472, days); put(root + 476, mins); put(root + 480, ticks);   // modification du disque
    put(root + 484, days); put(root + 488, mins); put(root + 492, ticks);   // création
    put(root + 127 * 4, 1);        // ST_ROOT
    put(root + 5 * 4, checksum(root, 5));
    // Carte des blocs (881) : un bit par bloc à partir du bloc 2, 1 = libre ; 880 et 881 sont occupés
    size_t bm = 881 * 512;
    for (int b = 2; b < blocks; b++) {
        if (b == 880 || b == 881) continue;
        int i = b - 2;
        size_t off = bm + 4 + (size_t)(i / 32) * 4;
        put(off, get(off) | (1u << (i % 32)));
    }
    put(bm, checksum(bm, 0));
    return img;
}

bool write(const std::string &file, const std::string &label) {
    auto d = adf(label);
    std::ofstream f(std::filesystem::u8path(file), std::ios::binary | std::ios::trunc);
    f.write((const char *)d.data(), (std::streamsize)d.size());
    return (bool)f;
}
}
