// Plusieurs Kickstart : chacun est choisi automatiquement pour la machine qui lui convient (1.3 pour l'Amiga 500,
// 3.1 pour l'A1200…), comme les versions Android et Mac. Ils sont rangés dans sys (kick-<crc>.rom).
#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>

struct Rom {
    std::string path;
    uint32_t crc = 0;
    int version = 0, revision = 0;
    std::string label() const;
    std::set<std::string> models() const;
    bool cdtvExt() const;
};

namespace kickstarts {
uint32_t crc32(const std::vector<uint8_t> &d);
std::vector<Rom> list();
// Ajoute un Kickstart ; renvoie vrai, ou faux et le motif du refus
bool install(const std::string &path, std::string &why, Rom *out = nullptr);
void remove(const Rom &r);
bool fits(const Rom &r, const std::string &model);
bool best(const std::string &model, Rom &out);
bool cdtvExt(Rom &out);
bool hasForWhd();
bool cd32Rom();
std::string family(int v);
// Nom attendu par WHDLoad (paquet skick), vide si inconnu
std::string whdName(uint32_t crc);
}
