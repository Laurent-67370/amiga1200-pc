// Dear ImGui : police (accents et symboles), couleurs (orange de l'Amiga 1200, comme les autres versions)
#pragma once
#include <SDL.h>
#include <string>
#include "imgui.h"

namespace ui {
void init(SDL_Window *w, SDL_Renderer *r, float scale);
void shutdown();
void newFrame();
void render(SDL_Renderer *r);
// Échelle d'affichage (écrans haute densité)
float displayScale(SDL_Window *w);
// Échelle de l'écran principal, avant de créer une fenêtre (taille de départ des fenêtres)
float screenScale();
extern ImFont *bold;
extern ImFont *title;
// Bouton orange (action principale)
bool primaryButton(const char *label, const ImVec2 &size = ImVec2(0, 0));
// Texte d'aide, plus discret, qui revient à la ligne
void note(const std::string &t);
// Icône de la fenêtre (resources/icon.bmp)
void setIcon(SDL_Window *w);
// Sélecteur de fichiers de l'application, quand le système n'en propose pas (Linux sans zenity ni kdialog)
void installBrowser();
// Second contexte ImGui pour une fenêtre qui a sa propre boucle (sélecteur de fichiers) ; nestedEnd rend le premier
ImGuiContext *nestedBegin(SDL_Window *&w, SDL_Renderer *&r);
void nestedEnd(ImGuiContext *prev);
}
