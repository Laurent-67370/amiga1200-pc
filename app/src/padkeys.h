// Boutons de manette changés en touches du clavier Amiga, pour un jeu (comme les autres versions) : la barre d'espace
// d'un jeu, F1 pour choisir le nombre de joueurs, Return pour la carte…
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace padkeys {
// Boutons réglables (A et B restent les deux boutons de tir)
const std::vector<std::string> &buttons();
// Touches proposées (codes RETROK du cœur) ; 0 : le bouton reste un bouton de manette
const std::vector<std::pair<int, std::string>> &keys();
std::map<std::string, int> load(const std::string &game);
void save(const std::string &game, const std::map<std::string, int> &m);
}
