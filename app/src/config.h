// Configuration WinUAE écrite pour Amiberry (même logique que les versions Android et Mac)
#pragma once
#include <string>
#include "store.h"
#include "kickstarts.h"

namespace config {
// Contenu à donner au cœur : une archive WHDLoad en DF0: se lance seule, sinon la configuration .uae
std::string content(Settings &s);
// whdHdf : jeu installé sur un .hdf qui démarre WHDLoad tout seul ; arosOnly : essai avec AROS, sans disque
std::string write(Settings &s, bool arosOnly = false, const std::string &whdHdf = "");
bool hasRdb(const std::string &path);
// Kickstart utilisé pour cette machine (faux : AROS)
bool kick(Settings &s, const std::string &model, Rom &out);
}
