// Dossiers de l'application et réglages (fichier reglages.ini, relu par le processus de l'émulation)
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace paths {
// ~/.local/share/Amiga1200 (Linux) ou %APPDATA%\Amiga1200 (Windows), avec la barre finale
std::string support();
std::string dir(const std::string &name);   // sous-dossier créé au besoin, avec la barre finale
std::string sys();
std::string save();
std::string data();
std::string log();
std::string config();
std::string shared();                        // Documents/Amiga1200/SHARED
std::string resources();                     // ressources fournies avec l'application
void installResources();
std::string name(const std::string &path);   // nom du fichier
std::string ext(const std::string &path);    // extension en minuscules, sans le point
bool exists(const std::string &path);
}

// Réglages : clé = valeur, dans reglages.ini. Les listes sont séparées par des tabulations.
class Settings {
public:
    static Settings &shared();
    void load();
    void flush();
    std::string get(const std::string &k, const std::string &def = "") const;
    void set(const std::string &k, const std::string &v);
    bool getBool(const std::string &k, bool def) const;
    void setBool(const std::string &k, bool v) { set(k, v ? "1" : "0"); }
    int getInt(const std::string &k, int def) const;
    void setInt(const std::string &k, int v) { set(k, std::to_string(v)); }
    double getDouble(const std::string &k, double def) const;
    void setDouble(const std::string &k, double v);
    std::vector<std::string> getList(const std::string &k) const;
    void setList(const std::string &k, const std::vector<std::string> &v);
    void remove(const std::string &k);
    std::vector<std::string> keysWithPrefix(const std::string &p) const;

    // Machine : A500, A600, A1200, A3000, A4000, CD32 ou CDTV
    std::string model() const { return get("model", "A1200"); }
    void setModel(const std::string &m) { set("model", m); saveProfile(); }
    // « Rapide » : 68040 sans cycles exacts, à toute vitesse, 64 Mo de Fast (sur PC, sans JIT)
    bool fastCpu() const { return getBool("jit", true); }
    void setFastCpu(bool v) { setBool("jit", v); saveProfile(); }
    bool rtg() const { return getBool("rtg", true); }
    void setRtg(bool v) { setBool("rtg", v); saveProfile(); }
    bool ntsc() const { return getBool("ntsc", false); }
    bool sharedVol() const { return getBool("shared", true); }
    bool floppySound() const { return getBool("floppySound", true); }
    // « user » : Kickstart installés, choisis selon la machine ; « aros » : ROM libre fournie
    std::string rom() const { return get("rom", "aros"); }
    std::string floppy(int n) const { return get("df" + std::to_string(n)); }
    void setFloppy(int n, const std::string &p) { set("df" + std::to_string(n), p); }
    std::vector<std::string> hds() const { return getList("hds"); }
    std::string cd() const { return get("cd"); }
    int chip() const { return getInt("chip", 0); }
    int slow() const { return getInt("slow", -1); }
    int fast() const { return getInt("fast", 0); }
    std::string accel() const { return get("accel", "none"); }
    bool midi() const { return getBool("midi", false); }
    std::string midiIn() const { return get("midiIn"); }
    int filter() const { return getInt("filter", 0); }
    bool integer() const { return getBool("integer", false); }
    bool fill() const { return getBool("fill", false); }
    int quitKey() const { return getInt("quitKey", 291); }

    // Disque qui démarre : le CD des consoles, sinon DF0:, sinon le premier disque dur
    std::string bootKey() const;
    void saveProfile();
    bool applyProfile();
    std::string signature() const;
    static std::string resumeFile();

    static bool bigBox(const std::string &m) { return m == "A1200" || m == "A3000" || m == "A4000"; }
    static bool cdConsole(const std::string &m) { return m == "CD32" || m == "CDTV"; }
    static bool isWhd(const std::string &path);
private:
    std::map<std::string, std::string> kv;
    std::string file;
};
