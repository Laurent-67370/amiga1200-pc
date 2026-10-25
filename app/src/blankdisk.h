// Disquette vierge, formatée (DOS, système de fichiers OFS) : 880 Ko, nom de volume au choix (comme les autres versions)
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace blankdisk {
std::vector<uint8_t> adf(const std::string &label);
bool write(const std::string &file, const std::string &label);
}
