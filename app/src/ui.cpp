#include "ui.h"
#include "store.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace ui {
ImFont *bold = nullptr;
ImFont *title = nullptr;

float displayScale(SDL_Window *w) {
    // Rapport entre pixels réels et taille de la fenêtre (écrans Retina ou mis à l'échelle), sinon DPI de l'écran
    int ww, wh, pw, ph;
    SDL_GetWindowSize(w, &ww, &wh);
    SDL_GetRendererOutputSize(SDL_GetRenderer(w), &pw, &ph);
    float s = ww > 0 ? (float)pw / ww : 1;
    if (s <= 1.01f) {
        float dpi = 0;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(w), &dpi, nullptr, nullptr) == 0 && dpi > 110) s = dpi / 96.0f;
    }
    return s < 1 ? 1 : s > 3 ? 3 : s;
}

void init(SDL_Window *w, SDL_Renderer *r, float scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // Accents français, ponctuation (« » … ’), flèches, formes (▶ ■), symboles (✓ ✕ ★ ⏏)
    static const ImWchar ranges[] = {0x0020, 0x017F, 0x2000, 0x206F, 0x2190, 0x21FF, 0x2300, 0x23FF, 0x2500, 0x27BF, 0};
    std::string font = paths::resources() + "fonts/DejaVuSans.ttf", fontBold = paths::resources() + "fonts/DejaVuSans-Bold.ttf";
    float px = 15.0f * scale;
    if (paths::exists(font)) {
        io.Fonts->AddFontFromFileTTF(font.c_str(), px, nullptr, ranges);
        bold = paths::exists(fontBold) ? io.Fonts->AddFontFromFileTTF(fontBold.c_str(), px * 1.07f, nullptr, ranges) : nullptr;
        title = io.Fonts->AddFontFromFileTTF((paths::exists(fontBold) ? fontBold : font).c_str(), px * 1.9f, nullptr, ranges);
    } else {
        ImFontConfig c; c.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&c);
    }
    if (!bold) bold = io.Fonts->Fonts[0];
    if (!title) title = io.Fonts->Fonts[0];

    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 10; st.ChildRounding = 12; st.FrameRounding = 7; st.PopupRounding = 8; st.GrabRounding = 6;
    st.TabRounding = 7; st.ScrollbarRounding = 8;
    st.FramePadding = ImVec2(10, 6); st.ItemSpacing = ImVec2(8, 8); st.WindowPadding = ImVec2(18, 16);
    st.ScaleAllSizes(scale);
    ImVec4 *c = st.Colors;
    ImVec4 orange(0.96f, 0.55f, 0.13f, 1), orangeHi(1.0f, 0.65f, 0.25f, 1), orangeLo(0.80f, 0.42f, 0.06f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.18f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.16f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.31f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.25f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.32f, 0.36f, 1);
    c[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.29f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.33f, 0.38f, 1);
    c[ImGuiCol_ButtonActive] = orangeLo;
    c[ImGuiCol_CheckMark] = orange;
    c[ImGuiCol_SliderGrab] = orange; c[ImGuiCol_SliderGrabActive] = orangeHi;
    c[ImGuiCol_Header] = ImVec4(0.96f, 0.55f, 0.13f, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.96f, 0.55f, 0.13f, 0.55f);
    c[ImGuiCol_HeaderActive] = orangeLo;
    c[ImGuiCol_Tab] = ImVec4(0.22f, 0.22f, 0.25f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(0.96f, 0.55f, 0.13f, 0.6f);
    c[ImGuiCol_TabSelected] = orangeLo;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.20f, 1);
    c[ImGuiCol_NavHighlight] = orange;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.96f, 0.55f, 0.13f, 0.4f);

    ImGui_ImplSDL2_InitForSDLRenderer(w, r);
    ImGui_ImplSDLRenderer2_Init(r);
}

void shutdown() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void newFrame() {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void render(SDL_Renderer *r) {
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), r);
}

bool primaryButton(const char *label, const ImVec2 &size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.50f, 0.10f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.62f, 0.22f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.40f, 0.05f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void note(const std::string &t) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.66f, 1));
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}
void setIcon(SDL_Window *w) {
    if (SDL_Surface *i = SDL_LoadBMP((paths::resources() + "icon.bmp").c_str())) { SDL_SetWindowIcon(w, i); SDL_FreeSurface(i); }
}
}  // namespace ui
