# Dlib Transformer Extensions sur macOS, épisode 70 : le cachet de la maison

> **Repère de vitesse** (Mac mini M5 Pro) : inchangé depuis [l'épisode 68](68-la-tete-trop-large.md) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384.

Suite de [l'épisode 69](69-le-sceau-et-la-langue-unique.md), où Gulliver retrouvait le sceau de l'auteur sur son catalogue. Restait la seconde moitié du chantier : les modèles qui n'ont pas de sceau du tout.

## Le sceau ne couvre pas tout

Le catalogue signé de l'auteur liste une vingtaine de modèles. Mais Gulliver sait aussi en prendre ailleurs : sur Hugging Face, la grande bibliothèque publique des modèles, dans un dossier du Mac, ou par un fichier qu'on lui envoie. La veille du lundi en télécharge d'elle-même chaque semaine. Sur le Mac de l'humain, 34 modèles sont installés : 23 figurent dans le catalogue signé, 11 n'y figurent pas.

Pour ces onze-là, aucun sceau ne garantit rien. Un fichier téléchargé peut avoir été choisi par erreur, ou remplacé plus tard sur le disque par un autre du même nom. Jusqu'ici, Gulliver chargeait tout ce qui était installé.

L'humain avait choisi la règle parmi trois propositions : un modèle hors de tout catalogue signé ne se charge pas tant qu'il ne l'a pas validé lui-même. C'est la règle d'une maison où l'on accepte les colis du facteur officiel, et où les autres attendent dans l'entrée que le propriétaire les ouvre et y appose son propre cachet.

## Un cachet que personne ne peut emporter

Encore fallait-il que ce cachet ne puisse pas être imité. Une signature de l'humain, gardée dans un fichier, n'aurait rien valu : qui copie le fichier copie le cachet.

Les Mac récents contiennent une petite puce à part, la Secure Enclave, une sorte de coffre scellé dans le processeur. On peut lui demander de fabriquer une clé de signature ; la partie secrète de cette clé naît dans le coffre et n'en sort jamais. On peut lui demander de signer, pas de dire avec quoi. C'est le cachet idéal : il reste dans la maison.

Il a fallu une étape d'essais pour trouver comment s'en servir sans les autorisations d'un programme distribué par l'App Store. Une clé rangée dans le trousseau du Mac est refusée. En revanche, la puce accepte de créer une clé non rangée et d'en rendre une forme scellée, 324 octets que seule cette puce, sur ce Mac, sait rouvrir. Gulliver garde cette forme scellée dans sa base. On a vérifié qu'un autre programme, lancé plus tard, la rouvre, signe, et que la signature se vérifie.

## Ce que l'humain voit

Dans l'onglet Modèles, chaque modèle installé porte maintenant une ligne marquée d'un cachet 🔏 : « signé », s'il vient du catalogue de l'auteur ; « fourni avec Gulliver » ; « validé par l'administrateur » ; ou, dans un encadré rouge, « non signé : il ne se charge pas ». Pour ce dernier, le bouton « Choisir » est grisé, et un bouton « Valider ce modèle » attend.

La validation demande une confirmation, qui rappelle de ne valider qu'un modèle dont on connaît la provenance. Gulliver relit alors le fichier en entier, vérifie qu'il n'a pas changé depuis son installation, et fait signer par la puce son identifiant et son empreinte. Si le modèle était déjà choisi pour une fonction, il se charge aussitôt.

## Le garde à la porte

Avant chaque chargement, un contrôle passe. Il regarde d'abord la taille du fichier, sa date et son numéro sur le disque : s'ils sont ceux du dernier contrôle, rien n'a bougé, et le contrôle ne coûte rien. Sinon, il relit tout le fichier et recalcule son empreinte. Un fichier remplacé est refusé. Un petit malin qui changerait aussi l'empreinte notée dans la base n'irait pas plus loin : la signature de la puce porte sur l'ancienne empreinte, et ne correspond plus.

Les tests l'ont essayé dans tous les sens : un modèle du catalogue passe ; un modèle d'ailleurs est refusé, puis validé, puis passe ; son fichier réécrit avec la même taille est refusé ; l'empreinte trafiquée dans la base défait la validation ; la validation retirée, il est de nouveau refusé.

Le premier essai du service entier a montré un oubli. Un modèle qu'on envoie à Gulliver prend tout de suite les fonctions libres, comme celle de répondre aux questions. Non signé, il échouait à se charger, et l'activation était annulée ; validé ensuite, il restait sans fonction, et la question de l'essai, « quelle est la capitale de la France ? », ne trouvait personne pour y répondre. Désormais, un modèle validé est traité comme un modèle qui arrive. L'essai répond « Paris ».

## La limite, dite franchement

Ce cachet n'est pas protégé par un code ni par une empreinte digitale : le service tourne sans écran, et personne ne serait là pour taper le code. Un programme hostile qui tournerait déjà sous le compte de l'humain pourrait donc demander à la puce de valider un modèle. Le cachet empêche autre chose, et c'est déjà beaucoup : qu'une copie de la base serve sur une autre machine, et qu'un fichier changé après coup se charge sans que personne le voie. On l'a écrit dans le manuel et dans les notes de version, plutôt que de laisser croire à une forteresse.

## Pour les curieux : les chiffres et les fichiers

- Version 7.17.0. Test complet : 81 étapes, sans échec, dont :

  ```
  service model unsigned       PASS     0.0 s  trust unsigned
  service model approved       PASS     1.2 s  {"ok":true,"trust":"approved"}
  service model trust          PASS     0.0 s  trust approved
  service chat                 PASS     0.1 s  ... "The capital of France is Paris." ...
  ```

- La clé : `SecKeyCreateRandomKey` avec `kSecAttrTokenIDSecureEnclave`, non permanente, contrôle d'accès `kSecAccessControlPrivateKeyUsage` ; sa forme scellée est l'attribut `toid` (324 octets), rouverte par `SecKeyCreateWithData`. Le texte signé : `gulliver-model-approval-v1`, l'identifiant, l'empreinte du modèle et celle de son projecteur.
- Le contrôle avant chargement : `model_manager::may_load()`, appelé par `service::load_model()` et par la mesure des moteurs.
- Les routes : `POST` et `DELETE /gulliver/v1/models/{id}/approve`, réservées à l'administrateur, refusées par la passerelle de Cloudflare ; le champ `trust` de `GET /gulliver/v1/models`.
- Les bancs d'essai (test complet, test de la page, test de la recherche, mesure du service) valident leurs propres modèles d'essai, comme le ferait l'humain. Le test de la page en a profité pour perdre une ligne de Python.
- Fichiers : `host/approval_key.{h,cpp}` (nouveaux), `host/models.{h,cpp}`, `host/service.{h,cpp}`, `host/server.cpp`, `web/js/models.js`, `web/openapi.json`, `tests/test_host.cpp`, `tools/test_macos.sh`, `tools/page_test/page_test.sh`, `cloudflare/test/routes.test.mjs`.
- dlib n'est pas touché.

## La suite

Les onze modèles de l'humain venus de Hugging Face attendent leur cachet : il les validera une fois, depuis l'onglet Modèles. Puis le Python continue de reculer, outil par outil.
