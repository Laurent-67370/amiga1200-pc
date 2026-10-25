// Les trois façons de démarrer l'application, et ce qu'elles partagent
#pragma once
#include <string>

int runHome();
int runEmu(const std::string &content, bool resume);
int runSelfTest(int frames);
// Démarrage du cœur dans ce processus : journal, dossiers, options de la version web, son
bool coreStart(const std::string &content);
// Fichier d'un emplacement de sauvegarde d'état (dans le dossier etats)
std::string stateFile(const std::string &game, int slot);

// Plantage de l'émulation : le processus note son numéro en démarrant et l'efface en quittant normalement
void crashStarted();
void crashEnded();
// Accueil : vrai si l'émulation s'est arrêtée brutalement ; report reçoit le rapport (fin du journal)
bool crashCheck(std::string &report);
std::string crashFile();
