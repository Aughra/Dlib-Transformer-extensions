# Dlib Transformer Extensions sur macOS, épisode 44 : le passe-partout et la photo de groupe

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B, Gemma 4 E2B sur le GPU et les images de Gemma 3 : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 43](43-la-tete-en-deux-tranches.md) : Gemma 4 lisait enfin ses prompts sur le GPU. Restait une case vide dans le tableau des modèles : Gemma 3 lisait le texte depuis l'épisode 37, mais pas les images.

## Pourquoi le petit frère

La question s'est posée avant de commencer : à quoi bon faire voir les images à Gemma 3, quand Gemma 4, plus récent et sans doute plus rapide, les voit déjà ? La réponse tient à sa descendance. Beaucoup de modèles spécialisés sont construits sur Gemma 3, comme on décline une même voiture en taxi ou en ambulance : MedGemma pour la médecine, et une foule de variantes publiées par la communauté. Les faire voir, c'est apprendre la méthode de Gemma 3 une fois pour toutes. Et le tuyau des images existait déjà dans Albireo : il ne manquait que l'interprète.

## Le passe-partout

Chaque famille de modèles a sa façon de regarder une image. Qwen la découpe en carreaux de taille variable ; Gemma 4 garde sa forme et choisit le nombre de carreaux selon le détail demandé. Gemma 3 est plus simple : il regarde toujours un carré de 896 pixels de côté. Une photo en largeur y est réduite jusqu'à toucher les bords, et le vide au-dessus et en dessous est rempli de noir, comme le passe-partout d'un cadre qui entoure une photo trop petite pour lui.

Le carré est ensuite découpé en 64 × 64 petits carreaux de 14 pixels, que lit un encodeur de 27 couches, puis les carreaux sont moyennés par paquets de 4 × 4. Il en sort toujours 256 « mots-image », quelle que soit la photo, que le modèle lit entre deux balises, `<start_of_image>` et `<end_of_image>`.

Pour savoir exactement comment s'y prendre, on n'a pas deviné : on a lu le code de llama.cpp, la référence de tous les essais du portage, au commit même de la version installée sur le Mac. La taille du cadre, l'arrondi du côté qui ne remplit pas le carré (vers le haut), le filtre de redimensionnement (bilinéaire), la couleur des bandes (noir) : tout vient de là.

## La photo de groupe

Le code de llama.cpp a révélé une particularité qu'on n'attendait pas. D'habitude, un modèle de langage lit comme on fait la queue : chaque mot ne voit que ceux qui le précèdent, jamais ceux qui suivent. C'est ce qui lui permet d'écrire un mot après l'autre. Mais pour les 256 mots-image de Gemma 3, llama.cpp lève cette règle : ils se voient tous les uns les autres, comme sur une photo de groupe où chacun voit tout le monde. C'est logique : une image n'a ni début ni fin, et le carreau du coin en haut à gauche n'a pas de raison de ne pas voir celui d'en bas.

Le moteur d'Albireo ne savait pas faire ça. Il a fallu lui apprendre trois choses :

- le lot de mots envoyé au moteur porte maintenant des « plages » dont les mots se voient entre eux ;
- dans ces plages, le calcul de l'attention laisse chaque mot regarder jusqu'au dernier mot de l'image, et non jusqu'à lui-même ;
- une image n'est jamais coupée entre deux passes du moteur, même quand il reprend une conversation déjà à moitié calculée : la photo de groupe se prend en une fois, pas en deux moitiés.

Pour vérifier que cela servait vraiment, on l'a désactivé le temps d'un essai. La description du perroquet s'écarte alors de celle de llama.cpp dès le quatorzième mot : « This close-up photograph » au lieu de « The image showcases ». Remis en place, les deux descriptions redeviennent identiques.

## Deux décodeurs, deux photos

La comparaison avec llama.cpp s'est faite sur quatre images : le perroquet des images de compte de macOS en carré de 512 pixels, un zèbre recadré en largeur, le même en hauteur, et le perroquet en grand, 1 600 × 1 000. Le nombre de mots du prompt est le même partout : 274. Sur le perroquet carré, la description est identique mot pour mot ; sur le zèbre en largeur, pendant 59 mots.

Sur les deux autres, l'écart venait plus tôt, et franchement : à un endroit où llama.cpp écrivait « shows » avec 62 % de certitude, Albireo écrivait « features ». L'enquête a trouvé un coupable inattendu : le décodage du JPEG. Une photo JPEG est compressée, et deux bibliothèques ne la décompressent pas exactement de la même façon ; Albireo utilise libjpeg, llama.cpp une autre, stb_image. Les pixels diffèrent d'un niveau de couleur ici et là, invisible à l'œil, mais ce modèle y est sensible : llama.cpp lui-même ne décrit pas pareil la même photo selon qu'elle est en JPEG ou en PNG. Avec les images converties en PNG, un format sans perte que tout le monde décode à l'identique, les deux écarts tombent sur des quasi-égalités de la référence : deux mots à 44 % et 42 %, puis à 47 % et 37 %.

Une limite assumée : une passe qui contient une image reste sur le processeur, comme pour les autres familles. L'encodage d'une image prend aujourd'hui une trentaine de secondes, mais le Mac est en mode économie d'énergie, où les temps ne veulent rien dire.

## Pour les curieux : les chiffres et les fichiers

- Projecteur : `mmproj-F16.gguf` du dépôt unsloth/gemma-3-4b-it-GGUF, 851 251 328 octets, SHA-256 `731199e0…` conforme ; type `gemma3`, 27 blocs de largeur 1 152, 16 têtes, patchs de 14 pixels, vue de 896 pixels, sortie de 2 560 valeurs.
- Encodeur : `engine/vision_encoder.{h,cpp}`, famille `gemma3` : vue carrée (`gemma3_view`, la règle `PAD_CEIL` de `img_tool::resize` dans `mtmd-image.cpp`), blocs SigLIP sans rotation, norme `v.post_ln`, moyenne 4 × 4, norme `mm.soft_emb_norm`, projection `mm.input_projection`, rangée transposée dans le fichier ([2560, 1152]) et remise à l'endroit au chargement.
- Moteur : `forward_batch::bidirectional` (`engine/runtime.h`), la dernière position que voit chaque token (`reach_`, `engine/cpu_runtime.cpp`), une image lue en une passe et jamais reprise en son milieu (`engine/engine.cpp`) ; la règle vient de `mtmd_decode_use_non_causal` dans `mtmd.cpp`.
- Comparaison : `llama-server -ngl 0 --no-mmproj-offload --jinja --mmproj …`, llama.cpp 0.9.0 (commit `cd959bae9`), choix glouton, « Describe this image in two sentences. », clés et valeurs en demi-précision des deux côtés (en 8 bits, Albireo s’écarte au 39e token du perroquet carré : « facial » au lieu de « feather »).
- Test complet d'Albireo : deux étapes nouvelles, « gemma 3 image » (« scarlet macaw ») et « gemma 3 wide image » (« zebra »).
- Albireo passe en **4.28.0**. Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

L'autre chantier demandé en même temps : Gemma 4 26B-A4B, un « mélange d'experts » de 26 milliards de nombres dont 4 seulement travaillent pour chaque mot, le modèle taillé pour le futur Mac mini.
