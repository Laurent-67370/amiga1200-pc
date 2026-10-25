// Sélecteur de fichiers dessiné par l'application (Linux sans zenity ni kdialog) : dossiers, fichiers filtrés par
// extension, choix multiple, nom du fichier à enregistrer. Il tourne dans sa propre boucle, sur la fenêtre en cours.
#include "platform.h"
#include "ui.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace {
std::string lastDir;

std::vector<std::string> browse(const std::string &title, bool multiple, bool save, const std::string &name,
                                const std::vector<std::string> &exts) {
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    ImGuiContext *prev = ui::nestedBegin(win, ren);
    if (!ren) return {};
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/";
    fs::path dir = !lastDir.empty() && fs::is_directory(lastDir) ? fs::path(lastDir) : fs::path(home);
    std::set<std::string> chosen;
    char fname[512] = {0};
    snprintf(fname, sizeof fname, "%s", name.c_str());
    // Extensions {"/"} : on choisit un dossier (celui qui est affiché)
    bool folder = exts.size() == 1 && exts[0] == "/";
    bool all = exts.empty(), done = false, ok = false;
    std::vector<std::pair<std::string, bool>> entries;   // nom, dossier ?
    auto load = [&] {
        entries.clear();
        std::error_code ec;
        for (auto &e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            std::string n = e.path().filename().u8string();
            if (n.empty() || n[0] == '.') continue;
            bool d = e.is_directory(ec);
            if (!d && folder) continue;
            if (!d && !all) {
                std::string x = e.path().extension().u8string();
                if (!x.empty()) x.erase(0, 1);
                std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                if (std::find(exts.begin(), exts.end(), x) == exts.end()) continue;
            }
            entries.push_back({n, d});
        }
        std::sort(entries.begin(), entries.end(), [](auto &a, auto &b) {
            if (a.second != b.second) return a.second;
            return strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
        });
        chosen.clear();
    };
    load();
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) { SDL_PushEvent(&e); done = true; }
        }
        ui::newFrame();
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##parcourir", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(ui::bold);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        if (ImGui::Button("↑ Dossier parent") && dir.has_parent_path() && dir != dir.root_path()) { dir = dir.parent_path(); load(); }
        ImGui::SameLine();
        if (ImGui::Button("Dossier personnel")) { dir = home; load(); }
        ImGui::SameLine();
        if (!exts.empty() && !folder && ImGui::Checkbox("Tous les fichiers", &all)) load();
        ImGui::TextDisabled("%s", dir.u8string().c_str());
        float bottom = ImGui::GetFrameHeightWithSpacing() * (save ? 2.2f : 1.2f);
        ImGui::BeginChild("##liste", ImVec2(0, -bottom), ImGuiChildFlags_Borders);
        fs::path enter;
        for (auto &[n, d] : entries) {
            std::string label = (d ? "▸ " : "   ") + n;
            bool sel = chosen.count(n) > 0;
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (d) enter = dir / fs::u8path(n);
                else {
                    if (save) snprintf(fname, sizeof fname, "%s", n.c_str());
                    else if (multiple && io.KeyCtrl) { if (sel) chosen.erase(n); else chosen.insert(n); }
                    else { chosen.clear(); chosen.insert(n); }
                    if (ImGui::IsMouseDoubleClicked(0) && !save) { ok = true; done = true; }
                }
            }
        }
        ImGui::EndChild();
        if (!enter.empty()) { dir = enter; load(); }
        if (save) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##nom", "Nom du fichier", fname, sizeof fname);
        }
        bool can = folder || (save ? fname[0] != 0 : !chosen.empty());
        ImGui::BeginDisabled(!can);
        if (ui::primaryButton(folder ? "Choisir ce dossier" : save ? "Enregistrer" : multiple && chosen.size() > 1 ? "Choisir ces fichiers" : "Choisir")) { ok = true; done = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) done = true;
        if (multiple && !save) { ImGui::SameLine(); ImGui::TextDisabled("Ctrl+clic : plusieurs fichiers"); }
        ImGui::End();
        SDL_SetRenderDrawColor(ren, 28, 28, 31, 255);
        SDL_RenderClear(ren);
        ui::render(ren);
        SDL_RenderPresent(ren);
    }
    ui::nestedEnd(prev);
    lastDir = dir.u8string();
    std::vector<std::string> r;
    if (!ok) return r;
    if (folder) r.push_back(dir.u8string());
    else if (save) r.push_back((dir / fs::u8path(fname)).u8string());
    else for (auto &n : chosen) r.push_back((dir / fs::u8path(n)).u8string());
    return r;
}
}  // namespace

namespace ui {
void installBrowser() { platform::setBrowser(browse); }
}
