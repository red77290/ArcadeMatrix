🇬🇧 [English](HOME_ASSISTANT.md) | 🇫🇷 Français | 🇪🇸 [Español](HOME_ASSISTANT_ES.md)

# Intégration Home Assistant

Home Assistant peut alimenter les écrans **Données MQTT** du panneau : valeurs, tableaux, graphiques sur 24 h et votre
propre station météo. Home Assistant fait le travail (historique, mise en forme) ; le panneau affiche ce qu'il reçoit.
Le protocole est décrit dans [MQTT_DATA_CONTRACT_FR.md](MQTT_DATA_CONTRACT_FR.md) ; les blueprints ci-dessous
l'implémentent, vous n'avez donc aucun template à écrire.

## 1. Broker

1. Installez le module **Mosquitto broker** (Paramètres → Modules complémentaires) et l'intégration **MQTT** si ce
   n'est pas déjà fait.
2. Créez un utilisateur Home Assistant pour le panneau (Paramètres → Personnes → Utilisateurs, « Connexion uniquement
   depuis le réseau local »), par exemple `arcadematrix`. Mosquitto accepte les utilisateurs Home Assistant comme
   identifiants MQTT.
3. Sur le panneau : *Système → MQTT & API → Données MQTT* : broker = l'adresse de Home Assistant, port `1883`, cet
   utilisateur et son mot de passe. Enregistrez. Rien ne se connecte tant qu'aucun écran Données MQTT n'est affiché.

## 2. Importer les blueprints

Les blueprints sont dans [`tools/home_assistant/blueprints/`](../tools/home_assistant/blueprints/) :

| Blueprint | Genre | Publie |
| :--- | :--- | :--- |
| `arcadematrix_value.yaml` | automatisation | une entité en page `value` |
| `arcadematrix_table.yaml` | automatisation | jusqu'à 4 entités en page `table` |
| `arcadematrix_weather.yaml` | automatisation | une entité météo (actuel + prévisions) en page `weather` |
| `arcadematrix_graph_history.yaml` | **template** | un capteur contenant 24 h d'historique (96 x 15 min) d'un ou deux capteurs |
| `arcadematrix_graph.yaml` | automatisation | cet historique en page `graph` |

Importez chacun via Paramètres → Automatisations et scènes → Blueprints → **Importer un blueprint**, en collant l'URL
GitHub du fichier (`https://github.com/red77290/ArcadeMatrix/blob/main/tools/home_assistant/blueprints/<fichier>`).
Vous pouvez aussi copier les blueprints d'automatisation dans `config/blueprints/automation/arcadematrix/` et le
blueprint template dans `config/blueprints/template/arcadematrix/`, puis recharger.

Tous les blueprints publient des messages **retenus** (le panneau ne lit que ce que le broker conserve) et republient
au démarrage de Home Assistant.

## 3. Pages valeur, tableau et météo

Créez une automatisation depuis le blueprint (Paramètres → Automatisations et scènes → Blueprints → le blueprint →
Créer une automatisation), choisissez les entités et un **topic MQTT**, par exemple `arcadematrix/value/outside`. La
page est publiée tout de suite, puis à chaque changement des entités (la météo aussi toutes les 30 minutes).

Le blueprint de tableau a une entrée optionnelle **Short labels** (libellés courts) : séparés par des virgules, dans
le même ordre que les entités (par exemple `BAS, HAUT, EXT`). Un petit panneau les utilise quand un libellé complet ne
tient pas ; les panneaux plus grands gardent les mots entiers.

## 4. Pages graphique (deux blueprints)

Un graphique a besoin de 24 heures de mesures, que Home Assistant garde dans un capteur créé par le blueprint
**template** ; l'automatisation **graph** publie ensuite ce capteur.

1. Ajoutez le capteur d'historique à `configuration.yaml` (les blueprints template s'utilisent en YAML), puis
   redémarrez Home Assistant ou rechargez les entités template :

   ```yaml
   template:
     - use_blueprint:
         path: arcadematrix/arcadematrix_graph_history.yaml   # chemin sous config/blueprints/template/
         input:
           sensor_1: sensor.outdoor_temperature
           sensor_2: sensor.living_room_temperature            # optionnel
       name: Outside vs inside history
       unique_id: arcadematrix_history_outside_inside
   ```

   Si vous avez importé le blueprint depuis son URL, vérifiez le dossier où Home Assistant l'a rangé et utilisez ce
   `path`.
2. Créez une automatisation depuis **ArcadeMatrix - publish a graph** : choisissez le capteur d'historique, un topic
   (par exemple `arcadematrix/graph/inside`), un titre, les textes de légende et les couleurs.
3. L'historique se remplit pendant les **24 premières heures** : une mesure toutes les 15 minutes, la plus récente à
   droite. D'ici là, le graphique montre ce qui a déjà été collecté.

## 5. L'afficher sur le panneau

Ajoutez un écran avec le moteur **Données MQTT** et listez les topics, séparés par des virgules, dans l'ordre des
pages, par exemple :

```
arcadematrix/weather/local, arcadematrix/graph/inside, arcadematrix/value/outside
```

Jusqu'à 6 topics avec PSRAM, 4 sans. L'écran reste affiché un cycle complet de ses pages (les `seconds` de chaque
page, 10 par défaut ; un topic météo montre une page ACTU. plus une page par jour de prévision), sa durée dans la
rotation n'a donc pas d'importance. Pour retirer une page définitivement, supprimez son message retenu, par exemple
via Outils de développement → Actions → `mqtt.publish` avec le topic, une charge vide et `retain: true`.
