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
  "duration_sec": 30
}
```
L'ESP32 allouera la mémoire (`initialize()`) pour `crypto_principale` la toute première fois qu'il le rencontre dans cette boucle de rotation.

---

## 6. Configuration des moteurs financiers (`crypto` & `stock`)

Les moteurs `crypto` et `stock` intègrent un affichage multi-pages avec cours en temps réel et courbes graphiques historiques (sparklines) :

| Clé | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `symbols` | `String` | `"BTC,ETH"` / `"AAPL,NVDA"` | Séparés par virgule | Actifs financiers à surveiller. |
| `show_chart` | `bool` | `true` | `true`, `false` | Active la page avec la courbe sparkline historique. |
| `chart_timeframe` | `String` | `"daily"` | `"hourly"`, `"daily"`, `"weekly"`, `"monthly"` | Échelle de temps pour l'historique de prix. |
| `duration_sec` / `page_seconds` | `int` | `5` | `3` à `30` | Durée en secondes d'affichage de chaque vue avant alternance. |
| `cache_ttl_min` | `int` | `5` | `1` à `60` | Minutes de rétention du cache avant ré-interrogation API. |

---

## 7. Configuration du moteur Météo (`weather`)

Le moteur `weather` récupère les prévisions météo sur 3 jours depuis [OpenWeatherMap](https://openweathermap.org) avec mise en cache automatique de 15 minutes :

| Clé | Type | Défaut | Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| `api_key` | `String` | `""` | Clé API gratuite | Votre clé API OpenWeatherMap (gratuite sur [openweathermap.org](https://home.openweathermap.org/users/sign_up)). |
| `city` | `String` | `"Paris,FR"` | Texte | Localisation de la ville (voir format ci-dessous). |
| `units` | `String` | `"metric"` | `"metric"`, `"imperial"` | Unité de température : `metric` pour Celsius (°C) ou `imperial` pour Fahrenheit (°F). |
| `lang` | `String` | `"fr"` | `"en"`, `"fr"`, `"es"`, `"de"`, `"it"` | Langue des labels des jours (AUJ. / TODAY / HOY). |
| `weather_offset_x` | `int` | `0` | `-64` à `64` | Décalage horizontal en pixels. |
| `weather_offset_y` | `int` | `0` | `-32` à `32` | Décalage vertical en pixels. |

### Comment formater le champ `city` pour OpenWeatherMap
OpenWeatherMap utilise le code pays ISO 3166 (et le code d'État à 2 lettres pour les États-Unis) pour identifier sans ambiguïté la localité :
* **International :** Utilisez `Ville,CodePays` (ex: `Paris,FR`, `Bruxelles,BE`, `Montreal,CA`, `Tokyo,JP`).
* **États-Unis :** Utilisez `Ville,CodeEtat,CodePays` (ex: `Tucson,AZ,US`, `Miami,FL,US`, `Dallas,TX,US`). Si l'État ou le pays est omis, l'API peut renvoyer une homonyme située dans un autre pays/état.
* **Où vérifier :** Rendez-vous sur [openweathermap.org](https://openweathermap.org) et cherchez votre ville. Le titre du résultat et l'URL indiquent exactement le format reconnu.


