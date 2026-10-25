// MIDI du port série de l'Amiga : ce que joue l'Amiga part vers un synthétiseur (USB, logiciel comme FluidSynth ou le
// synthé de Windows), un clavier MIDI peut jouer sur l'Amiga (Bars & Pipes, Music-X, OctaMED…). Windows : winmm ;
// Linux : séquenceur ALSA, chargé à l'exécution (libasound.so.2) s'il est là.
#pragma once
#include <string>
#include <vector>

namespace midi {
// Appareils qui reçoivent (sorties de l'Amiga) ou qui envoient (claviers)
std::vector<std::string> names(bool output);
// Démarre (sortie : appareil nommé, sinon le premier ; entrée : seulement si nommée) ; renvoie le compte rendu
std::string start(const std::string &out, const std::string &in);
void stop();
}
