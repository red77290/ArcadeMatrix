# Guide de configuration (`conf.ini`)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 Français | 🇪🇸 [Español](CONFIGURATION_ES.md)

ArcadeMatrix est conçu pour être complètement découplé de son code C++ du point de vue de l'utilisateur final. Tous les paramètres sont gérés soit via l'interface Web (à `http://arcadematrix.local`), soit directement via le fichier `conf.ini` à la racine de votre carte SD.

## Référence complète
La liste exhaustive des paramètres `conf.ini` est fournie dans le dossier `release/sdcard/`. Ouvrez ce fichier pour voir toutes les valeurs disponibles et les commentaires.

### Sections clés :
- `[WIFI]` : SSID, mot de passe et nom d'hôte mDNS pour accéder à l'interface Web.
- `[MATRIX]` : mapping de géométrie (Width, Height, Panel Type, Chain count). **Ajustez ces valeurs si votre matrice est déformée.**
- `[MQTT]` : adresse IP de votre Batocera/Recalbox pour la synchronisation Live Marquee.
- `[TIME]` : fuseau horaire et options de mise en page de l'horloge.
- `[IDLE]` : séquence des modules à jouer quand rien ne se passe (Clock, Date, Weather, GIFs, Sprites).
- `[DATE]` : arrière-plans de sprites et formatage du module date.
- `[WEATHER]` : clés API OpenWeatherMap.
- `[STANDBY]` : minuteries d'économie d'énergie du mode nuit et niveau `night_brightness` (mettez 0 pour éteindre complètement le panneau la nuit).
