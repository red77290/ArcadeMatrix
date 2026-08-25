# Contribuer à ArcadeMatrix (ESP32)

[English](CONTRIBUTING.md) | 🇫🇷 Français | 🇪🇸 [Español](CONTRIBUTING_ES.md)

Merci de votre intérêt pour le projet ArcadeMatrix !

## 1. Principes de Développement
- **Respect des contraintes matérielles** : Tout code exécuté dans la boucle d'affichage (`update` et `render`) doit avoir zéro allocation dynamique de mémoire.
- **Isolation matérielle** : Pas de `psramFound()` ou de `#ifdef` éparpillés. La détection matérielle appartient exclusivement à `HardwareHAL` et le filtrage des prérequis à `EngineRegistrar`.
- **Gel du câblage** : Les définitions de broches dans `include/HardwareProfile.h` sont validées et gelées. Ne pas modifier les pin-maps.
- **Documentation trilingue** : Lors de la modification des guides ou de l'architecture, maintenez les versions EN, FR et ES synchronisées.

## 2. Exécution des Tests
```bash
pio test -e esp32dev
```

## 3. Processus de Pull Request
1. Forkez le dépôt et créez votre branche.
2. Vérifiez que votre code compile sans erreur sur les deux cibles :
   ```bash
   pio run -e esp32dev
   pio run -e esp32s3_waveshare
   ```
3. Soumettez votre PR avec une description claire de vos modifications.
