# Dlib Transformer Extensions sur macOS, épisode 47 : les deux vitrines

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B, Gemma 4 E2B sur le GPU, les images de Gemma 3 et Gemma 4 26B-A4B : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 46](46-l-etagere-du-mac.md) : les modèles du Mac se chargeaient dans Albireo sans Internet. Cette fois, on s'est occupé de ce qu'on voit : les pages des deux applications du dépôt.

## Deux boutiques, une rue

Le dépôt contient deux applications : Albireo, qui converse, et Pseudalis, qui retire les données personnelles des documents. Elles tournent sur le même Mac, chacune avec sa propre page, et Pseudalis demande déjà à Albireo de décrire les images qu'il trouve dans les documents. Mais rien ne permettait de passer de l'une à l'autre : deux boutiques voisines sans porte entre elles.

Chacune a maintenant, sous son logo, un petit sélecteur « Albireo | Pseudalis ». Il n'apparaît que sur le Mac lui-même, quand l'autre application y tourne : chacune trouve sa voisine par son dossier de données et le port de ses réglages, et vérifie qu'elle répond.

Une précaution s'est imposée en chemin. Les sections les plus sensibles de Pseudalis (les originaux conservés, la clé, les réglages) ne s'ouvrent qu'avec un jeton d'administration, que l'icône donne sous Windows. Il aurait été tentant de le faire passer par Albireo. Mais sur le Mac, les routes d'Albireo s'ouvrent sans mot de passe : un autre compte du Mac aurait pu y lire la clé de l'armoire. Le jeton passe donc par une petite commande, `tools/open_pseudalis.sh`, qui le donne au navigateur par un fichier privé et temporaire, sans qu'il apparaisse dans la liste des programmes en cours.

## Pseudalis, rangé par pièces

La page de Pseudalis était une longue colonne qu'il fallait faire défiler. Elle a maintenant un menu à gauche, comme Albireo : protéger des fichiers, restaurer, protéger un texte, et les sections d'administration quand le jeton est là. Une seule section s'affiche à la fois, comme on passe d'une pièce à l'autre au lieu de tout voir depuis le couloir. L'état du service est rangé dans le menu, et un thème sombre suit celui du Mac.

## Le compteur de vitesse

Les deux applications affichent désormais, en bas à gauche, leur débit en direct, relu chaque seconde. Pour Albireo, ce sont les tokens lus (les questions, les documents) et écrits (les réponses) par seconde : pendant une réponse, la case « Écriture » s'allume et grimpe, puis retombe au repos, la dernière valeur restant affichée en plus pâle. Pour Pseudalis, qui ne raisonne pas en tokens, c'est le volume des documents lus et des copies protégées écrites, en kilo-octets par seconde.

Le premier compteur de Pseudalis exagérait : il comptait un document traité d'un coup comme s'il avait pris une seconde, quelle que soit sa durée. Il part maintenant du début de chaque traitement : 30 000 octets en 1,46 seconde donnent 20,5 Ko/s, et non 30.

## La page blanche

Le bouton « API » d'Albireo ouvrait une page de documentation technique toute blanche, en anglais. Elle a gardé son contenu, mais elle est habillée aux couleurs de l'application, en clair comme en sombre, et commence par une carte « Démarrage rapide » : des exemples en curl, en Python et en JavaScript, prêts à copier, qui reprennent l'adresse réelle du service et le chemin de son certificat. Celle de Pseudalis a reçu le même habillage.

## Pour les curieux : les chiffres et les fichiers

- Routes nouvelles : `GET /albireo/v1/pseudalis`, `GET /albireo/v1/speed` (les statistiques du moteur seules, bien plus légères que l'état complet), `GET /v1/speed` de Pseudalis (`engine_client::speed()`, fenêtre de cinq secondes).
- Albireo : `web/js/{layout,status,app}.js`, `web/css/app.css`, `web/docs/*` ; Pseudalis : `web/index.html`, `web/css/app.css`, `web/js/{nav,theme}.js`, `web/docs/*`, `common/engine_loader.*`, `tools/open_pseudalis.sh`.
- La page de Pseudalis interdit les scripts écrits dans la page elle-même (`script-src 'self'`) : le choix du thème avant l'affichage passe par un fichier à part.
- Tests : banc de la page d'Albireo, 114 vérifications ; test complet de Pseudalis, 34 étapes sur 34.
- Une maladresse en chemin : lancer le service de Pseudalis avec `--help`, une option qu'il ne connaît pas, l'a démarré dans son dossier par défaut. Il a été arrêté et le dossier effacé aussitôt.
- Albireo passe en **4.31.0**. Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Retour au texte : un défaut rare de Qwen 3.5 sur le GPU, le format de Mistral Small 3.2, et des boutons de la page qui suivent ce que chaque modèle sait faire.
