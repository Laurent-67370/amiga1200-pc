// Scanner un dossier (comme les versions web, Android et Mac) : le dossier choisi est parcouru avec ses sous-dossiers,
// chaque fichier reconnu par son contenu (Kickstart par empreinte, ROM chiffrées d'Amiga Forever avec le rom.key du
// dossier, disquettes, jeux WHDLoad, disques durs, CD, configurations .uae, archives .zip). Un bilan permet de choisir
// ce qui rejoint la Bibliothèque ; les fichiers restent où ils sont (lus sur place) et les Kickstart sont installés
// comme ROM de la machine.
#pragma once
#include <atomic>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

class Settings;

namespace scan {
extern const char *KEY, *ZIP, *UAE;

struct Item {
    std::string path, rel;       // chemin, et chemin affiché (depuis le dossier choisi)
    uint64_t size = 0;
    std::string kind;            // vide : non reconnu
    std::string label;           // ce que c'est (Kickstart reconnu, nom de la configuration…)
    std::string why;             // pourquoi il n'est pas proposé ; vide : importable
    bool on = true;
    std::vector<uint8_t> rom;    // ROM en clair, prête à installer
    std::set<std::string> tags;  // usages : a1200, a500, cd32main, cd32ext, cdtvext
    std::string name() const;
};

// Tous les fichiers du dossier et de ses sous-dossiers (fichiers cachés et corbeilles ignorés, 20 000 au plus)
std::vector<Item> walk(const std::string &root, std::atomic<bool> &stop, std::atomic<int> &count);
// Reconnaissance, doublons compris (Bibliothèque, Kickstart installés, configurations) ; at : avancée
void classify(std::vector<Item> &items, std::atomic<bool> &stop, std::atomic<int> &at);
// Usages couverts par les Kickstart installés, et la ligne « ✓ Amiga 1200 (3.x) · ✗ CD32… »
std::set<std::string> libTags();
std::string kickLine(const std::set<std::string> &t);

// Ajout : Kickstart installés et archives extraites (à part) ; le rangement dans la Bibliothèque et les
// configurations se font ensuite dans la boucle principale (apply)
struct Report {
    std::vector<std::pair<std::string, std::string>> lib;   // chemin, type
    std::vector<std::string> uae;                           // configurations à enregistrer
    std::vector<std::pair<std::string, int>> added;
    std::vector<std::string> notes;
};
Report importAll(std::vector<Item> &items, std::atomic<bool> &stop, std::atomic<int> &at);
// Rangement et configurations ; renvoie le compte rendu
std::string apply(Settings &s, Report &r, bool stopped);

// Rubriques du bilan (type, titre) et rubrique d'un type (les jeux sur disque dur vont avec les jeux)
extern const std::vector<std::pair<std::string, std::string>> cats;
std::string cat(const std::string &kind);
}
