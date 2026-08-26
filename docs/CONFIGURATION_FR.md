🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

# Configuration Détaillée (config.json) - ESP32

Le système de configuration de l'ESP32 utilise un fichier `config.json` stocké à la racine de la carte SD. Ce fichier est généralement géré automatiquement via l'interface Web, mais il est utile de le comprendre pour des configurations avancées ou de débogage.

Depuis la mise à jour Parité S13, l'architecture est entièrement axée sur des "instances" de moteurs indépendantes.

---

## 1. Structure Globale

```json
{
  "matrix": { ... },
  "wifi": { ... },
  "system": { ... },
  "instances": [ ... ],
  "rotation": [ ... ]
}
```

---

## 2. Le Bloc `"matrix"` (Matériel HUB75 & DMA)

Ce bloc configure le bus I2S et le driver de la matrice. L'ESP32 pilotant directement les broches sans OS temps réel, ces réglages sont critiques pour la stabilité.

| Clé | Type | Description |
| :--- | :--- | :--- |
| `width` | `int` | Largeur d'un seul panneau (ex: `64`). |
| `height` | `int` | Hauteur d'un seul panneau (ex: `32`). |
| `chain_length` | `int` | Nombre de panneaux chaînés horizontalement. Attention : la RAM de l'ESP32 ne supportera pas de grandes chaînes. |
| `pwm_bits` | `int` | Profondeur des couleurs. Valeur par défaut `8`. Monter au-delà de 8 fait chuter les FPS drastiquement sur ESP32. |
| `driver_chip` | `String` | Puce contrôleur (`SHIFTREG`, `FM6126A`). |
| `force_single_buffer` | `bool` | Si `true`, le rendu est moins fluide (tearing visible) mais cela **divise par 2 l'utilisation mémoire DMA**. Très utile sur les matrices 128x64 sans PSRAM. |
| `brightness_limit` | `int` | Limiteur logiciel de luminosité maximale (`0` à `100`). Protège votre alimentation. |

*Note : Toute modification de la géométrie de la matrice (`width`, `height`, `driver_chip`, `pwm_bits`) nécessite un redémarrage physique de l'ESP32.*

---

## 3. Le Bloc `"system"` (Environnement et Veille)

| Clé | Type | Description |
| :--- | :--- | :--- |
| `timezone` | `String` | Chaîne POSIX (ex: `CET-1CEST,M3.5.0,M10.5.0/3`). |
| `format_24h` | `bool` | Format de l'heure. `true` = 23:00, `false` = 11:00 PM. |
| `lang` | `String` | Langue du système (ex: `en`, `fr`). |
| `night_mode_enabled` | `bool` | Active l'extinction ou la réduction de luminosité automatique la nuit. |
| `turn_off_at` | `String` | Heure de début de la veille (ex: `"23:00"`). |
| `wake_up_at` | `String` | Heure de réveil (ex: `"07:00"`). |
| `night_brightness` | `int` | Luminosité de veille (`0` = matrice complètement éteinte et DMA suspendu). |
| `fighter_enabled` | `bool` | Active l'incrustation des sprites de combat MUGEN (`.fgt`) au-dessus des autres moteurs. |
| `fighter_interval_sec` | `int` | Délai en secondes entre deux combats MUGEN. |

---

## 4. Le Bloc `"wifi"`

| Clé | Type | Description |
| :--- | :--- | :--- |
| `ssid` | `String` | Le nom de votre réseau Wi-Fi 2.4 GHz (l'ESP32 ne supporte pas le 5 GHz). |
| `password` | `String` | La clé WPA2. |
| `hostname` | `String` | Le nom mDNS pour accéder à l'interface via `http://hostname.local`. |

---

## 5. Moteurs : `"instances"` & `"rotation"`

Grâce à l'architecture découplée "Lazy-Once", l'ESP32 peut gérer virtuellement une infinité de configurations de moteurs sans surcharger la Heap, tant que ces moteurs ne sont pas dans la boucle active.

### `"instances"`
C'est un tableau contenant la configuration de chaque bloc logique.

```json
{
  "instance_id": "crypto_principale",
  "engine_id": "crypto",
  "config": {
    "symbols": "BTC,ETH,SOL",
    "duration_sec": 10
  }
}
```
* `instance_id` : Nom unique de ce bloc (ex: vous pouvez avoir deux widgets cryptos différents).
* `engine_id` : L'identifiant interne du moteur C++.
* `config` : Un objet JSON dynamique propre au moteur (ses `Capabilities`).

### `"rotation"`
Définit l'ordre de passage à l'écran.

```json
{
  "instance_id": "crypto_principale",
  "duration_sec": 30,
  "overlays": {
    "fighter": true
  }
}
```
* `instance_id` : Nom unique de l'instance de moteur à exécuter.
* `duration_sec` : Durée d'affichage en secondes.
* `overlays.fighter` : (Booléen optionnel, défaut `false`) Active l'overlay animé de combat MUGEN Fighter sur ce slot. Les overlays sont transversaux et activables sur TOUS les moteurs (Clock, Date, Météo, GIFs, Crypto, etc.).

L'ESP32 allouera la mémoire (`initialize()`) pour `crypto_principale` la toute première fois qu'il le rencontre dans cette boucle de rotation.

---

## 6. Configuration de tous les Moteurs

Chaque moteur déclare son schéma de configuration de manière dynamique. Voici la liste complète des moteurs et de leurs paramètres :

### Moteur : `clock`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `clock_theme` | `ENUM` | `0` | Depuis `/api/themes` | Thème visuel / style d'horloge (Nintendo, Capcom, Sega, Arcade, Cyberpunk, Flip, Tetris, etc.). |
| `clock_format` | `String` | `%H:%M:%S` | `%H:%M:%S`, `%H:%M`, `%I:%M:%S %p`, `%I:%M %p` | Chaîne de format POSIX strftime. |
| `clock_font` | `ENUM` | `PressStart2P.ttf` | Depuis `/api/fonts` | Police d'affichage (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, ou `.amf` SD). |
| `timezone` | `ENUM` | `Europe/Paris` | Depuis `/api/timezones` | Fuseau horaire / région. |
| `clock_size` | `int` | `2` | `1` à `5` | Multiplicateur de taille de police. |
| `clock_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Couleur primaire supérieure (pour le thème Personnalisé 20). |
| `clock_color_2` | `Color` | `#ff00ff` | Hex `#RRGGBB` | Couleur secondaire inférieure (pour le thème Personnalisé 20). |
| `clock_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `clock_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `date`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `date_theme` | `ENUM` | `0` | Depuis `/api/themes` | Thème visuel de la date. |
| `date_format` | `String` | `%d/%m/%Y` | `%d/%m/%Y`, `%Y-%m-%d`, `%d %b %Y`, `%A %d %B` | Format d'affichage de la date. |
| `date_font` | `ENUM` | `PressStart2P.ttf` | Depuis `/api/fonts` | Police d'affichage (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, ou `.amf`). |
| `timezone` | `ENUM` | `Europe/Paris` | Depuis `/api/timezones` | Fuseau horaire pour la date. |
| `date_size` | `int` | `1` | `1` à `3` | Multiplicateur de taille de police. |
| `date_color_1` | `Color` | `#ffffff` | Hex `#RRGGBB` | Couleur primaire (pour le thème Personnalisé 20). |
| `date_color_2` | `Color` | `#00ffff` | Hex `#RRGGBB` | Couleur secondaire (pour le thème Personnalisé 20). |
| `date_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `date_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `weather`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clé API gratuite | Votre clé API OpenWeatherMap (gratuite sur [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `Paris,FR` | Texte | Localisation de la ville (voir format ci-dessous). |
| `units` | `ENUM` | `metric` | `metric`, `imperial` | Unité de température : `metric` pour Celsius (°C) ou `imperial` pour Fahrenheit (°F). |
| `lang` | `ENUM` | `fr` | `fr`, `en`, `es`, `de`, `it` | Langue des labels des jours (AUJ. / TODAY / HOY). |
| `weather_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `weather_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

#### Comment formater le champ `city` pour OpenWeatherMap
OpenWeatherMap utilise le code pays ISO 3166 (et le code d'État à 2 lettres pour les États-Unis) :
* **International :** Utilisez `Ville,CodePays` (ex: `Paris,FR`, `Bruxelles,BE`, `Montreal,CA`, `Tokyo,JP`).
* **États-Unis :** Utilisez `Ville,CodeEtat,CodePays` (ex: `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`).
* **Où vérifier :** Rendez-vous sur [openweathermap.org](https://openweathermap.org) et cherchez votre ville.

### Moteur : `gifs`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `folder` | `LIST` | `all` | Depuis `/api/playlists` | Playlists / Dossiers de GIFs actifs (ex: `Arcade`, `Consoles`, `Fighters`). |
| `speed_multiplier` | `Float` | `1.0` | `0.25` à `3.0` | Facteur de vitesse de lecture (`1.0` = vitesse normale). |
| `shuffle` | `Boolean` | `true` | `true`, `false` | Ordre aléatoire de lecture des animations. |
| `duration_sec` | `int` | `10` | `2` à `120` | Durée d'affichage en secondes par animation GIF. |

### Moteur : `crypto` (Nécessite PSRAM)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `BTC,ETH,SOL` | Séparés par virgule | Symboles de crypto-monnaies à surveiller. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Affiche la courbe graphique sparkline historique. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Période de l'historique de prix. |
| `duration_sec` | `int` | `5` | `3` à `30` | Secondes d'affichage par page d'actif. |
| `currency` | `ENUM` | `USD` | `USD`, `EUR`, `GBP`, `JPY` | Devise de référence pour la conversion. |
| `provider` | `ENUM` | `coingecko` | `coingecko`, `binance` | Fournisseur de données de marché en direct. |
| `cache_ttl_min` | `int` | `5` | `1` à `60` | Minutes entre chaque rafraîchissement API. |
| `crypto_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `crypto_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `stock` (Nécessite PSRAM)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `AAPL,TSLA,NVDA` | Séparés par virgule | Tickers boursiers à surveiller. |
| `show_chart` | `Boolean` | `true` | `true`, `false` | Affiche la courbe graphique sparkline historique. |
| `chart_timeframe` | `ENUM` | `daily` | `hourly`, `daily`, `weekly`, `monthly` | Période de l'historique boursier. |
| `duration_sec` | `int` | `5` | `3` à `30` | Secondes d'affichage par page d'action. |
| `provider` | `ENUM` | `yahoo` | `yahoo` | Fournisseur de données de marché. |
| `cache_ttl_min` | `int` | `5` | `1` à `60` | Minutes entre chaque rafraîchissement API. |
| `stock_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `stock_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `audiovisualizer` (Nécessite Microphone)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `enabled` | `Boolean` | `false` | `true`, `false` | Active la superposition du visualiseur audio FFT en direct. |
| `style` | `ENUM` | `spectrum` | `spectrum`, `waveform`, `radial`, `neon_fire` | Style de rendu visuel audio. |
| `sensitivity` | `int` | `5` | `1` à `10` | Sensibilité de réaction du microphone. |
| `gain` | `Float` | `1.0` | `0.5` à `5.0` | Facteur de gain d'amplification audio matériel. |

### Moteur : `decibelMeter` (Nécessite Microphone)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `threshold` | `int` | `80` | `40` à `120` | Seuil d'alerte sonore en décibels (dB). |

### Moteur : `temp` (Nécessite Capteur)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `units` | `ENUM` | `C` | `C`, `F` | Unité de mesure du capteur de température embarqué. |
| `temp_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `temp_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Moteur : `message`
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `text` | `String` | `ArcadeMatrix` | Texte | Texte du message ou de la bannière à afficher. |
| `color` | `Color` | `#ffffff` | Hex `#RRGGBB` | Couleur du texte. |
| `size` | `int` | `1` | `1` à `4` | Multiplicateur de taille de police. |
| `direction` | `ENUM` | `rtl` | `rtl`, `ltr`, `ttb`, `btt`, `static` | Sens de défilement (`rtl` = droite vers gauche, `static` = texte centré fixe). |
| `speed` | `int` | `50` | `10` à `200` | Délai en millisecondes par pas de défilement (plus bas = plus rapide). |
| `font` | `ENUM` | `Default` | Depuis `/api/fonts` | Police d'affichage (`PressStart2P`, `namco`, `FreeSansBold`, `FreeMonoBold`, `RetroGaming`, `.amf`). |

### Moteur : `google_cast` (Google Home / Nest Audio - Compatible ESP32 Classic & S3)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `device_ip` | `String` | `""` | IP | IP statique de votre enceinte Google Home / Nest Audio. Laissez vide pour la découverte mDNS automatique sur le réseau local. |
| `device_name` | `String` | `""` | Texte | Filtre sur le nom de l'appareil lors de la détection automatique sur le LAN. |
| `show_album_art` | `Boolean` | `true` | `true`, `false` | Télécharge et affiche la pochette de l'album (nécessite PSRAM). |
| `show_progress` | `Boolean` | `true` | `true`, `false` | Affiche la barre de progression temporelle de la lecture en bas de l'écran. |
| `show_visualizer` | `Boolean` | `true` | `true`, `false` | Affiche l'égaliseur de fréquences animé quand la musique est en lecture. |
| `show_volume` | `Boolean` | `true` | `true`, `false` | Affiche le niveau de volume actuel de l'enceinte Google Nest. |

### Moteur : `spotify` (Lecteur Officiel Spotify - Nécessite PSRAM / ESP32-S3)
| Champ | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `client_id` | `String` | `""` | Clé API | Votre Client ID Spotify Developer API. |
| `client_secret` | `String` | `""` | Clé API | Votre Client Secret Spotify Developer API. |
| `refresh_token` | `String` | `""` | Token OAuth2 | Votre Refresh Token OAuth2 Spotify pour la synchronisation continue de la lecture. |
| `show_album_art` | `Boolean` | `true` | `true`, `false` | Télécharge et affiche la pochette d'album Spotify sur la matrice. |
| `show_progress` | `Boolean` | `true` | `true`, `false` | Affiche la barre de progression temporelle du morceau en bas de l'écran. |
| `show_visualizer` | `Boolean` | `true` | `true`, `false` | Affiche l'égaliseur audio animé quand la musique est en lecture. |
| `show_volume` | `Boolean` | `true` | `true`, `false` | Affiche le pourcentage de volume de lecture Spotify actif. |

---

*Note : Les schémas de configuration peuvent être interrogés à tout moment depuis l'ESP32 via `GET /api/engines`.*


