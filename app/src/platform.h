// Ce qui diffère entre Windows et Linux : dossiers, processus, dialogues de fichiers, ouverture d'un fichier ou d'une adresse
#pragma once
#include <string>
#include <vector>
#include <map>

namespace platform {
// Dossier de l'exécutable (avec la barre finale)
std::string exeDir();
std::string exePath();
// Dossier Documents de l'utilisateur
std::string documentsDir();
// Dossier Images de l'utilisateur
std::string picturesDir();
// Relance de l'application avec ces arguments et ces variables d'environnement ; renvoie un identifiant de processus (0 : échec)
long spawnSelf(const std::vector<std::string> &args, const std::map<std::string, std::string> &env = {});
// Le processus tourne-t-il encore ?
bool processAlive(long pid);
long selfPid();
// Fichiers choisis par l'utilisateur (vide : annulé). filters : « Disquettes|*.adf;*.dms » …
std::vector<std::string> openFiles(const std::string &title, bool multiple, const std::vector<std::string> &filters = {});
std::string saveFile(const std::string &title, const std::string &name, const std::string &ext);
// Choix d'un dossier (scan de la Bibliothèque) ; vide si annulé
std::string openFolder(const std::string &title);
// Linux sans zenity ni kdialog : sélecteur de fichiers dessiné par l'application (fourni par l'interface)
// Extensions {"/"} : choix d'un dossier
using Browser = std::vector<std::string> (*)(const std::string &title, bool multiple, bool save, const std::string &name,
                                             const std::vector<std::string> &exts);
void setBrowser(Browser b);
// Ouvre un fichier, un dossier ou une adresse avec l'application du système
void open(const std::string &target);
// Raccourcis : menu Démarrer et bureau (Windows), menu des applications (Linux) ; renvoie le compte rendu
std::string installShortcuts();
// Mise en veille courte précise (Windows : minuterie à 1 ms)
void initTimers();
// Boîte de message du système (avant que la fenêtre de l'application existe)
void message(const std::string &title, const std::string &text);
}
