#include "app.h"
#include "platform.h"
#include "store.h"
#include <SDL.h>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string marker() { return paths::support() + ".emulation"; }
std::string crashFile() { return paths::support() + "plantage.txt"; }

void crashStarted() { std::ofstream(fs::u8path(marker()), std::ios::trunc) << platform::selfPid(); }
void crashEnded() {
    std::ifstream f(fs::u8path(marker()));
    long pid = 0;
    if (f >> pid && pid == platform::selfPid()) { f.close(); std::error_code ec; fs::remove(fs::u8path(marker()), ec); }
}

// Une marque restée alors que le processus n'existe plus, c'est un arrêt brutal
bool crashCheck(std::string &report) {
    long pid = 0;
    {
        std::ifstream f(fs::u8path(marker()));
        if (!(f >> pid)) return false;
    }
    if (platform::processAlive(pid)) return false;
    std::error_code ec;
    fs::remove(fs::u8path(marker()), ec);
    char date[64];
    time_t t = time(nullptr);
    strftime(date, sizeof date, "%d/%m/%Y %H:%M:%S", localtime(&t));
    report = std::string("Plantage de l'émulation le ") + date + " (amiga1200-pc " AMIGA1200_VERSION ", " + SDL_GetPlatform() + ")\n\n";
    std::deque<std::string> last;
    std::ifstream log(fs::u8path(paths::log()));
    std::string line;
    while (std::getline(log, line)) { last.push_back(line); if (last.size() > 150) last.pop_front(); }
    report += "— Fin du journal de la séance —\n";
    for (auto &l : last) report += l + "\n";
    std::ofstream(fs::u8path(crashFile()), std::ios::trunc) << report;
    return true;
}
