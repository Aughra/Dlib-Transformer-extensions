# Dlib Transformer Extensions sur macOS, épisode 21 : le tableau de bord

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 20) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 20](20-la-lecture-aussi.md) : le GPU écrit et lit. Restait à pouvoir le régler sans ouvrir de fichier, et à se servir d'Albireo ailleurs que devant le Mac.

## Une clé que personne ne pouvait tenir

Sous Windows, Albireo se règle par une petite icône près de l'horloge. Elle seule détient une clé, un « jeton d'administration », que le service exige pour changer un réglage, effacer un modèle ou redémarrer. La page web, elle, n'a pas cette clé, exprès : un site malveillant ouvert dans le navigateur ne doit pas pouvoir piloter le service. Le service refuse aussi toute requête qui vient d'un autre site.

Sur Mac, l'icône n'existe pas. La clé dormait donc dans un fichier, et pour couper le GPU il fallait éditer ce fichier à la main. C'est un trousseau rangé dans un tiroir dont on aurait jeté la poignée.

## Ouvrir la porte, en connaissance de cause

Ce fork est une version personnelle. On a donc retiré les deux protections, la clé et le contrôle d'origine. La page peut désormais tout régler. Le risque est connu et écrit noir sur blanc dans la page : un site piégé, visité sur le Mac pendant que le service tourne, pourrait en théorie lui donner des ordres. Chez soi, c'est une chambre sans verrou ; il suffira d'en remettre un si l'usage change.

## Un onglet, quatorze réglages, trois voix

Les paramètres de la page gagnent un onglet « Service ». Chaque réglage y a son interrupteur, son curseur ou son champ, et une ligne qui dit ce qu'il change. Certains s'appliquent tout de suite, comme le mode hors ligne ou le bouton Web. D'autres, comme le GPU ou la mémoire des conversations, demandent de redémarrer le service : ils attendent un bouton « Appliquer et redémarrer », qui les envoie tous ensemble. Le redémarrage prend deux secondes environ.

À côté de chaque nom, un point d'interrogation. Au survol, il donne le principe en une phrase. Touché, il déplie une fiche, à la manière du glossaire de TortueTech : un schéma, les avantages et les inconvénients, puis trois explications. La première est normale. La deuxième est confiée à un hibou à lunettes, inventé pour l'occasion, qui parle très technique. La troisième revient à la marmotte du glossaire, pour un lecteur de quinze ans. Enfin, l'abeille dit ce qui arrive si l'on pousse le réglage trop loin, ou qu'on s'en passe.

Pour le GPU, la marmotte parle d'une cuisine : un chef qui sait tout faire, mais une chose à la fois, et une brigade de commis qui épluchent mille carottes ensemble sur le même plan de travail. Le hibou, lui, cite les tampons pris sans copie et la tolérance du contrôle au chargement. Les chiffres des fiches ont été vérifiés dans le code et dans le journal du service : deux affirmations qu'on n'avait pas pu vérifier ont été retirées avant la publication.

## Depuis le canapé

Dernier souhait : ouvrir Albireo sur le téléphone, depuis le canapé. Le service ouvre donc une seconde porte, sur l'adresse du Mac dans le réseau de la maison, au port 8444. Celle-ci n'est pas sans serrure : elle demande un mot de passe. Le service n'en garde qu'une empreinte, impossible à remonter, calculée lentement exprès pour décourager les essais en série. Le bon mot de passe donne au téléphone un badge, un cookie signé, valable trente jours. En choisir un nouveau annule tous les badges d'un coup. Un mot de passe faux coûte une seconde d'attente.

Cette porte parle en http, sans chiffrement. Le chiffrement aurait exigé que le téléphone fasse confiance à l'autorité de certification du Mac. Le prix est connu : le Wi-Fi voit passer les échanges en clair, et le micro du téléphone ne sert pas à la dictée, car les navigateurs le réservent aux pages chiffrées. La copie dans le presse-papiers, elle aussi réservée à ces pages, passe par un vieux détour qui marche partout. Une chose surprend au premier usage : les conversations vivent dans le navigateur de chaque appareil. Celles du téléphone ne sont donc pas celles du Mac.

## Pour les curieux : la porte du réseau, essayée

Depuis l'adresse du Mac sur le réseau (`192.168.1.12`), avec `curl` :

```
page sans cookie:
200 text/html; charset=utf-8
API sans cookie:
{"error":{"message":"the network access asks for its password first","type":"authentication_error","param":null,"code":"lan_login"}}
mauvais mdp:
{"error":{"message":"wrong password","type":"authentication_error","param":null,"code":"lan_password_wrong"}} 401 1.232676s
bon mdp:
HTTP/1.1 200 OK
Set-Cookie: albireo_lan=<masqué>; Path=/; Max-Age=2592000; HttpOnly; SameSite=Strict
faux cookie:
401
localhost inchangé:
200
```

| Élément | Choix |
|---|---|
| Empreinte du mot de passe | PBKDF2-HMAC-SHA-256, sel aléatoire, 200 000 tours |
| Badge | cookie `albireo_lan` : un nombre au hasard et sa signature HMAC, la clé étant l'empreinte |
| Durée | 30 jours ; un nouveau mot de passe les annule tous |
| Porte | http, port 8444, toutes les adresses du Mac |
| Sur le Mac | https, port 18443, sans mot de passe |

Le test complet d'Albireo passe toujours, 19 étapes sur 19.

## À suivre

Prochaine étape demandée depuis ce même canapé : les documents. Il s'agit de les prendre dans un dossier dédié du Mac, et de pouvoir en envoyer par la page, depuis le téléphone.
