# Dlib Transformer Extensions sur macOS, épisode 14 : le zèbre et le perroquet

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 13) : écriture **33 à 37 tokens/s** (2B) ; lecture 217 tokens/s sur 512 tokens (2B) ; une image encodée en 1,8 à 2,2 s (4B).

Suite de [l'épisode 13](13-l-atelier-d-a-cote.md) : les longues lectures d'Albireo passent par Accelerate, la bibliothèque de calcul d'Apple, et vont trois fois plus vite. Restait à voir ce que ça donne quand le modèle regarde une image, ce qu'aucun essai n'avait encore fait sur le Mac.

## Deux photos de macOS

Pour avoir des images sans rien télécharger, on a pris celles que macOS propose comme photo de profil : un zèbre et un perroquet, de 512 pixels de côté. Le modèle est celui du service, Qwen3-VL 4B, avec son « projecteur », la partie qui transforme une image en tokens que le modèle de langue sait lire. On lui a demandé : « Décris cette image en une phrase. »

Les réponses sont justes. Pour le zèbre : « Une zèbre aux rayures noires et blanches se dresse en profil dans un paysage de savane dorée, avec un ciel bleu clair en arrière-plan. » Pour le perroquet : « un perroquet rouge vif avec un bec imposant ». Le féminin du zèbre est du modèle, pas du portage.

Le moteur de la veille et celui de l'épisode 13 donnent les mêmes phrases, à trois mots près pour le perroquet. L'épisode 13 a donc divisé le temps par deux : de 20 à 23 secondes pour lire l'image et la question, on est passé à 10 à 13.

## Chaque morceau regarde tous les autres

Dix secondes pour une image, c'était encore long. La photographie du moteur a montré une seule fonction, l'attention de l'encodeur d'images, qui prenait les deux tiers du temps, et rien d'autre qui dépasse quelques pour cent.

Le projecteur découpe l'image en 1 024 petits carrés. Dans un modèle de langue, chaque mot ne regarde que les mots qui le précèdent. Dans l'encodeur d'images, chaque carré regarde tous les autres, dans les deux sens : c'est une classe où chaque élève compare sa copie à celle de tous ses camarades, vingt-quatre fois de suite. Le calcul est fait par tuiles, 64 carrés contre 128 à la fois, et chaque tuile se résume à deux petites multiplications de tableaux.

Sur PC, l'auteur a écrit ces multiplications en instructions AVX2. Sur le Mac, elles tournaient en version de secours, un nombre à la fois, et la première lisait ses données en travers de la mémoire, comme on lirait un dictionnaire colonne par colonne au lieu de ligne par ligne.

## L'atelier, même pour de petites commandes

L'épisode 13 avait montré que l'atelier d'Apple, le coprocesseur matriciel, préfère une grosse commande à beaucoup de petites. Ici, impossible de faire autrement : les tuiles sont petites, et les huit cœurs les envoient en même temps. Mais la version de secours était si lente que même un atelier encombré la bat de loin. On lui a donc confié les deux multiplications de chaque tuile.

L'encodage d'une image passe de 6,7 à 8,5 secondes à 1,8 à 2,2. Lecture comprise, le modèle commence à répondre au bout de 6 à 7 secondes environ, au lieu de 11 à 13, et les descriptions restent identiques mot pour mot.

Aucun test de l'auteur ne contrôle cette fonction. On en a écrit un, hors du dépôt : 84 cas de tailles variées, comparés à un calcul en double précision. Le plus grand écart est de 3 millionièmes avec Accelerate, contre 4 millionièmes avec la version de secours. Le nouveau calcul n'est pas moins précis que l'ancien.

## Pour les curieux : les chiffres

Commande, pour chaque image et chaque moteur (`chat_after` est la copie d'`engine_chat` gardée depuis l'épisode 11) :

```
build/att/chat_after build/accel/engine_$v.dylib $H/qwen3-vl-4b-thinking.gguf --projector $H/qwen3-vl-4b-thinking.mmproj.gguf --image $I/$img.jpg --reasoning off --temperature 0 --max 60 "Décris cette image en une phrase."
```

La photographie du moteur pendant l'encodage, avant le changement (premières lignes, en échantillons) :

```
albireo::qk_encoder_attention(float const*, long, float const*, long, float const*, long, long, long, float*, long, float*)  (in engine_after.dylib)        20143
__psynch_cvwait  (in libsystem_kernel.dylib)        4334
swtch_pri  (in libsystem_kernel.dylib)        4088
expf  (in libsystem_m.dylib)        727
dlib::gguf_dq_q8_0(unsigned char const*, float*)  (in engine_after.dylib)        278
```

Les mesures alternées, `lowpowermode 0`, charge moyenne de 5 à 11 :

| Essai | Image | Encodage, avant | Encodage, après | Lecture complète, avant | Lecture complète, après |
|---|---|---|---|---|---|
| 1 | zèbre | 6 668 ms | 2 101 ms | 10 661 ms | 7 290 ms |
| 1 | perroquet | 7 749 ms | 2 181 ms | 12 099 ms | 6 838 ms |
| 2 | zèbre | 8 495 ms | 1 781 ms | 12 931 ms | 5 901 ms |
| 2 | perroquet | 8 526 ms | 1 886 ms | 12 932 ms | 6 151 ms |

Dans le code de l'auteur, le changement tient en deux fois quatre lignes, en tête de `encoder_scores` et `encoder_values`, sous la condition `ALBIREO_QK_ACCELERATE` propre au Mac. Le reste vit dans `qkernels_accelerate.inc`, le fichier du fork.

## À suivre

L'original a reçu trois nouveaux commits, qu'il faudra fusionner. Puis la page de Pseudalis à essayer dans un navigateur, et les tests de dlib, toujours en attente.
