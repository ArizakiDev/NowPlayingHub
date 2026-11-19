# Now Playing Hub — appli PC v2.2 + firmware 1.9.1

## Correctif : l'écran de la carte clignotait en mode nuit

**Cause** (une boucle entre l'appli et la carte) :
1. À chaque (re)connexion, l'appli renvoie ses réglages, dont `/api/idleconfig`.
2. Le firmware relançait alors **toujours** une récupération météo/actus bloquante (3 à 7 s) : son serveur HTTP ne répondait plus.
3. L'appli attendait 3 s, concluait « carte hors ligne », se reconnectait et renvoyait les réglages. Retour à l'étape 1.
4. Chaque `POST /api/config` appelait en plus `applyBrightness()`, qui remettait la luminosité **normale** (jusqu'à 100 %) pendant tout le redessin complet de l'écran, avant que le mode nuit ne la rebaisse. En journée on ne le voit pas ; en mode nuit, c'est un flash à chaque tour : l'effet « guirlande ».

**Corrections**
- Firmware 1.9.1 : `applyBrightness()` respecte le mode nuit (plus de flash) ; `/api/config` ne redessine l'écran que si la couleur, la vitesse de défilement ou l'option « nom de l'appli » ont réellement changé ; `/api/idleconfig` ne relance la météo/actus que si le lieu, le flux RSS ou le fuseau a changé.
- Appli 2.2 : un échec isolé de requête ne fait plus passer la carte hors ligne (il en faut 3 d'affilée), donc plus de renvoi en cascade des réglages.

> Mets les deux à jour. Avec seulement l'appli, le clignotement devrait déjà beaucoup diminuer ; avec seulement le firmware, la boucle disparaît aussi. Les deux ensemble : c'est la bonne combinaison.

## Nouvelle interface (Windows 11 / Fluent)

- **Mica** derrière la fenêtre (Windows 11 ; sur Windows 10, fond plein équivalent) et barre de titre intégrée.
- **Thème clair / sombre qui suit Windows**, changement à chaud.
- Navigation façon *NavigationView* : icônes, survol, indicateur d'accent, état de la carte en bas.
- Zone de contenu arrondie comme les Paramètres de Windows 11, pages qui entrent en glissant vers le haut.
- Cartes arrondies, interrupteurs, curseurs, champs (trait d'accent au focus), boutons et barres de défilement refaits aux dimensions Fluent. L'accent reste la couleur affichée par la carte.
- Titres de pages et de cartes en casse normale (plus de MAJUSCULES).
- Rien n'a pu être compilé ici (pas de SDK .NET) : XAML vérifié bien formé, mais une petite erreur reste possible au premier `build.bat`.

---

# Now Playing Hub — appli PC (v2.1) + firmware 1.9.0

Cette version ajoute la **page de statut** (disponibilité sur 30 jours façon Statuspage, détail du processeur de l'ESP32…) et **retire les stats PC de l'ESP32**. Ce qui a changé depuis la v1 (appli native, réglages en direct, bugs corrigés) est conservé plus bas.

## Installer

1. Supprime l'ancien dossier `pc-app/NowPlayingHub`, puis décompresse ce zip à la racine du projet (il contient `pc-app/` **et** `firmware/`).
2. **Reflashe la carte** avec le firmware 1.9.0 (PlatformIO → Upload, ou la mise à jour Wi-Fi de l'appli), puis lance `pc-app\build.bat`.
3. **Mets les deux à jour ensemble** : l'ancienne appli v2.0 enverrait encore les stats PC à un firmware 1.9 qui ne les connaît plus (erreurs 404 dans le journal). L'appli 2.1 fonctionne en revanche avec un firmware 1.8 (le détail du processeur est alors partiel).

## Nouveautés v2.1

### Page de statut (appli PC)
- **Page web** `http://localhost:8123/` (même rendu que la capture : bandeau « Tous les systèmes sont opérationnels », 7 composants avec 30 barres journalières et le pourcentage de disponibilité, incidents récents). Clair ou sombre selon le téléphone/PC, actualisation toutes les 10 s.
- **Accessible depuis le téléphone** : Système → *Accessible depuis le réseau local*, puis ouvre l'adresse `http://<IP du PC>:8123/` (Windows peut demander d'autoriser le pare-feu). Par défaut, uniquement ce PC. Lecture seule, sans jeton, sans nom de Wi-Fi.
- **Page native « statut »** dans l'appli : le même historique + le détail de la carte.
- **Composants suivis** : connexion à la carte, signal Wi-Fi (dégradé sous -75 dBm), mémoire (dégradée sous 30 Ko libres), température de la puce (dégradée au-dessus de 85 °C), carte SD, météo, actualités.
- **Honnêteté des mesures** : mesure toutes les 10 s, état confirmé après 2 mesures identiques (les à-coups de moins de 20 s sont ignorés). Quand l'appli est fermée, le jour reste **gris** (aucune mesure), jamais compté comme une panne. Disponibilité = temps sans panne, « dégradé » compté pour moitié. Historique dans `%AppData%\NowPlayingHub\status-history.json` (35 jours).

### Détail de l'ESP32 (appli PC, page statut)
Processeur (puce, révision, cœurs, fréquence, température), charge (durée moyenne et pire d'un tour de boucle, tours lents, pile libre), mémoire (libre, minimum, plus gros bloc, fragmentation), flash (capacité, firmware, place libre pour les mises à jour), réseau (SSID, canal, signal), système (raison du dernier démarrage, durée allumée, SDK), capteurs et SD. Quatre courbes sur 15 min : mémoire libre, température, Wi-Fi, tour de boucle.

### Firmware 1.9.0 (carte)
- **Retrait des stats PC** : struct, route `POST /api/pcstats`, ligne discrète de l'écran de veille, page « Mon PC » et zone tactile associée. L'écran de veille garde exactement son rendu (la zone en bas à gauche est simplement effacée comme avant).
- **`GET /api/status` enrichi** (toutes les anciennes clés sont conservées, `pcStats` en moins) : `chip`, `reset`, `heapInfo`, `flash`, `wifi`, `loop`, `stackFree`.
- **Mesure de la boucle principale** : `loop()` est enveloppée pour chronométrer chaque tour (moyenne, pire cas, tours > 100 ms). Coût : deux appels à `micros()` par tour.
- Menu de la carte : les textes « Panel : http://… » (panel supprimé en v2) deviennent « Réglages et statut : appli PC ».
- `FW_VERSION` passe à 1.9.0 (la vérification de mises à jour compare ce numéro au tag de la release).

### Appli PC
- Stats PC retirées partout (service, réglage, tuile « Mon PC »).
- Version `2.1.0`. Nouveaux réglages `config.json` : `StatusEnabled`, `StatusLan`, `StatusPort`.

## À savoir
- **Rien n'a pu être compilé ni flashé ici** (ni SDK .NET, ni PlatformIO). C# : syntaxe vérifiée par analyseur sur les 39 fichiers. Firmware : l'analyseur signale les mêmes 6 constructions (concaténations de macros comme `"v" FW_VERSION`) avant et après mes modifications, aucune nouvelle. Page web testée avec des données d'exemple (rendu, barres, tooltips, courbes). Une erreur de compilation reste possible : envoie-moi la sortie.
- **Pas d'« optimisation » spectaculaire du firmware, volontairement** : la 1.8 gère déjà finement le tas (buffers à la demande, sprites alloués par écran, écritures NVS conditionnelles, Wi-Fi rapide). Sans pouvoir compiler ni mesurer, réécrire ce code aurait risqué de casser quelque chose. Les nouvelles mesures de charge servent justement à cibler la prochaine optimisation avec des chiffres.
- Les seuils de statut (mémoire, Wi-Fi, température) sont des estimations raisonnables, à ajuster après quelques jours de mesures réelles.

---

# Historique : v2.0 (refonte de l'appli PC)

Refonte complète de l'appli PC de la carte **Now Playing CYD**. La v1 était une interface de test (icône dans la zone de notification + page web locale) ; la v2 est une vraie appli Windows native, avec les mêmes fonctions et des réglages appliqués en direct.

> **Le firmware ne change pas.** Le protocole HTTP/UDP vers l'ESP32 est identique à la v1 : pas besoin de reflasher (testé sur le code du firmware 1.8.0). Le fichier `%AppData%\NowPlayingHub\config.json` existant est relu tel quel.

## Installer (v2.0)

1. **Supprime l'ancien dossier `pc-app/NowPlayingHub`** (les fichiers sont maintenant rangés en sous-dossiers : les garder provoquerait des erreurs « défini deux fois »), puis décompresse ce zip à la racine du projet.
2. Lance `pc-app\build.bat` (SDK .NET 8 requis). Résultat : `pc-app\dist\NowPlayingHub.exe`, un seul fichier autonome.
3. Au premier lancement, la fenêtre s'ouvre. Page **système** : adresse de la carte (`nowplaying.local` ou son IP) et jeton (`OTA_TOKEN` de `config.h`), bouton « Tester la connexion ».

## Ce qui a changé

### Interface

| Avant (v1) | Maintenant (v2) |
|---|---|
| Page web sur `localhost:8123` ouverte dans le navigateur, + page `/status` | Fenêtre WPF native, thème sombre à plat, 7 pages : **accueil, écran, veille, musique, matériel, firmware, système** |
| Boutons « Appliquer » | Chaque réglage est enregistré et envoyé à la carte **0,8 s après la dernière modification** (un seul envoi quand on fait glisser un curseur : la carte écrit dans sa flash et se redessine à chaque réception) |
| Couleur fixe | L'interface reprend **la couleur d'accent affichée par la carte** (pochette ou couleur choisie) |
| Icône de zone de notification minimale | Menu : ouvrir, précédent / lecture / suivant, démarrage auto, quitter ; titre en cours dans l'infobulle |
| Fermer = quitter | Fermer = réduire dans la zone de notification (désactivable) ; lancement avec Windows directement réduit (`--tray`) ; relancer l'appli ramène la fenêtre au premier plan |
| Aucun retour sur la lecture | Pochette, titre, barre de progression (clic = se déplacer dans le morceau), commandes, ligne de paroles en cours |

Le serveur web local a été **supprimé** (plus de port à gérer, plus de contrôle Host/Origin à maintenir). La page `/status` est remplacée par la section **Diagnostic** de la page système (état détaillé de la carte, copie du JSON, journal).

### Code

- Rangé en `Core/` (config, journal, MVVM), `Services/` (média, ESP32, synchro, météo/actus, paroles, spectre, stats, mise à jour), `ViewModels/`, `Views/`, `Themes/`. Toujours **un seul projet**, un seul namespace, aucune dépendance ajoutée (toujours NAudio seul).
- Reprise telle quelle des modules déjà éprouvés : `LyricsService`, `SpectrumService`, `PcStatsService`, `Images`.
- `Program.cs` (tray + autostart + démarrage mélangés) éclaté en `App`, `TrayIcon`, `Autostart`.
- `SettingsCoordinator` : point unique d'enregistrement + envoi (carte, veille, rien) avec délai.
- Les deux lectures du statut de la carte (`GetStatusJsonAsync`, `GetStatusAsync`) sont fusionnées en un `EspInfo` typé (mémoire, Wi-Fi, écran affiché, capteur, SD, spectre…).

### Bugs corrigés

1. **Changer de ville ou de flux RSS ne rechargeait pas la météo/les actus** avant 10 à 30 minutes. Rechargement immédiat maintenant (`IdleService.Invalidate`).
2. Une météo ou un flux RSS en échec était **retenté chaque minute** : nouvel essai espacé (2 min météo, 5 min actus).
3. Si l'envoi des réglages à la connexion échouait, il n'était **jamais retenté**. Il l'est désormais jusqu'au succès.
4. Pochette : 4 essais en 4 s puis abandon pour le morceau (Cider la publie parfois après le titre). Maintenant 8 essais espacés.
5. `config.json` : écriture **atomique** (fichier temporaire puis renommage) ; si le fichier est illisible, il est copié en `.bad` au lieu d'être écrasé en silence ; valeurs hors limites remises d'équerre.
6. Journal : il était **vidé entièrement** à 512 Ko. Rotation vers `log.old.txt`.
7. Fuite de handle GDI à la création de l'icône de la zone de notification : icône embarquée (`app.ico`).
8. Choisir une appli suivie renvoyait inutilement la config de veille à la carte.
9. Le fuseau POSIX avec chevrons (`<+03>-3`) était refusé par le panel.
10. Téléchargement/choix de firmware : taille limitée à 4 Mo.

### Autres retouches

- **Valeurs par défaut neutres** : la ville par défaut était codée en dur dans le code (coordonnées personnelles). Désormais Bruxelles ; ton `config.json` garde ses valeurs. Flux RSS par défaut : Google Actualités FR (l'ancien était un flux de podcasts RFI).
- **Recherche de ville** avec conversion automatique du fuseau IANA → chaîne POSIX (une trentaine de fuseaux connus ; sinon préréglages ou saisie libre).
- **Avertissement** tant que le jeton par défaut `nowplaying` est utilisé.
- **Vérification des mises à jour** du firmware au démarrage (si un dépôt est renseigné) : un point apparaît à côté de « firmware ».
- **Version unique** : `2.0.0` dans le `.csproj` (avant : 1.2.0 dans le projet, 1.6 dans le README, 1.8.0 dans le firmware).
- `build.bat` s'arrête avec un message si la compilation échoue ; l'exe est publié compressé et sans `.pdb`.
- Nouvelles clés dans `config.json` : `CloseToTray`, `CheckUpdatesOnStart`. L'ancienne clé `Port` est ignorée.

## À savoir

- **Je n'ai pas pu compiler ici** (pas de SDK .NET / Windows dans mon environnement). La syntaxe C# de tous les fichiers a été vérifiée avec un analyseur, le XAML est bien formé et les liaisons de chaque page ont été recoupées avec les ViewModels, mais une petite erreur de compilation reste possible au premier `build.bat` : envoie-moi la sortie et je corrige.
- Les 4 flux RSS proposés en préréglage (Google Actualités, Le Monde, France 24, BBC) n'ont pas été testés en direct ; en cas d'échec, c'est visible dans le journal.
- Les boutons de lecture utilisent la police d'icônes `Segoe MDL2 Assets` (présente sur Windows 10 et 11).
- Sections du README racine à mettre à jour : « Appli PC » (plus de panel web, ni de `http://localhost:8123`), « Page de statut », la phrase « Panel web pour tout configurer depuis le navigateur » et la structure du dépôt.

## Structure

```
pc-app/
  build.bat
  NowPlayingHub/
    NowPlayingHub.csproj   app.ico   App.xaml(.cs)   MainWindow.xaml(.cs)   TrayIcon.cs
    Core/        AppConfig, Log, Mvvm, Autostart, Dialogs, AppServices (+ SettingsCoordinator)
    Services/    MediaService, EspClient, SyncService, IdleService, LyricsService,
                 SpectrumService, PcStatsService, Images, Updater, Geocoder
    ViewModels/  Shell, Home, Display, Idle, Music, Hardware, Firmware, System
    Views/       une vue XAML par page
    Themes/      Theme.xaml (palette, styles, interrupteurs, curseurs…)
```

Données de l'appli : `%AppData%\NowPlayingHub\` (`config.json`, `log.txt`, `log.old.txt`).
