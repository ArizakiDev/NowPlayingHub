# Now Playing CYD

Un petit écran de bureau qui affiche **ce que tu écoutes** (Cider / Apple Music, iTunes, Spotify, navigateur…) et se transforme en **horloge + météo + actualités** quand rien ne joue.

Matériel : **ESP32-2432S028R** (« Cheap Yellow Display », ILI9341 320×240, tactile résistif).

- Pochette, titre défilant, artiste, barre de progression, boutons tactiles précédent / lecture / suivant
- Écran de veille : horloge, date, météo (Open-Meteo), UV, lever/coucher du soleil, défilement d'actus RSS
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
- **Paroles synchronisées** : une ligne à la fois en bas de l'écran, récupérée sur [LRCLIB](https://lrclib.net). Le titre, l'artiste, l'album et la durée du morceau sont envoyés à ce service pour la recherche ; décoche l'option si tu ne le veux pas.
- La **météo** vient d'[Open-Meteo](https://open-meteo.com) et la recherche de ville de son API de géocodage (coordonnées de la ville envoyées, aucune clé nécessaire).

La page `http://nowplaying.local` affiche l'état de la carte (version, Wi-Fi, mémoire, lecture) et permet de la redémarrer.

## Mises à jour du firmware en Wi-Fi

Dans le panel, carte **Firmware de la carte** :

- **Depuis GitHub** : renseigne `utilisateur/depot` dans *Connexion*. L'appli lit la dernière *release* et télécharge le premier fichier `.bin`.
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
| `POST /api/nowplaying` | état de lecture (titre, artiste, position…) |
| `POST /api/cover?id=` / `/api/icon?id=` | pochette / logo (JPEG en base64) |
| `GET/POST /api/config` | luminosité, couleur, défilement, mode nuit, veille |
| `POST /api/reboot`, `/api/wifireset` | redémarrer / oublier le Wi-Fi (`?token=`) |
| `GET/POST /api/idleconfig` | lieu, fuseau horaire et flux RSS de l'écran de veille |
| `POST /api/idle` | météo et actus envoyées par le PC |
| `GET /api/status` | version, IP, mémoire |
| `POST /api/update?token=` | mise à jour OTA |

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
