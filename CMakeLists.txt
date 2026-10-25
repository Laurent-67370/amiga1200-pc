# Amiga 1200 pour Windows et Linux : l'application (SDL2, Dear ImGui) liée au cœur Amiberry compilé par
# native/build-core.sh (native/out/<cible>/libcore.a)
cmake_minimum_required(VERSION 3.16)
project(amiga1200 CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(AMIGA1200_VERSION "0.1" CACHE STRING "Version affichée")
set(CORE_TARGET "linux" CACHE STRING "Cible du cœur (linux ou windows)")
set(CORE_DIR ${CMAKE_SOURCE_DIR}/native/out/${CORE_TARGET})

find_package(SDL2 REQUIRED)

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
if(WIN32)
  # Icône et informations de l'exécutable (amiga1200.ico est fabriquée par scripts/package-windows.sh)
  set(WIN_RC ${CMAKE_BINARY_DIR}/amiga1200.rc)
  file(WRITE ${WIN_RC} "1 ICON \"${CMAKE_BINARY_DIR}/amiga1200.ico\"\n")
endif()
add_executable(amiga1200 ${WIN_RC}
  app/src/main.cpp app/src/home.cpp app/src/emu.cpp app/src/store.cpp app/src/config.cpp app/src/kickstarts.cpp
  app/src/platform.cpp app/src/ui.cpp app/src/crash.cpp app/src/library.cpp app/src/zip.cpp app/src/extras.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(amiga1200 PRIVATE app/src native ${IMGUI} ${IMGUI}/backends third_party/stb)
target_compile_definitions(amiga1200 PRIVATE AMIGA1200_VERSION="${AMIGA1200_VERSION}")

# SDL liée dans l'exécutable : rien à installer à côté (elle charge elle-même X11, Wayland, PulseAudio… au besoin)
if(TARGET SDL2::SDL2-static)
  set(SDL_LIBS SDL2::SDL2main SDL2::SDL2-static)
else()
  set(SDL_LIBS SDL2::SDL2main SDL2::SDL2)
endif()
# Le cœur est en C++ : bibliothèque statique liée tout entière (ses objets s'enregistrent eux-mêmes)
if(WIN32)
  target_link_libraries(amiga1200 PRIVATE -Wl,--whole-archive ${CORE_DIR}/libcore.a -Wl,--no-whole-archive
    ${SDL_LIBS} z winmm iconv ws2_32 iphlpapi gdi32 setupapi imm32 version ole32 oleaut32 uuid comdlg32 shell32 -static -static-libgcc -static-libstdc++ -lpthread)
  set_target_properties(amiga1200 PROPERTIES WIN32_EXECUTABLE ON)
else()
  target_link_libraries(amiga1200 PRIVATE -Wl,--whole-archive ${CORE_DIR}/libcore.a -Wl,--no-whole-archive
    ${SDL_LIBS} z dl m pthread util -static-libstdc++ -static-libgcc)
endif()
