# amiga1200-pc

Application native de l'émulateur Amiga [amiga1200](https://github.com/laurent-67370/amiga1200) pour **Windows** et
**Linux** (PC x64). Elle utilise le même cœur **Amiberry (WinUAE 6)** que la version web,
[amiga1200-android](https://github.com/laurent-67370/amiga1200-android) et
[amiga1200-mac](https://github.com/laurent-67370/amiga1200-mac), avec une seule base de code (C++, SDL2, Dear ImGui)
pour les deux systèmes.

> **État : en essai.** Les fonctions des versions Android et Mac y sont (Bibliothèque, Kickstart multiples, sauvegardes
> avec images, reprise, filtres et cadrage par jeu, MIDI, touches de manette, assistant…) ; l'application attend ses
> premiers essais sur de vrais PC.
>
> Sur PC x64, le cœur libretro d'Amiberry n'a pas de JIT : le 68040 est interprété. Le mode « Processeur rapide » le
> fait tourner à toute la vitesse du PC, largement assez pour AGS et le Workbench sur une machine récente.

## La famille Amiga 1200

Quatre versions du même émulateur, toutes sur le **même cœur Amiberry 8.3 (WinUAE 6)**, avec les mêmes bases :
Bibliothèque, jeux WHDLoad en un geste, gros disques durs (AGS), CD32 et CDTV, carte graphique RTG, sauvegardes d'état,
configurations exportables, MIDI.

| Version | Dépôt | Pour | Particularités | Obtenir |
|---|---|---|---|---|
| **Web**, l'origine du projet | [amiga1200](https://github.com/laurent-67370/amiga1200) | Navigateur : ordinateur, Android, iPhone, iPad | Rien à installer, hors ligne (application PWA), deux moteurs (WinUAE 6 et PUAE) | [Ouvrir la page](https://laurent-67370.github.io/amiga1200/) |
| **Android** | [amiga1200-android](https://github.com/laurent-67370/amiga1200-android) | Téléphones et tablettes 64 bits (Android 9 et plus) | **JIT**, manette et clavier tactiles, raccourcis sur l'écran d'accueil | [APK (Releases → dev)](https://github.com/laurent-67370/amiga1200-android/releases/tag/dev) |
| **Mac** | [amiga1200-mac](https://github.com/laurent-67370/amiga1200-mac) | Mac Apple Silicon (macOS 13 et plus) | **JIT**, application macOS (menus, CoreMIDI, manettes Game Controller) | [.dmg (Releases → dev)](https://github.com/laurent-67370/amiga1200-mac/releases/tag/dev) |
| **Windows et Linux** (ce dépôt) | [amiga1200-pc](https://github.com/laurent-67370/amiga1200-pc) | PC x64 : Windows 10/11, Linux (AppImage) | Une seule base pour les deux systèmes, .zip portable et AppImage autonome (sans JIT : 68040 interprété) | [.zip et AppImage (Releases → dev)](https://github.com/laurent-67370/amiga1200-pc/releases/tag/dev) |

Passer de l'une à l'autre : une configuration exportée en **.uae** (machine, mémoire, disques) se relit dans les
quatre versions, et aussi dans WinUAE et Amiberry. Les applications Android, Mac et PC ont un bouton **Web** en haut
de l'accueil qui mène à la version web. Les Kickstart et les disques restent à ajouter dans chaque version : ils ne
sont jamais fournis ni envoyés nulle part.

## Installer

Ouvrez la page **[Releases → dev](https://github.com/laurent-67370/amiga1200-pc/releases/tag/dev)**.

- **Windows 10 ou 11** : téléchargez `amiga1200-windows-x64.zip`, décompressez-le, lancez `Amiga1200\Amiga1200.exe`.
  Windows peut avertir (application non signée) : « Informations complémentaires » puis « Exécuter quand même ».
- **Linux** (Ubuntu 22.04 ou plus récent, Debian 12, Fedora…) : téléchargez `amiga1200-linux-x86_64.AppImage`, rendez-le
  exécutable (`chmod +x amiga1200-linux-x86_64.AppImage`, ou Propriétés → Autoriser l'exécution) et lancez-le. Les
  fenêtres de choix de fichiers sont celles du bureau (`zenity` ou `kdialog`), sinon celle de l'application.

Carte **Raccourcis** de l'accueil : « Amiga 1200 » dans le menu Démarrer et sur le bureau (Windows), ou dans le menu des
applications (Linux).

## Ce que fait l'application

- **Assistant de premier lancement** (« ★ Assistant ») : ce que l'on veut faire (WHDLoad, disquettes, AGS, CD,
  découverte avec AROS), les Kickstart qu'il faut, les fichiers à ranger, puis le bouton qui lance ; « ? Aide » résume
  tout.
- **Écran d'accueil** : préréglages (AGS / Workbench, A1200 d'origine, Amiga 500, CD32), machine (**A500, A600, A1200,
  A3000, A4000, CD32, CDTV**), processeur rapide, carte graphique RTG, volume SHARED:, NTSC, bruit des lecteurs,
  mémoire (Chip, Slow, Fast) et carte 68030 ou 68040, **quatre lecteurs** DF0: à DF3:, **disquette vierge** formatée
  (pour les sauvegardes des jeux), CD, jusqu'à **8 disques durs** (HDF,
  AGS compris). Fichiers glissés sur la fenêtre : Kickstart installé, disque dur branché, disquette mise dans DF0:.
- **Plusieurs Kickstart**, reconnus à leur contenu (somme CRC et version, 3.1.4 et 3.2 compris) : chacun sert
  automatiquement à la machine qui lui convient. Sinon, la ROM libre **AROS** fournie.
- **Bibliothèque** (onglets Jeux, Disquettes, Disques durs, CD) : les fichiers déjà choisis, retrouvés d'un clic avec
  l'image du jeu ; « ▶ Jouer », DF0:/DF1:, « Brancher » un disque dur, « ▶ CD32 » / « ▶ CDTV ». Ajout par « Ajouter des
  fichiers… » ou en glissant les fichiers sur la fenêtre. Les **.zip sont analysés** : un disque dur ou un jeu WHDLoad
  zippé est extrait (dossier Disques), une archive abîmée est signalée, une disquette zippée reste lue telle quelle.
- **Jeux WHDLoad** : « ▶ Jouer » pour une archive `.lha`, `.lzx` ou le dossier d'un jeu zippé, et pour un **jeu
  installé sur un .hdf** qui démarre WHDLoad tout seul (A1200, Kickstart 3.x, 8 Mo de Fast ; vos Kickstart lui sont
  donnés dans `WHDCommon:Kickstarts`). Il faut un vrai Kickstart.
- **Sauvegardes d'état** : quatre emplacements par jeu avec leur image et leur date (Ctrl+E) ; Ctrl+S / Ctrl+L pour le
  premier.
- **Captures d'écran** (Ctrl+K) : l'image du jeu dans la Bibliothèque, une copie dans Images/Amiga1200 ; un jeu sans
  image en reçoit une quand on quitte.
- **Configurations** enregistrées sous un nom, **export et import .uae** (WinUAE, Amiberry, autres versions) ; machine,
  mémoire et processeur retenus pour chaque disque de démarrage.
- **Version web** (en haut de l'accueil) : lien vers la [version web](https://laurent-67370.github.io/amiga1200/)
  d'origine, et rappel du passage de l'une à l'autre par les configurations .uae.
- **Changer de PC** : *Tout exporter…* range dans un seul `.zip` réglages, Bibliothèque, configurations, Kickstart,
  sauvegardes d'état et des jeux, disques extraits et captures ; *Importer…* sur l'autre PC (Windows ou Linux).
- **Reprendre où j'en étais** : en fermant l'émulation, l'état est gardé ; l'accueil propose « Reprendre ».
- **Fenêtre de l'émulation** (son propre processus) : un clic capture la souris, **F12** la libère et affiche le menu
  (Amiga, Disquettes, Image). Raccourcis, souris libre : Ctrl+P pause, Ctrl+S / Ctrl+L sauvegarde et reprise rapides,
  Ctrl+E sauvegardes d'état, Ctrl+O disquette, Ctrl+N disquette vierge, Ctrl+I filtre suivant, Ctrl+J cadrage,
  Ctrl+K capture, Ctrl+T turbo, Ctrl+Q quitter le jeu WHDLoad (F10, * ou Échap au choix), Ctrl+R réinitialiser,
  F11 plein écran. Les messages s'affichent dans une bulle sur l'image.
- **Clavier** : touches physiques (AZERTY ou QWERTY indifféremment) ; Windows ou Page préc./suiv. = touches Amiga,
  Inser = Help, Début/Fin = parenthèses du pavé numérique. Souris capturée, le clavier l'est aussi : la touche Windows
  n'ouvre pas le menu Démarrer.
- **Écrans haute résolution** sous Windows : l'application dessine à la taille réelle (police nette), chaque écran à
  son échelle.
- **Manettes** (Xbox, PlayStation, Switch… par SDL) : la première joue sur le port joystick, la deuxième sur le port
  souris ; **touches de la manette par jeu** (X, Y, L1, R1, L2, R2, Menu… changés en touches du clavier Amiga).
- **Image** : lisse, nette ou écran cathodique (lignes de balayage), pixels réguliers, 4:3 ou toute la fenêtre, et
  **cadrage retenu pour chaque jeu** (zoom, position verticale : Ctrl+J pendant la partie).
- **MIDI** : l'Amiga joue sur un synthétiseur (USB, synthé de Windows, FluidSynth ou TiMidity sous Linux), un clavier
  MIDI joue sur l'Amiga (Bars & Pipes, Music-X, OctaMED…). Windows : winmm ; Linux : séquenceur ALSA.
- **Son** par SDL, l'émulation cadencée par le son (comme les autres versions) ; vitesse affichée dans le titre.
- **Rapport de plantage** : si l'émulation s'arrête brutalement, l'accueil prépare un rapport (fin du journal et pile
  d'appels du plantage ; sous Windows, un minidump en plus) à copier, ouvrir ou signaler sur GitHub.

Dossiers : réglages, ROM et sauvegardes dans `~/.local/share/Amiga1200` (Linux) ou `%APPDATA%\Amiga1200` (Windows) ;
volume SHARED: dans `Documents/Amiga1200/SHARED`.

## Comment c'est construit

| Élément | Origine |
|---|---|
| Cœur Amiberry 8.3 (WinUAE 6) | `BlitterStudio/amiberry`, même version que les autres versions, Makefile libretro (`platform=unix`, ou `platform=win` avec MinGW-w64) |
| Correctifs du cœur | `native/patches/` : ceux des versions Android et Mac |
| Frontal libretro | `amiga1200/wasm/frontend.c` (sous-module **amiga1200**), compilé tel quel ; `native/compat/` remplace les fonctions d'Emscripten qu'il utilise |
| Pont | `native/bridge.c` : fil de l'émulation cadencé par le son, journal du cœur, fonctions appelées par l'application |
| Application | `app/src` : C++17, SDL2 (fenêtres, son, manettes), Dear ImGui (sous-module `third_party/imgui`), stb (images, sous-module `third_party/stb`), archives .zip lues et écrites avec zlib |

Le cœur apporte ses propres fonctions SDL factices : `native/build-core.sh` les renomme (`AMB_SDL_*`) pour qu'elles ne
se mêlent pas à la vraie SDL de l'application.

La compilation est faite par GitHub Actions (`.github/workflows/build.yml`) : Linux sur Ubuntu 22.04 (AppImage, avec une
SDL2 compilée pour charger X11, Wayland, PulseAudio… seulement s'ils sont là), Windows en compilation croisée MinGW-w64
(exécutable autonome), puis un **essai réel** sur Linux et sur un vrai Windows (`--selftest` : AROS démarre sur un
A1200, sans fenêtre ; l'essai vérifie que des images sont calculées et qu'elles ne sont pas noires). Depuis `main`, les
deux applications sont publiées dans la release `dev`.

Pour compiler sur Linux (Ubuntu 22.04 ou plus récent) :

```sh
git clone --recurse-submodules https://github.com/laurent-67370/amiga1200-pc.git
cd amiga1200-pc
native/build-core.sh linux && scripts/package-linux.sh          # build/amiga1200-linux-x86_64.AppImage
native/build-core.sh windows && SDL2_MINGW=… scripts/package-windows.sh   # build/amiga1200-windows-x64.zip
```

Paquets utiles : `build-essential cmake zlib1g-dev imagemagick fonts-dejavu-core zip squashfs-tools`, les en-têtes de
développement de X11, Wayland et PulseAudio pour SDL2 (voir la CI), et pour Windows `mingw-w64 libz-mingw-w64-dev
win-iconv-mingw-w64-dev` avec le paquet `SDL2-devel-*-mingw` de SDL.

## Licences

GPL v3, comme Amiberry et la version web. AROS est sous licence publique AROS. La police DejaVu est libre (licence
Bitstream Vera). Les Kickstart d'Amiga ne sont pas fournis : utilisez les vôtres.
