// Sauvegardes d'état (4 emplacements par jeu, avec image), configurations enregistrées et format WinUAE (.uae),
// « Changer de PC » (tout dans un .zip)
#pragma once
#include <string>
#include <vector>
#include <map>
#include "store.h"

namespace states {
std::string key(const std::string &game);
std::string state(const std::string &game, int slot);
std::string thumb(const std::string &game, int slot);
bool save(const std::string &game, int slot);
bool load(const std::string &game, int slot);
// Date de l'emplacement (« 25/10/2026 10:06 »), vide s'il est libre
std::string date(const std::string &game, int slot);
}

namespace configs {
// Configurations enregistrées sous un nom : la machine, la mémoire et les disques
std::vector<std::string> names();
void save(Settings &s, const std::string &name);
bool restore(Settings &s, const std::string &name);
void remove(Settings &s, const std::string &name);
// Export lisible par WinUAE, Amiberry et les autres versions ; import : compte rendu
bool exportUae(Settings &s, const std::string &file);
std::string importUae(Settings &s, const std::string &file);
}

namespace backup {
std::string exportTo(const std::string &zipFile);
std::string importFrom(const std::string &zipFile);
}
