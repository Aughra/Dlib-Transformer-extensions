# Dlib Transformer Extensions sur macOS, épisode 40 : le panier du voisin

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B et Gemma 3 4B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie). Cet épisode ne touche pas à la vitesse.

Suite de [l'épisode 39](39-les-dettes-payees.md) : le fork avait payé ses dettes envers le code d'origine. Restait à regarder ce que l'original avait fait de son côté, sans nous.

## Faire son marché chez le voisin

Depuis l'épisode 18, le fork ne fusionne plus l'original : il avançait trop vite, et le moteur Metal demandait de percer des murs. L'original a continué sa route, avec 31 commits depuis la dernière fusion. On ne les a pas récupérés en bloc. On les a lus un par un, comme on passe devant l'étal d'un voisin jardinier, et on a dressé une liste de ce qui servirait sur Mac.

Une bonne moitié n'avait rien pour nous : les cartes graphiques NVIDIA, OpenVINO, l'installateur et l'icône de Windows, la mémoire des cartes qu'un Mac n'a pas. Il restait onze idées. L'humain en a choisi sept. Chacune a été recopiée à la main, en français dans les commentaires et adaptée au découpage de notre page, qui n'a plus la forme de celle de l'original. Le mérite de ces idées revient à l'auteur de l'original ; le travail du fork a été de les faire entrer chez lui.

## L'élève qui repose la même question

Un petit modèle qui cherche sur le web peut tourner en rond. L'auteur en a vu un chercher douze fois la même vulnérabilité sur Wikipédia, en changeant une année ou un mot. C'est l'élève qui retourne demander au bibliothécaire le même livre, sous un titre à peine différent, en espérant une autre réponse.

Désormais, un appel identique à un précédent n'est pas exécuté, une recherche qui reprend les mots de deux autres non plus, et l'agent n'a droit qu'à huit recherches par outil. Au second refus, les outils s'arrêtent, et une dernière étape demande au modèle de répondre avec ce qu'il a trouvé. Une recherche sur une autre ville ou une autre année, elle, passe : on l'a vérifié.

## Déménager sans laisser de mot sur le frigo

« Supprimer toutes les discussions » laissait derrière lui la mémoire d'Albireo, le nom de l'utilisateur et ses instructions, appris ou donnés dans ces discussions mêmes. C'est déménager en laissant ses notes collées sur le frigo. Ces trois réglages partent maintenant avec le reste. Le détail le plus fin vient de l'original : une remise à zéro est datée, pour qu'une mise à jour de la mémoire déjà commencée ne réécrive pas les anciennes notes une seconde plus tard.

## L'examen à durée limitée

Les modèles « qui réfléchissent » pensent à voix basse avant de répondre. Sans limite, certains réfléchissent jusqu'à ne plus avoir de place pour la réponse, comme un candidat qui passe toute l'épreuve au brouillon. Par défaut, la réflexion prend maintenant au plus la moitié de la réponse autorisée.

Ce changement a fait échouer deux étapes du test d'Albireo, et c'était justifié. La première vérifiait l'ancienne règle, « pas de limite par défaut » : on l'a mise à jour comme l'original. La seconde a donné une bonne réponse sur deux lignes au lieu d'une, parce que la réflexion avait été coupée à 200 tokens. Avec un peu plus de place, la réflexion se termine d'elle-même et la réponse retrouve sa ligne.

## Les guillemets dans les guillemets

Quand un modèle écrit du code pour un outil, il doit placer ce code entre guillemets, et échapper ceux du code lui-même. Les petits modèles l'oublient. C'est un dialogue de roman dont l'imprimeur ne sait plus quels guillemets ferment la réplique. L'original a écrit une petite enquête : essayer chaque lecture possible des guillemets, et garder la seule qui donne un appel valide d'après la liste des outils. Si aucune lecture ne convient, ou plus d'une, le texte reste tel quel plutôt que de deviner.

Au premier essai, trois vérifications ont échoué. Le fork ne transmettait la liste des outils à son lecteur d'appels que pour les modèles Qwen 3.5. Sans cette liste, l'enquête trouvait plusieurs lectures et, prudente, refusait. Une ligne par format a suffi.

## La jauge et le carnet

La page a gagné une jauge, comme celle d'un réservoir d'essence : une barre qui montre la part du contexte du modèle qu'occupe la discussion. Elle passe à l'orange aux trois quarts, au rouge à 90 %. Au passage, la fenêtre d'une discussion est devenue le contexte entier du modèle. Les plus vieux échanges sont résumés seulement au-delà de la moitié.

Enfin, la zone de saisie est devenue un carnet qui se met en forme pendant qu'on écrit. Deux astérisques autour d'un mot le mettent en gras, un tiret commence une liste, et les marques restent visibles en gris clair. Le texte envoyé au modèle ne change pas : c'est le markdown tapé. Ce changement a demandé de mettre à jour le banc d'essai de la page, qui cherchait une zone de saisie ordinaire.

## Un banc trop pressé

Le banc d'essai de la page avait lui-même un défaut, de la même famille que ceux de l'épisode 39. Il posait sa première question dès que le service répondait, sans attendre la fin du chargement du modèle. En mode économie d'énergie, ce chargement prend une vingtaine de secondes, et la page répondait poliment « Albireo démarre : posez la question dans un instant ». Le banc attend maintenant que le modèle soit prêt.

## Pour les curieux : la liste et les vérifications

| Reprise | Version de l'original | Version du fork |
|---|---|---|
| Garde-fou de l'agent contre les appels répétés | 1.0.15 | 4.23.0 |
| « Tout supprimer » efface la mémoire, le nom, les instructions | 1.0.15 | 4.23.0 |
| Limite de réflexion par défaut (moitié de la réponse, 32 768 tokens au plus) | 1.0.14 | 4.24.0 |
| Réparation des appels aux guillemets non échappés (`stray_quotes`) | 1.0.14 | 4.24.0 |
| Modèle livré : Qwen3-VL 4B Instruct | 1.0.15 | 4.24.0 |
| Barre de contexte, fenêtre égale au contexte du modèle | 1.0.14 et 1.0.17 | 4.25.0 |
| Champs qui dessinent le markdown (`mdfield.js`) | 1.0.15 | 4.25.0 |

- Banc de la page : regress 43, docs 12, images 12, actions 26 (dont la suppression totale : nom, instructions et mémoire vides dans la base), tabs 11, phone 8, sans erreur de la page. La barre affichait « 77 / 262 144 » après deux questions.
- `test_chat_format` passe avec les cinq cas de l'original (code Python aux guillemets non échappés, deux appels dans un bloc, appel ambigu laissé en texte…).
- `test_macos.sh` : 49 étapes sur 51 au passage complet ; les deux étapes corrigées (`test_engine_api`, « lfm2 moe long prompt » à `--max 800`) passent ensuite seules.
- Le fichier du modèle Instruct a été vérifié auprès de Hugging Face : 2 497 281 664 octets, SHA-256 `66358cb1…`.
- Non essayé : une vraie boucle d'agent avec un modèle, difficile à provoquer exprès. Le garde-fou a été essayé seul, avec Node.
- Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Quatre idées de la liste attendent encore : Mistral Small 3.2, les boutons réglés sur les capacités vérifiées de chaque modèle, l'écriture de plusieurs mots par pas et les nouveautés de Pseudalis. Les plus lourdes attendront le Mac mini, qui arrive bientôt.
