// Bibliothèque : les fichiers déjà choisis (jeux WHDLoad, disquettes, disques durs, CD), retrouvés d'un clic. Seul le
// chemin est gardé : le fichier reste où il est, lu sur place (comme les versions Android et Mac).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct LibItem {
    std::string path, kind;
    double added = 0, used = 0;
    std::string name() const;
    std::string title() const;   // nom sans extension
    bool exists() const;
};

namespace library {
extern const char *WHD, *FLOPPY, *HDF, *ROM, *CD;
// Disque dur qui démarre WHDLoad tout seul (jeu installé sur un petit .hdf) : rangé avec les jeux
extern const char *WHDHDF;
// Type d'après le nom : un Kickstart n'est jamais rangé, il est installé comme ROM de la machine ; vide si inconnu
std::string kind(const std::string &path);
std::vector<LibItem> all();
// Ajouté (ou rafraîchi) ; le type vient de l'extension sauf s'il est donné ; faux si inconnu ou Kickstart
bool add(const std::string &path, const std::string &kind = "");
void touch(const std::string &path);
void remove(const std::string &path);
}

// Captures d'écran : l'image d'un jeu dans la Bibliothèque (captures/<jeu>.jpg), et une copie dans Images/Amiga1200
namespace captures {
std::string of(const std::string &name);
// Image de l'Amiga réduite à maxWidth (0 : taille réelle) et enregistrée en JPEG ou PNG selon l'extension
bool writeFrame(const std::string &file, int maxWidth);
bool save(const std::string &name, bool gallery, bool onlyIfMissing = false);
// Images (RGBA) lues pour l'affichage
bool load(const std::string &file, std::vector<uint8_t> &rgba, int &w, int &h);
}

// Archives .zip : un disque dur (.hdf) ou un jeu WHDLoad (.lha) zippé est extrait (dossier Disques) et rangé selon ce
// qu'il est ; le dossier d'un jeu WHDLoad zippé se lance comme une .lha ; une disquette zippée reste lue telle quelle
namespace zipimport {
struct Found { std::string path, kind; };
struct Result { std::vector<Found> found; std::string problem; bool whdArchive = false; };
Result inspect(const std::string &zip);
// Disque qui démarre WHDLoad de lui-même (S:startup-sequence lance un .slave) ; les gros disques ne sont pas lus
bool bootsWhdload(const std::string &path);
}
