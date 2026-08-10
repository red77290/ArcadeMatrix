# Guide de Configuration (`conf.ini` & API)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

ArcadeMatrix est entièrement configurable sans recompiler le code C++. Tous les paramètres sont gérés soit via l'interface Web (à `http://arcadematrix.local`), soit via l'API REST, soit directement via le fichier `conf.ini` à la racine de la carte SD.

---

## 📊 Matrice de Référence Exhaustive des Paramètres

| Section | Clé `conf.ini` | Clé API JSON | Type | Default | Exemple | Effet Runtime | Persisté ? | Reboot Requis ? | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **[WIFI]** | `SSID` | `wifi_ssid` | string | `""` | `"HomeWiFi"` | Connexion au boot | Oui | **Oui** | Nom du réseau Wi-Fi 2.4GHz |
| | `PASSWORD` | `wifi_password` | string | `""` | `"secret"` | Connexion au boot | Oui | **Oui** | Mot de passe WPA2 |
| | `HOSTNAME` | `hostname` | string | `"ArcadeMatrix"` | `"my-matrix"` | mDNS | Oui | **Oui** | Nom d'hôte réseau (`http://my-matrix.local`) |
| **[MATRIX]**| `WIDTH` | `matrix_cols` | int | `64` | `256` | DMA Matrix | Oui | **Oui** | Largeur totale de la matrice en pixels |
| | `HEIGHT` | `matrix_rows` | int | `32` | `64` | DMA Matrix | Oui | **Oui** | Hauteur totale de la matrice en pixels |
| | `CHAIN` | `matrix_chain` | int | `1` | `2` | Panneaux | Oui | **Oui** | Nombre de panneaux chaînés |
| | `BRIGHTNESS_LIMIT`|`brightness_limit`| int | `100` | `80` | **En direct** | Oui | Non | Limitation de luminosité (0-100%) |
| | `COLOR_DEPTH` | `color_depth` | int | `8` | `8` | Profondeur | Oui | **Oui** | Bits par canal de couleur (8 par défaut) |
| | `DRIVER_CHIP` | `matrix_driver_chip`| string | `"SHIFTREG"` | `"FM6126A"` | Driver IC | Oui | **Oui** | Puce driver (`SHIFTREG`, `FM6126A`, `ICN2038S`, `SM16208`) |
| | `FORCE_SINGLE_BUFFER`| `force_single_buffer`| bool | `false` | `true` | Mémoire RAM | Oui | **Oui** | Force le mode simple buffer pour économiser la RAM |
| **[TIME]** | `NTP_SERVER` | `ntp_server` | string | `"pool.ntp.org"` | `"time.google.com"` | Synchro NTP | Oui | Non | Serveur de temps NTP |
| | `TIMEZONE` | `timezone` | string | `"CET-1CEST..."` | `"EST5EDT"` | Fuseau POSIX | Oui | Non | Chaîne POSIX de fuseau horaire |
| | `FORMAT_24H` | `format_24h` | bool | `true` | `false` | Affichage | Oui | Non | Format 24h (`true`) ou 12h AM/PM (`false`) |
| | `CLOCK_THEME` | `clock_theme` | int | `0` | `20` | **En direct** | Oui | Non | Thème d'horloge (0-29, 20=Custom Gradient) |
| | `CLOCK_COLOR_1` | `clock_color_1` | string | `"#ffffff"` | `"#FF0000"` | **En direct** | Oui | Non | Couleur Hex de début de dégradé |
| | `CLOCK_COLOR_2` | `clock_color_2` | string | `"#ffffff"` | `"#00FF00"` | **En direct** | Oui | Non | Couleur Hex de fin de dégradé |
| | `CLOCK_FONT_PATH`| `clock_font_path`| string | `""` | `"/fonts/my.amf"`| **En direct** | Oui | Non | Chemin vers une police `.amf` sur SD |
| **[IDLE]** | `ROTATION` | `rotation` | string | `"clock,date,weather,gifs"` | `"clock,gifs"` | **En direct** | Oui | Non | Modules actifs (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`) |
| | `CLOCK_DURATION_SEC`|`clock_duration_sec`| int | `60` | `30` | **En direct** | Oui | Non | Durée d'affichage de l'horloge en secondes |
| | `GIFS_COUNT` | `gifs_count` | int | `3` | `5` | **En direct** | Oui | Non | Nombre de GIFs à jouer par cycle |
| | `FIGHTER_ENABLED`| `fighter_enabled` | bool | `true` | `false` | **En direct** | Oui | Non | Activer l'overlay de combat MUGEN |
| | `FIGHTER_INTERVAL_SEC`|`fighter_interval_sec`| int | `10` | `20` | **En direct** | Oui | Non | Intervalle en secondes entre les combats MUGEN |
| **[STANDBY]**| `NIGHT_MODE_ENABLED`|`night_mode_enabled`| bool | `false` | `true` | **En direct** | Oui | Non | Activer la mise en veille automatique la nuit |
| | `TURN_OFF_AT` | `turn_off_at` | string | `"23:00"` | `"22:30"` | **En direct** | Oui | Non | Heure d'extinction (HH:MM) |
| | `WAKE_UP_AT` | `wake_up_at` | string | `"07:00"` | `"08:00"` | **En direct** | Oui | Non | Heure de réveil (HH:MM) |
| | `NIGHT_BRIGHTNESS`|`night_brightness`| int | `10` | `0` | **En direct** | Oui | Non | Luminosité de nuit (0 = écran complètement éteint) |
| **[CRYPTO]** | `ENABLED` | `crypto_enabled` | bool | `true` | `false` | **En direct** | Oui | Non | Activer le ticker crypto |
| | `SYMBOLS` | `crypto_symbols` | string | `"BTC,ETH,SOL,DOGE"` | `"BTC,ETH"` | **En direct** | Oui | Non | Symboles crypto à suivre (séparés par des virgules) |
| | `DURATION_SEC` | `crypto_duration_sec`| int | `5` | `10` | **En direct** | Oui | Non | Durée d'affichage par symbole en secondes |
| **[STOCK]** | `ENABLED` | `stock_enabled` | bool | `true` | `false` | **En direct** | Oui | Non | Activer le ticker boursier |
| | `SYMBOLS` | `stock_symbols` | string | `"AAPL,NVDA,TSLA,MSFT"` | `"AAPL"` | **En direct** | Oui | Non | Symboles boursiers (séparés par des virgules) |
| | `DURATION_SEC` | `stock_duration_sec` | int | `5` | `10` | **En direct** | Oui | Non | Durée d'affichage par symbole en secondes |

---

## 📌 Remarques Importantes

- **`ROTATION`** : La chaîne `ROTATION` gère la séquence des modules d'affichage autonome (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`). **Attention : `sprites` n'est pas un module de rotation**. Les combattants MUGEN sont gérés par le moteur d'overlay `FighterEngine` via la propriété `FIGHTER_ENABLED`.
- **Réinitialisation & Reboot** : Les modifications de géométrie matérielle (`WIDTH`, `HEIGHT`, `DRIVER_CHIP`, `CHAIN`, `COLOR_DEPTH`) ou de Wi-Fi nécessitent un redémarrage du système (`POST /api/system/reboot` ou bouton *Reboot System* sur la WebUI). Toutes les autres options (thèmes, couleurs, durées, luminosité) s'appliquent immédiatement à chaud.
