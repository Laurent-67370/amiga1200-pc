#include "padkeys.h"
#include "store.h"
#include <sstream>

namespace padkeys {
const std::vector<std::string> &buttons() {
    static const std::vector<std::string> b = {"X", "Y", "L1", "R1", "L2", "R2", "L3", "R3", "Menu", "Options"};
    return b;
}
const std::vector<std::pair<int, std::string>> &keys() {
    static const std::vector<std::pair<int, std::string>> k = [] {
        std::vector<std::pair<int, std::string>> v = {
            {0, "Manette (par défaut)"}, {32, "Espace"}, {13, "Return"}, {27, "Échap"}, {127, "Del"}, {8, "Retour arrière"},
            {281, "Help"}, {9, "Tab"}, {304, "Maj"}, {306, "Ctrl"}, {308, "Alt"}, {311, "Amiga gauche"}, {312, "Amiga droite"},
            {273, "↑"}, {274, "↓"}, {276, "←"}, {275, "→"}, {268, "* (pavé)"},
        };
        for (int i = 1; i <= 10; i++) v.push_back({281 + i, "F" + std::to_string(i)});
        for (int i = 1; i <= 9; i++) v.push_back({48 + i, std::to_string(i)});
        v.push_back({48, "0"});
        for (char c : std::string("ynpqmsjc")) v.push_back({c, std::string(1, (char)toupper(c))});
        return v;
    }();
    return k;
}
// Réglage « touches:<jeu> » : « X=32;Y=13 »
std::map<std::string, int> load(const std::string &game) {
    std::map<std::string, int> m;
    std::istringstream is(Settings::shared().get("touches:" + game));
    std::string part;
    while (std::getline(is, part, ';')) {
        auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        int v = atoi(part.substr(eq + 1).c_str());
        if (v) m[part.substr(0, eq)] = v;
    }
    return m;
}
void save(const std::string &game, const std::map<std::string, int> &m) {
    std::string s;
    for (auto &[b, k] : m) if (k) s += (s.empty() ? "" : ";") + b + "=" + std::to_string(k);
    if (s.empty()) Settings::shared().remove("touches:" + game);
    else Settings::shared().set("touches:" + game, s);
}
}
