#pragma once
// Copie ce fichier en config.h (il est dans le .gitignore : ne le commite jamais).

// Wi-Fi : OPTIONNEL. Laisse vide pour configurer la carte depuis ton telephone :
// au premier demarrage elle ouvre un reseau « NowPlaying-Setup » (page de config sur 192.168.4.1).
#define WIFI_SSID  ""
#define WIFI_PASS  ""

// Jeton exige pour redemarrer / mettre a jour la carte en Wi-Fi.
// Doit etre IDENTIQUE dans le panel de l'appli PC. CHANGE-LE.
#define OTA_TOKEN  "change-moi"
