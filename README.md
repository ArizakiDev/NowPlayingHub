# Now Playing CYD

Un petit écran de bureau qui affiche **ce que tu écoutes** (Cider / Apple Music, iTunes, Spotify, navigateur…) et se transforme en **horloge + météo + actualités** quand rien ne joue.

Matériel : **ESP32-2432S028R** (« Cheap Yellow Display », ILI9341 320×240, tactile résistif).

- Pochette, titre défilant, artiste, barre de progression, boutons tactiles précédent / lecture / suivant
- **Tactile** : toucher la barre de progression pour se déplacer dans le morceau, les paroles pour les voir en grand, le bloc titre/artiste pour le **visualiseur de spectre**, la pochette pour la voir en **plein écran 240×240** (balayage gauche/droite = suivant/précédent)
- **Luminosité automatique** (capteur de lumière de la carte) et **LED RGB** (couleur d'accent, pulsation ou rythme des basses)
- **Cache de pochettes sur carte SD** (optionnel)
- **Page de statut** sur `http://localhost:8123/status` (carte, lecture, capteurs, SD, spectre, stats du PC)
- Écran de veille : horloge, date, météo (Open-Meteo), UV, lever/coucher du soleil, défilement d'actus RSS, et une ligne très discrète avec les stats du PC (toucher pour la page « Mon PC »)
- Mode nuit et mise en veille de la luminosité, réglables
- **Panel web** pour tout configurer depuis le navigateur
- **Mises à jour du firmware en Wi-Fi** (GitHub ou fichier `.bin`)

## Comment ça marche

```
 Lecteur média (Windows)  ──►  NowPlayingHub.exe  ──HTTP──►  ESP32 (écran)
                                  │  panel web http://localhost:8123
                                  └─ météo, RSS, pochettes, mises à jour OTA
```

L'appli PC lit la session média Windows (GSMTC), prépare pochette et logo, et pousse le tout à l'ESP32.

## Matériel et broches

Rien à câbler : tout est déjà sur la carte (CYD).

| Fonction | Broches |
|---|---|
| Capteur de lumière (LDR) | GPIO 34 |
| LED RGB (allumée à l'état bas) | GPIO 4 (R), 16 (G), 17 (B) |
| Lecteur microSD | CS 5, SCK 18, MISO 19, MOSI 23 |
| Spectre audio (PC → carte) | UDP 8124 |

> **Carte SD et tactile** : sur le CYD, le lecteur SD et le tactile partagent le même périphérique SPI (VSPI) avec des broches différentes. Le firmware ré-attache les broches avant chaque accès à l'un ou à l'autre. Si tu constates des soucis de toucher avec le cache SD activé, désactive-le dans le panel.

## Installation

### 1. Firmware

Prérequis : [PlatformIO](https://platformio.org/).

```bash
cp firmware/include/config.example.h firmware/include/config.h
# édite config.h : OTA_TOKEN (à changer !) et, si tu veux, WIFI_SSID / WIFI_PASS
cd firmware
pio run -t upload
```

> `config.h` contient tes secrets : il est dans le `.gitignore`, ne le commite jamais.

**Wi-Fi sans recompiler** : si tu laisses `WIFI_SSID` vide, la carte ouvre au premier démarrage un réseau **`NowPlaying-Setup`**. Connecte-toi avec ton téléphone, une page s'ouvre (sinon `192.168.4.1`), choisis ton réseau 2,4 GHz et entre le mot de passe : la carte redémarre et s'y connecte. Le même mode s'active si elle n'arrive pas à se connecter pendant 90 s après un démarrage. Pour oublier le Wi-Fi : bouton dans le panel, ou page `http://nowplaying.local`.

Si les couleurs sont inversées, regarde `firmware/include/User_Setup.h` (`TFT_INVERSION_ON`, `TFT_BGR`/`TFT_RGB`).

### 2. Appli PC (Windows)

Prérequis : SDK .NET 8.

```bat
pc-app\build.bat
```

Ça produit `pc-app\dist\NowPlayingHub.exe`. Au lancement, le panel s'ouvre sur `http://localhost:8123`. Dans **Connexion**, renseigne l'adresse de la carte (`nowplaying.local` ou son IP) et le même `OTA_TOKEN` que dans `config.h`.

### 3. Réglages

Tout se règle depuis le panel : ville (recherche intégrée), fuseau horaire, flux RSS, mode nuit, veille, couleur d'accent, luminosité…

- **Couleur d'accent automatique** : calculée à partir de la pochette.
- **Paroles synchronisées** : affichées en bas de l'écran sur 1 ou 2 lignes. Plusieurs sources sont essayées dans l'ordre, la première qui trouve gagne : [LRCLIB](https://lrclib.net), puis Musixmatch (optionnel, il faut renseigner ton jeton utilisateur dans le panel ; API non officielle), puis NetEase Cloud Music. Le titre, l'artiste, l'album et la durée du morceau sont envoyés aux sources activées pour la recherche ; décoche l'option (ou chaque source) si tu ne le veux pas.
- **Luminosité automatique** : le capteur de lumière se **calibre** depuis le panel (« Il fait sombre » / « Il fait clair »), ce qui marche quel que soit le sens du capteur. La luminosité varie entre 10 % et ton réglage de luminosité ; le mode nuit et la mise en veille restent prioritaires.
- **LED RGB** : éteinte, couleur d'accent, pulsation, ou « suit les basses » (utilise le spectre audio).
- **Visualiseur de spectre** : l'appli PC capte le son de sortie de Windows (WASAPI loopback) **uniquement quand la carte affiche le visualiseur** (ou la LED en mode basses), l'analyse (FFT) et envoie 24 bandes en UDP à la carte. Aucun son n'est envoyé ni enregistré, seulement des niveaux. Désactivable dans le panel.
- **Pochette plein écran** : une version 240×240 est envoyée **à la demande** quand tu ouvres le plein écran (la petite pochette s'affiche en attendant).
- **Cache SD** : les pochettes (petite et grande) sont gardées sur la carte microSD, par piste ; au-delà du nombre de pistes choisi, les plus anciennes sont supprimées. Bouton « Vider le cache » dans le panel.
- **Décalage des paroles** : curseur de ±2 s dans le panel si les paroles te semblent en avance ou en retard.
- La **météo** vient d'[Open-Meteo](https://open-meteo.com) et la recherche de ville de son API de géocodage (coordonnées de la ville envoyées, aucune clé nécessaire).

La page `http://nowplaying.local` affiche l'état de la carte (version, Wi-Fi, mémoire, lecture) et permet de la redémarrer. La **page de statut** `http://localhost:8123/status` (bouton « Statut » du panel) montre en plus les capteurs, la LED, la carte SD, le spectre, les stats du PC et des courbes (mémoire, Wi-Fi, lumière).

## Mises à jour du firmware en Wi-Fi

Dans le panel, carte **Firmware de la carte** :

- **Depuis GitHub** : renseigne `ArizakiDev/NowPlayingHub` dans *Connexion*. L'appli lit la dernière *release* et télécharge le premier fichier `.bin`.
- **Depuis un fichier** : choisis un `firmware.bin` (`firmware/.pio/build/cyd/firmware.bin`).

### Publier une release

Le `firmware.bin` embarque ton `OTA_TOKEN` (compilé via `config.h`, jamais commité), et ton Wi-Fi si tu l'y as mis. Il se compile donc **en local** et s'ajoute à la main à la release : la CI ne peut pas le faire sans exposer tes secrets. Un firmware compilé avec `WIFI_SSID` vide est réutilisable par tout le monde, le Wi-Fi se configurant sur la carte.

1. Monte `FW_VERSION` dans `firmware/include/settings.h`.
2. `pio run -d firmware`
3. `git tag v1.4.1 && git push --tags` → la CI compile et attache `NowPlayingHub-win-x64.zip`.
4. Sur la page de la release GitHub, ajoute `firmware/.pio/build/cyd/firmware.bin`.

Le **tag doit correspondre à `FW_VERSION`** (avec ou sans le `v`), sinon l'appli compare mal les versions.

## API de la carte (HTTP)

| Route | Rôle |
|---|---|
| `POST /api/nowplaying` | état de lecture (titre, artiste, position, paroles…). La réponse peut contenir `needCover`, `needBig`, `viz` et `cmd` (`prev`, `toggle`, `next`, `seek:<secondes>`) |
| `POST /api/cover?id=` / `/api/icon?id=` | pochette / logo (JPEG en base64) |
| `GET/POST /api/config` | luminosité, couleur, défilement, mode nuit, veille |
| `POST /api/reboot`, `/api/wifireset` | redémarrer / oublier le Wi-Fi (`?token=`) |
| `GET/POST /api/idleconfig` | lieu, fuseau horaire et flux RSS de l'écran de veille |
| `POST /api/idle` | météo et actus envoyées par le PC |
| `POST /api/coverbig?id=` | pochette plein écran 240×240 (JPEG en base64, sur demande) |
| `POST /api/pcstats` | stats du PC pour l'écran de veille |
| `POST /api/sdclear` | vide le cache de pochettes de la carte SD |
| `GET /api/status` | version, IP, mémoire, Wi-Fi, capteur de lumière, SD, spectre… |
| `POST /api/update?token=` | mise à jour OTA |

## À savoir

Les fonctions ajoutées en 1.6 (capteur de lumière, LED, SD, spectre, plein écran HD) dépendent du matériel réel : si quelque chose se comporte bizarrement, la page de statut est le premier endroit où regarder. Le déplacement dans le morceau dépend du lecteur : certains (navigateurs, parfois Spotify) refusent le changement de position.

## Structure

```
firmware/   code ESP32 (PlatformIO, TFT_eSPI)
pc-app/     NowPlayingHub : appli C# / .NET 8 + panel web
.github/    CI (compilation) et release
```

## Licence

MIT, voir [LICENSE](LICENSE).

## Captures d'écran

Ajoute tes photos dans `docs/` (ex. `docs/ecran-lecture.jpg`, `docs/ecran-veille.jpg`, `docs/panel.png`) puis référence-les ici avec `![Écran de lecture](docs/ecran-lecture.jpg)`.
