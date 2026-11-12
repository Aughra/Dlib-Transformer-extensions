# Dlib Transformer Extensions sur macOS, épisode 33 : le carnet ouvre les yeux

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Qwen3.5-4B : écriture **18 à 19 tokens/s**, encodage d'une image de 512 × 512 points en **1,7 s** ; Qwen 3.6 11B-A3B : lecture **196 à 198 tokens/s** sur 512 tokens.

Suite de [l'épisode 32](32-le-comite-se-reunit.md) : le moteur lisait vite les mélanges d'experts. Deux promesses de Qwen 3.5, le modèle au carnet de notes de [l'épisode 26](26-le-carnet-de-notes.md), attendaient encore : se servir d'outils, et regarder des images.

## Deux façons d'écrire une commande

En mode agent, le modèle peut appeler des outils : une calculatrice, une recherche dans les documents, une date. Il écrit alors une sorte de bon de commande, que le moteur doit reconnaître et transmettre. Qwen 3 écrit ses bons en JSON, une notation d'informaticien faite d'accolades. Qwen 3.5, lui, les remplit en XML, avec des étiquettes : `<function=get_weather>`, puis `<parameter=city>` et la valeur.

Le moteur attendait du JSON de tous les modèles de cette famille. Avec Qwen 3.5, il voyait passer des bons de commande qu'il ne savait pas lire, et le mode agent ne marchait pas. C'est comme un secrétariat qui ne traite que les formulaires bleus et laisse les verts sur la pile. Ce défaut était noté « non vérifié » depuis l'épisode 26 ; en vérifiant, on l'a trouvé.

La correction a consisté à reprendre le lecteur de formulaires XML que le moteur avait déjà pour un autre modèle, Granite 4.2, et à écrire le prompt avec outils exactement comme le prévoit le gabarit de Qwen 3.5. Exactement veut dire à l'octet près : sur trois conversations, dont une avec deux appels et leurs deux résultats, le prompt d'Albireo et celui que produit le gabarit d'origine sont identiques. Et à la question « quel temps fait-il à Paris ? », le Qwen3.5-4B remplit son bon, reçoit « nuageux, 14 degrés » et s'en sert dans sa réponse.

## Des lunettes déjà rangées dans le tiroir

Le dépôt du Qwen3.5-4B annonce un modèle « image-texte » : il lit aussi les images, à condition qu'on lui donne son projecteur, le petit modèle qui traduit une image en mots qu'il comprend. En ouvrant ce projecteur, une bonne surprise : il est du même type que celui de Qwen3-VL, que le moteur lit depuis longtemps. Les lunettes étaient dans le tiroir.

Il manquait un détail : la façon de situer chaque morceau d'image. Qwen3-VL repère un mot par sa place dans le texte, mais un morceau d'image par trois coordonnées, le temps, la ligne et la colonne, comme une case d'échiquier. Le fichier de Qwen 3.5 déclare ces trois coordonnées ; le moteur ne les lisait que pour Qwen3-VL. Il les lit maintenant aussi pour Qwen 3.5, et seulement quand le fichier les déclare, pour qu'un modèle de texte seul reste un modèle de texte.

## Le même regard que la référence

Pour vérifier, on a montré deux images à Albireo et à llama.cpp, la référence, sur le processeur et en prenant toujours le mot le plus probable :

| Image | llama.cpp | Albireo |
|---|---|---|
| Perroquet | « This is a close-up portrait of a scarlet macaw, showcasing its vibrant red plumage, pale facial skin, and large, curved yellow beak. » | identique, mot pour mot, jusqu'au bout |
| Zèbre | « …against a soft-focus background of golden grasses and a pale blue sky. » | identique jusqu'à « grass » au lieu de « grasses », puis le même sens |

Le second écart tient à une quasi-égalité entre deux mots, comme on en a déjà vu : le moteur et la référence ne calculent pas dans le même ordre.

En chemin, un oubli a été corrigé : quand un long prompt est coupé aux marque-pages de [l'épisode 29](29-les-marque-pages.md), les coordonnées des mots n'étaient pas transmises aux morceaux. Pour du texte seul, cela ne changeait rien. Après une image, cela aurait pu en changer.

## Pour les curieux : les chiffres

- Projecteur : `mmproj-F16.gguf` du dépôt unsloth/Qwen3.5-4B-GGUF, 672 Mo, 24 blocs, images de 768 points au plus, empreinte SHA-256 conforme à celle de Hugging Face.
- Prompt d'une question avec une image de 512 × 512 points : 277 tokens, dont 256 pour l'image (16 × 16), des deux côtés.
- Encodage de l'image : 1,7 s ; lecture du prompt : 55 à 61 tokens/s (l'image passe par le processeur) ; écriture : 18 à 19 tokens/s sur le GPU.
- Le test complet d'Albireo compte maintenant 29 étapes, dont « qwen 3.5 tool call » et « qwen 3.5 image », qui montre au modèle le perroquet des images de compte de macOS.

## À suivre

Les Qwen 3.6 à mélange d'experts ont eux aussi un projecteur : on l'essaiera avec le grand modèle, sur le Mac mini.
