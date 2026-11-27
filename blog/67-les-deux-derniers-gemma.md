# Dlib Transformer Extensions sur macOS, épisode 67 : les deux derniers Gemma

> **Repère de vitesse** (Mac mini M5 Pro) : mesures du lundi 23 novembre, inchangées pour les petits modèles : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Nouveau à cette étape, Gemma 4 26B-A4B sur le GPU : lecture **1 341 à 1 421 tokens/s**, écriture **46 à 49 tokens/s**.

Suite de [l'épisode 66](66-les-documents-qui-donnent-des-ordres.md), où Gulliver apprenait à se méfier des documents qui donnent des ordres. À cette étape, on revient au moteur. Deux modèles de la famille Gemma 4 restaient sur le processeur, alors que leurs cousins plus petits avaient déjà passé sur le GPU : le 12B et le 26B-A4B. Ce dernier avait pourtant obtenu 100 sur 100 à l'épreuve de la semaine. Gulliver avait donc rangé son meilleur élève dans la salle la plus lente.

## Le 12B : une maison aux étages différents

Le 12B est un modèle ordinaire, sans experts, et tous ses poids sont dans des formats que le GPU lisait déjà. Il restait pourtant sur le processeur, avec un message qui parlait de formats inconnus. Le message se trompait de coupable.

Le modèle a deux sortes de couches. Les couches « à fenêtre » regardent les mots proches, avec huit têtes de clés. Les couches « globales » regardent tout le texte, avec une seule tête de clés, plus large. Le chemin du GPU prenait un seul nombre de têtes pour tout le modèle, comme un architecte qui dessinerait chaque étage d'un immeuble sur le plan du rez-de-chaussée.

Les couches globales ont un second trait : elles n'ont pas de matrice des valeurs. Leurs valeurs sont leurs clés, recopiées. Le contrôle du GPU cherchait cette matrice absente, et prenait son absence pour un format qu'il ne connaissait pas.

On a donc appris au GPU à lire le nombre de têtes étage par étage, et à recopier les clés dans les valeurs au bon moment, comme le fait le processeur. Pour les modèles qui ont le même nombre de têtes partout, rien ne change : Gemma 4 E2B garde sa perplexité au chiffre près.

| Gemma 4 12B | Lecture de 512 tokens | Écriture |
|---|---|---|
| Processeur, mesure de la semaine | 56,5 tokens/s | 21,9 tokens/s |
| **GPU, Gulliver 7.10.0** | **783 à 799** | **33** |
| llama.cpp | 919 | 34,7 |

## Le 26B : un bureau et cent vingt-huit spécialistes

Le 26B-A4B est d'une autre espèce. C'est un « mélange d'experts » : dans chaque couche, un routeur choisit, pour chaque mot, 8 spécialistes parmi 128. Gulliver savait déjà faire tourner ce genre de modèle sur le GPU, avec Qwen ou Mixtral. Mais Gemma 4 a sa façon à elle.

Dans chaque couche, un bureau ordinaire, le bloc dense, travaille à côté des spécialistes. Chacun lit le texte préparé à sa manière : le bureau a sa norme, le routeur une autre, les spécialistes une troisième. Chaque spécialiste a son coefficient. Les deux avis sont mis au propre séparément, puis additionnés et remis au propre une dernière fois. C'est une recette en six étapes, et l'ordre compte : une étape déplacée, et le plat change de goût sans que rien ne casse. On l'a reprise ligne par ligne sur celle du processeur, qui la suivait déjà, et sur celle de llama.cpp.

Deux détails plus terre à terre complétaient la liste :

- une partie des spécialistes range ses poids dans un format compact, le Q5_1, que le GPU ne savait pas lire. On le lui a appris : les tests le comparent au processeur, et l'écart reste de l'ordre de deux cent-millionièmes ;
- la largeur du bureau, 2 112, n'est pas un multiple de 256, la taille que le GPU attendait. Elle est un multiple de 32, et cela suffit quand les poids sont rangés par blocs de 32.

## Le contrôleur qui jugeait sur du charabia

Les noyaux justes, la recette en ordre, le modèle restait pourtant sur le processeur. Cette fois, le refus venait de Gulliver lui-même.

Avant de confier un modèle au GPU, Gulliver lui fait passer un petit examen : il lui donne une suite de mots, calcule la suite sur le GPU puis sur le processeur, et compare. Si les deux ne s'accordent pas, le modèle reste sur le processeur. C'est une bonne règle, qui a déjà rattrapé de vrais défauts. Mais la suite de mots était tirée au hasard.

Devant du charabia, le 26B hésite entre beaucoup de mots. Et chez un mélange d'experts, une hésitation coûte cher : un arrondi minuscule suffit à faire appeler un autre spécialiste, qui donne un autre avis, qui change tout ce qui suit. C'est comme juger deux traducteurs sur un texte qui ne veut rien dire : ils divergeront, et cela ne prouve pas que l'un des deux traduit mal. L'examen trouvait 27 à 57 % d'écart. Sur un vrai texte, pendant ce temps, la perplexité restait la même sur les deux.

On a d'abord cherché une erreur de calcul, et l'on a commencé par se méfier du GPU, ce qui était légitime. Les essais l'ont mis hors de cause : le même modèle, forcé sur le GPU, donnait les mêmes notes que le processeur sur le texte de référence. On a ensuite essayé d'assouplir l'examen, en acceptant que le bon mot soit parmi les cinq premiers. Les rangs observés allaient jusqu'à seize : le critère aurait dû devenir si lâche qu'il n'aurait plus rien rattrapé. Il a été écarté.

La solution retenue change le texte de l'examen plutôt que la note. Pour un mélange d'experts, Gulliver lit maintenant un motif de huit mots qui se répète. Un modèle sain le reconnaît vite et le prolonge avec assurance, sans hésiter. Sur ce texte-là, l'écart tombe entre 0,0003 % et 1,2 %, avec le même mot en tête. Les huit mélanges d'experts du Mac passent l'examen, et un noyau faux donnerait toujours des résultats sans rapport : le gardien est toujours à son poste, mais il n'interroge plus les candidats en charabia.

## Ce que ça donne

| Gemma 4 26B-A4B | Lecture de 512 tokens | Écriture |
|---|---|---|
| Processeur, mesure de la semaine | 134 tokens/s | 23,9 tokens/s |
| **GPU, Gulliver 7.12.0** | **1 341 à 1 421** | **46 à 49** |
| llama.cpp | 1 951 | 59,7 |

La lecture va dix fois plus vite, l'écriture deux fois. Une vraie question en français reçoit sur le GPU la même réponse que sur le processeur, écrite en 1,8 seconde au lieu de 10,7.

Il faut le dire franchement : sur ce modèle, Gulliver reste derrière llama.cpp, à environ 70 % en lecture et 80 % en écriture. Sur le 12B, l'écart est plus petit, autour de 85 % et 95 %. Le but du chantier vitesse est de les devancer : il reste du chemin, et ces deux modèles en sont maintenant les prochaines marches.

## Pour les curieux : les chiffres et les fichiers

- Mesures sur le Mac mini M5 Pro, 512 tokens lus puis 64 écrits (`bench_runtime`, mémoire de conversation en 8 bits, mesures alternées). Pendant la séance du 26B, une autre session faisait tourner sept programmes (charge 14 à 19) : le processeur y tombait à 75 tokens/s en lecture et 10 en écriture, d'où la comparaison avec la mesure de la semaine, faite sur une machine calme.
- Justesse du 12B (texte du banc, 4 morceaux de 512 tokens) :

  | Moteur | Perplexité |
  |---|---|
  | processeur, demi-précision | 344,0 |
  | GPU, 8 bits | 346,1 |
  | llama.cpp | 347,3 |

- Justesse du 26B (8 morceaux de 512 tokens) :

  | Moteur | Perplexité | Mot le plus probable juste |
  |---|---|---|
  | llama.cpp | 2 881 | — |
  | GPU | 2 964 | 23,77 % |
  | processeur | 3 005 | 23,73 % |

  Ces perplexités très hautes sont celles du modèle sur ce texte, et non une erreur du moteur : llama.cpp donne les mêmes sur le même fichier.
- Le message du service au chargement du 26B, sans aucune dérogation :

  ```
  GPU Apple M5 Pro: the generation steps and the prompts of 24 tokens or more, up to 576 positions, run on it
  ```

- Les huit mélanges d'experts contrôlés : LFM2.5-8B-A1B, Mixtral, Qwen3.6-11B-A3B, granite-1b-a400m, LFM2.5-24B-A2B, Qwen3.6-35B-A3B, Qwen3-VL-30B-A3B et Gemma 4 26B-A4B.
- Tests au vert : `test_metal` (dont les nouveaux produits en Q5_1), `test_qkernels`, `test_model_file`, `test_chat_format`, `test_host`, et le test complet de Gulliver (71 étapes, aucun échec).
- Fichiers du moteur touchés : `engine/cpu_runtime_metal.cpp`, `engine/metal_device.{h,mm}`, `engine/metal_kernels.metal`, `engine/metal_kernels_nax.metal`, `engine/metal_formats.metal`, `tests/test_metal.cpp`. Le détail est dans les notes pour l'auteur, changements 59 et 60.
- dlib n'est pas touché.

## La suite

Le service de la maison tourne encore sur une version d'avant : il attend l'accord de l'humain pour redémarrer en 7.12.0. Ensuite, le chantier vitesse reprend sur ces deux Gemma, pour rattraper llama.cpp, puis le dépasser.
