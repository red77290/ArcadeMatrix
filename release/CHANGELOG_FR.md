# Changelog

🇬🇧 [English](CHANGELOG.md) | 🇫🇷 Français | 🇪🇸 [Español](CHANGELOG_ES.md)

## [1.1.0] - 2026-07-09

### Ajouts
- **12 nouvelles polices / nouveaux thèmes :** intégration complète de 12 thèmes d'éditeurs iconiques (Nintendo, Capcom, Sega, SNK, Taito, etc.) applicables aux modules Clock et Date.
- **Offsets de précision (X/Y) :** ajout de nouvelles configurations d'offset X et Y via l'interface Web pour les modules Clock, Date et Weather afin de permettre un positionnement manuel au pixel près.
- **Réglages de rotation en direct :** les changements de durée de rotation (Idle Settings) et de configuration Weather (API/City) s'appliquent désormais instantanément, sans nécessiter de redémarrage.
- **Mises à jour de fuseau horaire en direct :** la mise à jour du paramètre Timezone synchronise maintenant immédiatement la RTC de l'ESP32.
- **Tailles multiples :** ajout de la sélection Clock Size et Date Size (taille 1 à 3) dans l'interface Web.

### Corrections
- **Horloge sautant des secondes :** la boucle de suivi du temps a été repensée pour récupérer directement le temps de la RTC matérielle à chaque frame (33ms) au lieu de s'appuyer sur des délais bloquants `millis()`, ce qui donne des secondes parfaitement fluides.
- **Ghosting LED (pixels verts) :** ajout de `latch_blanking = 4` à la configuration HUB75 DMA, supprimant les artefacts visuels et scintillements verts dans les coins de la matrice sur les panneaux rapides.
- **Bug des cases à cocher de playlist :** correction d'un bug où cocher une playlist de sprites dans l'interface décochait les autres, en résolvant des incohérences d'état du DOM.
- **Bouton Save manquant :** ajout de la logique manquante du bouton « Save Clock & Date » pour le panneau unifié des réglages Clock/Date.
- **Robustesse des formulaires :** empêche désormais les valeurs `NaN` de faire planter ou de polluer la configuration JSON lorsque des champs d'offset sont laissés vides dans l'interface Web.

## [1.0.0] - 2024-07-07

### Ajouts
- **Tableau de bord Web (Vite/VanillaJS) :** une interface web complète, esthétique et responsive pour contrôler la matrice en Wi-Fi sans reflasher.
- **API REST :** serveur Web asynchrone C++ (`WebServerAPI.cpp`) pour gérer tous les endpoints JSON des requêtes.
- **Clock Engine :** architecture POO pour gérer les horloges standard (Word Clock, Cyberpunk) et les horloges rétro arcade interactives (Mario, Ryu, Mega Man) déclenchées au changement de minute.
- **Intégration MQTT Batocera & Recalbox :** écoute de `batocera/events` et de `/Recalbox/EmulationStation/Event` pour déclencher instantanément des GIF spécifiques au jeu.
- **Messages marquee personnalisés :** `MessageEngine` permet de saisir du texte défilant personnalisé depuis l'interface Web avec contrôle de la couleur, de la taille, de la vitesse et de la direction.
- **Scripts d'indexation JSON natifs :** `generate_index.sh` (Mac/Linux) et `generate_index.ps1` (Windows) pour indexer instantanément le contenu de la carte SD sans Python.
- **Mode nuit / veille :** planification configurable de l'extinction / du réveil pour économiser l'alimentation et les LED.
- **Assets précompilés :** `firmware.bin` prêt à l'emploi et bundle `www` de carte SD pour les utilisateurs finaux sans environnement de développement.

### Changements
- Réécriture complète du fichier monolithique `.ino` en fichiers C++ modulaires orientés objet.
- La structure de la carte SD attend désormais les fichiers de l'interface Web dans `/www/` pour économiser la mémoire flash ESP32.

### Suppressions
- Exigence d'un script Python pour générer l'index (remplacée par des scripts Shell / PowerShell natifs).
