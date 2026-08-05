# Guide de configuration (`conf.ini`)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

ArcadeMatrix est conçu pour être complètement découplé de son code C++ du point de vue de l'utilisateur final. Tous les paramètres sont gérés soit via l'interface Web (à `http://arcadematrix.local`), soit directement via le fichier `conf.ini` à la racine de votre carte SD.

## Référence complète
La liste exhaustive des paramètres `conf.ini` est fournie dans le dossier `release/sdCard/`. Ouvrez ce fichier pour voir toutes les valeurs disponibles et les commentaires.

### Sections clés :
- `[WIFI]` : SSID, mot de passe et nom d'hôte mDNS pour accéder à l'interface Web.
- `[MATRIX]` : mapping de géométrie (Width, Height, Chain count), profondeur de couleur et bufferisation. **Ajustez ces valeurs si votre matrice est déformée.** `PANEL_TYPE` est accepté et sauvegardé mais n'a actuellement aucun effet sur le driver (mapping de pins unique et codé en dur pour tous les types de panneaux - voir `docs/HARDWARE_FR.md`).
- `[MQTT]` : adresse IP de votre Batocera/Recalbox pour la synchronisation Live Marquee.
- `[TIME]` : fuseau horaire, options de mise en page de l'horloge, et couleurs de dégradé `clock_color_1`/`clock_color_2`. Le paramètre `clock_theme` supporte plus de 20 horloges spécialisées (incluant Matrix Rain, Cyberpunk, Tetris, Pac-Man, etc.).
- `[IDLE]` : séquence des modules à jouer quand rien ne se passe (Clock, Date, Weather, GIFs, Sprites), incluant `fighter_interval_sec` (délai entre les combats MUGEN). **Nouvelles propriétés** : `mode` (ex: `default`, `clock_only`) et `gifs_before_clock` (nombre de GIFs à jouer avant de revenir sur l'horloge).
- `[DATE]` : arrière-plans de sprites, formatage, et couleurs de dégradé `date_color_1`/`date_color_2` du module date.
- `[WEATHER]` : clés API OpenWeatherMap.
- `[STANDBY]` : minuteries d'économie d'énergie du mode nuit et niveau `night_brightness` (mettez 0 pour éteindre complètement le panneau la nuit).
- `[FONTS]` : `custom_font_path` optionnel vers une police bitmap `.amf` chargée depuis la carte SD (voir `tools/bdf_to_amfont/README_FR.md`).
