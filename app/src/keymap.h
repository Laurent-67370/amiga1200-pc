// Touches du PC (codes physiques SDL, donc indépendants de la disposition AZERTY ou QWERTY) → codes RETROK du clavier
// libretro, comme les autres versions. Touches absentes du PC : Inser = Help, Windows et Page préc./suiv. = touches
// Amiga gauche et droite, Début/Fin = parenthèses du pavé numérique.
#pragma once
#include <SDL.h>

inline unsigned retrok(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'a' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 282 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return 257 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN: return 13;
    case SDL_SCANCODE_ESCAPE: return 27;
    case SDL_SCANCODE_BACKSPACE: return 8;
    case SDL_SCANCODE_TAB: return 9;
    case SDL_SCANCODE_SPACE: return 32;
    case SDL_SCANCODE_MINUS: return 45;
    case SDL_SCANCODE_EQUALS: return 61;
    case SDL_SCANCODE_LEFTBRACKET: return 91;
    case SDL_SCANCODE_RIGHTBRACKET: return 93;
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return 92;
    case SDL_SCANCODE_SEMICOLON: return 59;
    case SDL_SCANCODE_APOSTROPHE: return 39;
    case SDL_SCANCODE_GRAVE: return 96;
    case SDL_SCANCODE_COMMA: return 44;
    case SDL_SCANCODE_PERIOD: return 46;
    case SDL_SCANCODE_SLASH: return 47;
    case SDL_SCANCODE_NONUSBACKSLASH: return 60;     // touche < > des claviers ISO
    case SDL_SCANCODE_CAPSLOCK: return 301;
    case SDL_SCANCODE_INSERT: return 281;            // Help
    case SDL_SCANCODE_HOME: return 295;              // ( du pavé
    case SDL_SCANCODE_END: return 296;               // ) du pavé
    case SDL_SCANCODE_PAGEUP: return 311;            // Amiga gauche
    case SDL_SCANCODE_PAGEDOWN: return 312;          // Amiga droite
    case SDL_SCANCODE_DELETE: return 127;
    case SDL_SCANCODE_UP: return 273;
    case SDL_SCANCODE_DOWN: return 274;
    case SDL_SCANCODE_RIGHT: return 275;
    case SDL_SCANCODE_LEFT: return 276;
    case SDL_SCANCODE_KP_0: return 256;
    case SDL_SCANCODE_KP_PERIOD: return 266;
    case SDL_SCANCODE_KP_DIVIDE: return 267;
    case SDL_SCANCODE_KP_MULTIPLY: return 268;
    case SDL_SCANCODE_KP_MINUS: return 269;
    case SDL_SCANCODE_KP_PLUS: return 270;
    case SDL_SCANCODE_KP_ENTER: return 271;
    case SDL_SCANCODE_KP_EQUALS: return 272;
    case SDL_SCANCODE_LSHIFT: return 304;
    case SDL_SCANCODE_RSHIFT: return 303;
    case SDL_SCANCODE_LCTRL: return 306;
    case SDL_SCANCODE_RCTRL: return 305;
    case SDL_SCANCODE_LALT: return 308;
    case SDL_SCANCODE_RALT: return 307;
    case SDL_SCANCODE_LGUI: return 311;              // Windows = Amiga gauche
    case SDL_SCANCODE_RGUI: return 312;
    case SDL_SCANCODE_APPLICATION: return 312;
    default: return 0;
    }
}
