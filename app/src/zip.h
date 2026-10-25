// Archives .zip sans outil externe (Windows n'a pas unzip) : liste, vérification, extraction, et écriture pour
// « Changer de PC ». Méthodes « stockée » et « deflate » (zlib), formats zip et zip64 (fichiers de plus de 4 Go).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zip {
struct Entry {
    std::string name;
    uint64_t csize = 0, usize = 0, offset = 0;   // tailles compressée et réelle, position de l'en-tête local
    uint32_t crc = 0;
    uint16_t method = 0;
    bool dir() const { return !name.empty() && name.back() == '/'; }
};
// Liste des fichiers ; faux si ce n'est pas une archive lisible
bool list(const std::string &zip, std::vector<Entry> &out);
// Extraction d'un fichier (somme CRC vérifiée) ; faux si l'archive est abîmée
bool extract(const std::string &zip, const Entry &e, const std::string &dest);
// Lecture complète de l'archive, sommes CRC vérifiées : vrai si elle est entière et sans erreur
bool test(const std::string &zip);

// Écriture d'une archive
class Writer {
public:
    explicit Writer(const std::string &path);
    ~Writer();
    bool ok() const { return f != nullptr; }
    bool addFile(const std::string &src, const std::string &name);
    bool addData(const std::string &data, const std::string &name);
    bool close();
private:
    bool add(const std::string &name, const std::string *data, const std::string *src);
    void *f = nullptr;
    std::vector<Entry> entries;
};
}
