# Dlib Transformer Extensions sur macOS, épisode 41 : les grandes têtes

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B et Gemma 4 E2B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 40](40-le-panier-du-voisin.md) : on avait fait son marché chez l'original. On revient au chantier en cours avant cette parenthèse : faire passer les Gemma au GPU. Gemma 3 y était depuis l'épisode 38. Restait Gemma 4.

## Des têtes trop grandes pour la boîte

Un modèle regarde le texte par plusieurs « têtes » d'attention à la fois, chacune avec sa propre largeur de vue. Gemma 4 en a de deux tailles : 256 valeurs dans les couches qui ne regardent que les 512 derniers mots, et 512 valeurs dans ses couches globales, qui voient tout. Les noyaux du GPU, ces petits programmes que la puce graphique répète des milliers de fois, s'arrêtaient à 256.

Pour l'écriture d'une réponse, mot après mot, il a suffi d'agrandir la boîte. Un seul noyau demandait plus de soin : il rangeait ses résultats par moitiés de tête, et une moitié de 512 valeurs pour huit têtes aurait rempli à elle seule les 32 Ko de mémoire rapide que le GPU du M2 accorde à chaque groupe de calcul. Il les range maintenant par tranches de 128, comme on vide un camion en plusieurs voyages quand la porte du garage est étroite.

Pour la lecture des prompts, c'est une autre affaire : le noyau qui lit un long texte d'un coup demanderait 64 Ko pour ces têtes, le double de ce qu'il y a. Les prompts de Gemma 4 restent donc lus par le processeur. Il faudra repenser ce noyau, et ce sera l'étape suivante.

## Le casier trop grand

En branchant le tout, une subtilité est apparue. Le cache, qui garde la mémoire du texte déjà lu, réserve pour chaque couche la place d'une tête de 512 valeurs, même pour les couches de 256. Les nombres y tiennent sans difficulté, mais leurs échelles, ces petits multiplicateurs qui accompagnent les valeurs rangées sur 8 bits, se trouvent après la place prévue pour 512. Les noyaux, eux, les cherchaient après 256. C'est ranger ses papiers dans un casier trop grand et chercher l'étiquette au milieu de la porte au lieu du bord. On donne maintenant aux noyaux l'endroit exact des échelles, et un test vérifie ce cas précis.

## Ce qu'apporte Gemma 4

Le reste tient en une liste de petites particularités, chacune ajoutée à la passe du GPU :

- vingt couches sur trente-cinq n'ont pas leurs propres clés : elles lisent celles d'une couche antérieure, comme un élève qui révise sur les notes de son voisin ;
- les valeurs passent par une norme, sans poids ;
- chaque couche reçoit en plus une petite entrée qui lui est propre, calculée à partir du mot ;
- la sortie de chaque couche est multipliée par une échelle ;
- les scores des mots sont plafonnés en douceur, à 30.

Le premier chargement a refusé le GPU pour une raison idiote : le contrôle des formats regardait aussi les matrices des clés des couches qui n'en ont pas. Une ligne a suffi.

## Le faux désaccord

Puis le contrôle au chargement a accepté le GPU, avec un écart de 0,00004 % sur les scores. Mais sur un long journal de 2 622 tokens, le GPU et le processeur divergeaient dès le neuvième mot de la réflexion : « the solution » d'un côté, « the desired answer » de l'autre. La réponse finale restait la même, mais l'écart arrivait trop tôt pour qu'on le classe comme une simple quasi-égalité.

On a donc fait afficher, à chaque mot, les deux meilleurs candidats et leurs scores. Le désaccord était bien réel : 17,97 contre 17,05 dès le deuxième mot. Pourtant le contrôle disait l'inverse. La clé était dans le contrôle lui-même : pendant qu'il s'exécute, le processeur calcule en nombres à virgule flottante, comme le GPU. En usage normal, il arrondit les nombres qui circulent entre les couches sur 8 bits, pour aller plus vite. La plupart des modèles ne s'en aperçoivent pas ; Gemma 4, si.

On a forcé le processeur à calculer en floats : il concorde alors avec le GPU à la quatrième décimale (17,04785 contre 17,04807), et les 150 mots écrits après le journal sont identiques, lettre pour lettre. Le GPU n'avait rien de faux. Sur ce modèle, il est même plus proche du calcul exact que le processeur dans son mode habituel. On a retiré les deux outils de l'enquête, qui n'avaient pas vocation à rester.

Reste un détail qui ne tient pas au portage : sur ce journal, Gemma 4 E2B trouve bien le mot de passe, PELICAN, mais il prend l'enregistrement 3 pour celui d'Inès. Il se trompe de la même façon sur le processeur et sur le GPU. C'est une erreur du modèle.

## Pour les curieux : les chiffres

- `test_metal` : écriture des clés et valeurs identique au processeur (0 valeur et 0 échelle différente sur 6 144 en têtes de 512, sur 3 072 en têtes de 256 dans un cache de 512) ; attention à 1,4e-7 au plus de la double précision.
- Contrôle au chargement de Gemma 4 E2B Q4_K_M : 4,1e-5 % à la première étape, 9,0e-5 % à la seconde, même mot.
- Prompt de 230 tokens, deuxième mot : processeur habituel 17,97, GPU 17,05, processeur en floats 17,05 (17,04785 contre 17,04807).
- Journal de 2 622 tokens, 150 tokens écrits : identiques entre le GPU et le processeur en floats.
- Le test complet d'Albireo gagne deux étapes (« gemma 4 on the GPU », « gemma 4 long prompt on the GPU ») : 53 en tout.
- Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Les prompts de Gemma 4 sur le GPU : un noyau de lecture à repenser pour les têtes de 512, sans dépasser la mémoire rapide du GPU. Et toujours le Mac mini, qui dira enfin à quelle vitesse tout cela écrit.
