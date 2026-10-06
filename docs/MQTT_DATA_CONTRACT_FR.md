🇬🇧 [English](MQTT_DATA_CONTRACT.md) | 🇫🇷 Français | 🇪🇸 [Español](MQTT_DATA_CONTRACT_ES.md)

# Contrat MQTT Data

Le moteur `mqttdata` (« Données MQTT ») affiche des pages envoyées par n'importe quel éditeur MQTT : une box
domotique, un script, Node-RED. Ce document est le protocole entre l'éditeur et le panneau. Pour Home Assistant, des
blueprints prêts à l'emploi l'implémentent : voir [HOME_ASSISTANT_FR.md](HOME_ASSISTANT_FR.md).

## 1. Comment le panneau utilise MQTT

- Le broker se règle une fois dans *Système → MQTT & API → Données MQTT* (`data_mqtt` : broker, port, user, pass).
  Il est distinct de la liaison arcade `mqtt`, qui peut prendre la main sur l'affichage.
- Un écran Données MQTT (une instance du moteur `mqttdata`) liste des **topics**, séparés par des virgules, une
  **page** par topic, dans cet ordre. Rien d'autre ne se règle sur le panneau : tout le reste vient des messages.
- **Rien ne tourne en arrière-plan.** Quand l'écran arrive dans la rotation, le panneau se connecte, s'abonne à ses
  topics, affiche ce que le broker lui transmet et garde les pages à jour tant qu'il est affiché. Quand l'écran se
  termine, il se déconnecte et libère toute sa mémoire.
- L'éditeur **DOIT donc publier avec `retain: true`** : le panneau ne voit que le dernier message retenu de chaque
  topic (un instantané) plus ce qui arrive pendant que l'écran est affiché.
- Un **message retenu vide** (charge de longueur nulle, la façon MQTT de supprimer un message retenu) vide la page :
  elle affiche `PAS DE DONNEES`.
- L'identifiant client est `<hostname wifi>-data`, plusieurs panneaux peuvent donc partager un même broker.

## 2. Champs communs

Chaque message est un objet JSON.

| Champ | Type | Obligatoire | Défaut | Description |
| :--- | :--- | :--- | :--- | :--- |
| `type` | chaîne | **oui** | — | `value`, `table`, `graph` ou `weather` : choisit le rendu. |
| `v` | entier | non | `1` | Version du contrat suivie par l'éditeur (voir section 5). |
| `seconds` | entier | non | `10` | Durée de cette page, 3 à 3600. Pour `weather`, s'applique à chacune de ses pages. |

Le texte utilise une police pixel ASCII : `°` est pris en charge, les autres caractères non ASCII s'affichent `?`.
Les couleurs sont au format `"#RRGGBB"`.

### Durée d'affichage

L'écran Données MQTT fixe lui-même sa durée : un cycle complet de ses pages, soit la somme de leurs `seconds` (un
topic `weather` compte une page ACTU. plus une par jour de prévision). Une page sans message compte 10 s. La durée de
l'emplacement dans la rotation est ignorée (le moteur gère son propre rythme, comme le lecteur GIF). Les pages
défilent dans l'ordre des topics et chaque activation recommence à la première page.

## 3. Types de page

### `value` : une valeur

```json
{"type":"value","value":"21.7","unit":"°C","label":"OUTSIDE","color":"#40C0FF"}
```

| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `value` | chaîne (ou nombre) | — (obligatoire) | Valeur déjà formatée ; le panneau ne calcule rien. |
| `unit` | chaîne | `""` | Affichée après la valeur, dans la couleur du libellé, plus petite. |
| `label` | chaîne | `""` | Affiché au-dessus de la valeur. |
| `label_short` | chaîne | `""` | Utilisé si `label` ne tient pas. |
| `color` | couleur | `#FFFFFF` | Couleur de la valeur. |
| `label_color` | couleur | `#808080` | Couleur du libellé et de l'unité. |

Affichée exactement comme une `table` d'une tuile sans titre : la plus grande police qui tient.

### `table` : jusqu'à 4 valeurs

```json
{"type":"table","title":"HOME","show_title":true,
 "tiles":[{"label":"DOWNSTAIRS","label_short":"DN","value":"76","unit":"°F","color":"#40C0FF"},
          {"label":"UPSTAIRS","value":"77","unit":"°F"}]}
```

| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `title` | chaîne | `""` | Ligne de titre, affichée avec 1 à 3 tuiles s'il y a la place (jamais avec 4). |
| `show_title` | booléen | `true` | `false` masque le titre. |
| `tiles` | tableau | — (obligatoire, 1 à 4) | Chaque tuile a les champs d'une page `value` (`value`, `unit`, `label`, `label_short`, `color`, `label_color`). Les tuiles en trop sont ignorées. |

1 à 3 tuiles en colonnes, 4 en grille 2x2. Chaque tuile est empilée (libellé en haut, valeur et unité dessous) et
centrée. Le `label` complet est utilisé s'il tient, sinon `label_short`, puis une police plus petite, puis le libellé
est coupé. Une valeur n'est jamais coupée : son unité disparaît d'abord, puis elle s'affiche dans la plus petite police.

### `graph` : barres et lignes dans le temps

```json
{"type":"graph","title":"DOWNSTAIRS COOL","title_short":"DN COOL","summary":"76°","summary_color":"#40A0FF",
 "slots":96,"min":68,"max":90,"yaxis":true,
 "series":[{"label":"OUTSIDE","style":"line","color":"#FF4020","data":[75.0,74.8,null,74.1]},
           {"label":"INSIDE","style":"line","color":"#40A0FF","data":[76.0,76.2,76.1,75.9]}],
 "bands":[{"from":40,"to":43,"color":"#183860"}],
 "marks":[{"at":19,"color":"#404040"}],
 "legend":[{"text":"OUTSIDE","color":"#FF4020"},{"text":"INSIDE","color":"#40A0FF"}]}
```

| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `title` / `title_short` | chaîne | `""` | Titre de l'en-tête ; le court n'est utilisé que si le complet ne tient pas. |
| `summary` | chaîne | `""` | Partie droite de l'en-tête (par ex. la dernière valeur). |
| `summary_color` | couleur | couleur de la 1re série | Couleur du résumé. |
| `show_header` | booléen | `true` | `false` masque l'en-tête ; le graphique prend toute la hauteur. |
| `series` | tableau | — (obligatoire) | Jusqu'à 4 : `label`, `color`, `style` (`"bar"` par défaut ou `"line"`), `data` (nombres ; `null` = trou dans une ligne, 0 dans une barre). |
| `slots` | entier | série la plus longue | Positions en x ; les données sont **alignées à droite** (dernier point = le plus récent = à droite). |
| `min` / `max` | nombre | automatique | Plage y ; `max` absent ou `<= min` = automatique (lignes : plage des données élargie de 5 %, au moins 0,5). |
| `stack` | booléen | `true` | Barres empilées (piles positives et négatives séparées) ou superposées. |
| `yaxis` | booléen | `false` | Bornes de la plage affichées à gauche (panneaux d'au moins 64 px de haut). |
| `bands` | tableau | `[]` | Plages de fond `{from, to, color}` en indices de slot `[from, to)` ; jusqu'à 96, les plus récentes gardées. |
| `marks` | tableau | `[]` | Lignes verticales pointillées `{at, color}` ; jusqu'à 8. |
| `legend` | tableau | `[]` | Jusqu'à 4 `{text, color}` sous le graphique, sur les panneaux d'au moins 64 px de haut ; les éléments qui ne tiennent pas sont retirés par la fin. |
| `unit` | chaîne | `""` | Informatif. |

Les barres partent de la ligne zéro (les valeurs négatives descendent) ; une ligne zéro discrète apparaît quand 0 est
dans la plage. Les lignes font 1 px avec des jonctions verticales et sont tracées après les barres.

### `weather` : maintenant et prévisions

```json
{"type":"weather","units":"imperial","seconds":8,
 "current":{"temp":75,"condition":"clear-night","humidity":47,"wind":7,"wind_unit":"mph","wind_dir":"NE"},
 "days":[{"temp_max":89,"temp_min":70,"condition":"sunny","precip_prob":0},
         {"temp_max":90,"temp_min":71,"condition":"partlycloudy","precip_prob":5}]}
```

| Champ | Type | Défaut | Description |
| :--- | :--- | :--- | :--- |
| `units` | chaîne | `"metric"` | `"imperial"` = °F, `"metric"` = °C. Les nombres sont affichés tels quels, jamais convertis. |
| `current.temp` | nombre | — | Relevé en direct ; crée la page ACTU. |
| `current.condition` | chaîne | `""` | Identifiant de condition (liste ci-dessous). |
| `current.humidity` | entier | — | Pourcentage. |
| `current.wind` / `wind_unit` / `wind_dir` | nombre / chaîne / chaîne | — | Vitesse, unité, direction sur 8 points (`"NE"`) ; affiché `"47%  NE 9mph"`. |
| `days[]` | tableau | `[]` | Jusqu'à 5 : `temp_max`, `temp_min`, `condition`, `precip_prob`. Le jour 0 est aujourd'hui. |

Pages : ACTU. d'abord (si `current.temp` est présent), puis une page par jour, chacune pendant `seconds`. En °F, la
maximale s'affiche au-dessus de la minimale (convention américaine). Identifiants de condition : `sunny`,
`clear-night`, `partlycloudy`, `cloudy`, `fog`, `rainy`, `pouring`, `lightning`, `lightning-rainy`, `snowy`,
`snowy-rainy`, `hail`, `windy`, `windy-variant`, `exceptional` ; un identifiant inconnu s'affiche tel quel.

## 4. Limites

| | ESP32-S3 avec PSRAM (par ex. Waveshare ESP32-S3 Matrix) | ESP32 sans PSRAM (`esp32dev`) |
| :--- | :--- | :--- |
| Taille maximale d'un message | 8192 octets | 4096 octets |
| Points gardés par série de graphique (les plus récents) | 288 | 96 |
| Topics (pages) par écran Données MQTT | 6 | 4 |
| Séries / bandes / repères / éléments de légende | 4 / 96 / 8 / 4 | 4 / 96 / 8 / 4 |
| Tuiles de table / jours météo | 4 / 5 | 4 / 5 |

Les champs texte sont coupés à la taille de leur tampon : titres 23 caractères, libellés de tuile 23, valeurs 15,
unités 7. Un message au-delà de la limite n'est pas affiché : la page montre `NON PRIS EN CHARGE`, ou `PAS DE DONNEES` s'il est trop gros pour que le client MQTT le reçoive.

## 5. Versions et compatibilité ascendante

- `v` est la version du contrat suivie par l'éditeur ; ce firmware implémente la version 1.
- Les champs inconnus sont ignorés. Un message avec un `v` plus élevé est quand même affiché avec les champs que ce
  firmware connaît.
- Les nouveaux types de page auront de nouveaux noms de `type` ; un panneau qui ne connaît pas un type affiche
  `NON PRIS EN CHARGE` pour cette page seulement.

## 6. Messages affichés

| Message | Quand |
| :--- | :--- |
| `CONNEXION` | L'écran est affiché, le broker n'est pas encore connecté et aucune page n'est arrivée. |
| `PAS DE CONNEXION` | Le broker est injoignable (mauvaise adresse, connexion refusée) et aucune page n'est arrivée. |
| `PAS DE DONNEES` | Aucun message retenu sur ce topic 3 s après la connexion, ou un message retenu vide. |
| `NON PRIS EN CHARGE` | `type` absent ou inconnu, JSON invalide, message au-delà de la limite, ou contenu que le type ne peut pas afficher. Affiché pendant les `seconds` de la page (10 par défaut) ; les autres pages ne sont pas affectées. |

Les messages suivent la langue du panneau (EN / FR / ES).
