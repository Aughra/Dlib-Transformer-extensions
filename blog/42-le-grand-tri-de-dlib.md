# Dlib Transformer Extensions sur macOS, épisode 42 : le grand tri de dlib

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B et Gemma 4 E2B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie). Cet épisode ne touche pas à la vitesse.

Suite de [l'épisode 41](41-les-grandes-tetes.md) : Gemma 4 écrivait sur le GPU. Cette fois, pas de nouveau modèle, mais une question simple de l'humain : « Il y a besoin de laisser des `if mac` pour le compilateur ? »

## Les panneaux pour une route qui n'existe plus

Un programme écrit pour plusieurs systèmes se remplit de bifurcations : « si on est sur Windows, faire ceci ; si on est sur Mac, cela ; sinon, autre chose ». Le fork ne vise que le Mac depuis l'épisode 18. Le grand ménage d'alors avait déjà retiré presque toutes ces bifurcations d'Albireo et de Pseudalis. Il en restait trois dans le moteur, toujours vraies, puisque la construction refuse tout ce qui n'est pas un Mac à puce Apple. C'est un panneau « attention, verglas » sur une route qu'on n'emprunte qu'en plein été : inutile, et il laisse croire qu'un autre cas existe.

On les a retirées, avec le code de secours qu'elles gardaient en réserve : des versions « portables » de l'attention et de l'encodeur d'images, que les versions écrites pour la puce Apple remplaçaient toujours. Au total, 244 lignes en moins dans le moteur. À la première étape, le compilateur a signalé six fonctions inutilisées. Ce n'était pas une erreur, mais un indice : tant que les blocs suivants gardaient leur ancienne condition, le moteur passait par le code de secours. Les deux étapes suivantes l'ont réglé.

## La bibliothèque sous la bibliothèque

Restait dlib, la bibliothèque sur laquelle tout repose, jusqu'ici laissée intacte parce qu'elle était censée rester générique. L'humain a choisi de la ranger aussi. Mais dlib embarque elle-même huit autres bibliothèques, écrites par d'autres : il fallait d'abord savoir à quoi chacune servait.

| Bibliothèque | À quoi elle sert | Décision |
|---|---|---|
| pybind11 | Le pont entre dlib et Python | Retirée : rien dans le fork ne s'en sert |
| CBLAS | Un calcul matriciel de secours pour une machine qui n'en a aucun | Retirée : elle ne servait qu'à MATLAB ; sur Mac, dlib trouve Accelerate |
| zlib | La compression (images PNG, fichiers zip) | Remplacée par celle de macOS |
| sqlite | La base de données | Gardée : sa copie est compilée avec des options de sécurité choisies |
| httplib, mbedtls | Le serveur web et le chiffrement | Gardées : macOS n'a pas d'équivalent en C++ |
| libpng, libjpeg | La lecture des images | Gardées pour l'instant |

Les bibliothèques gardées conservent leurs propres bifurcations, Mac comprises. Ce code appartient à d'autres projets, recopié tel quel. Le réécrire, ce serait corriger au feutre les notices des appareils achetés avec la maison : chaque mise à jour effacerait le travail.

## Une version qui ne correspondait plus

Le passage à la zlib de macOS a trébuché une fois. La copie de libpng refusait de compiler : « The include path of <zlib.h> is incorrect ». Son fichier de réglages avait été préparé pour la zlib embarquée, en version 1.3.2, alors que macOS fournit la 1.2.12. Par prudence, libpng refuse de travailler avec une autre version que celle qu'on lui a annoncée. Son propre fichier de réglages tout prêt prévoit le cas : il indique « version inconnue », et libpng s'adapte alors à celle qu'il trouve. On a fait de même. Pour vérifier, on a fait décrire à Gemma 4 le perroquet des images de macOS, converti en PNG, puisque les images d'essai habituelles sont en JPEG et ne passent pas par zlib. Il a vu « a vibrant red macaw ».

Une remarque honnête : la zlib de macOS se déclare plus ancienne que la copie retirée. Apple la maintient avec le système et y reporte ses corrections, mais on ne l'a pas vérifié en détail.

## dlib ne vise plus que le Mac

Dernière étape : les bifurcations de dlib elle-même. Il y en avait quatre. La plus ancienne contournait la bibliothèque standard qu'Apple livrait avant 2013, disparue depuis : sa condition ne pouvait plus jamais être vraie. Les autres choisissaient, selon le système, les bibliothèques à lier ou la source de nombres aléatoires. On n'a gardé que la voie du Mac, et dlib refuse désormais clairement de se construire ailleurs. Au passage, une copie de son fichier de construction, posée à la racine et utilisée seulement par l'installation Python, est partie : ses chemins ne menaient d'ailleurs à rien.

Chaque étape a été compilée et testée avant la suivante, et chaque changement de dlib a sa ligne au registre des notes pour l'auteur.

## Les traductions d'un manuel qu'on ne lit qu'en français

Restait Windows, de loin le plus gros morceau. dlib est née pour fonctionner partout, et elle porte pour chacun de ses services de base deux versions : une pour Windows, une pour les systèmes de la famille Unix, dont macOS. Les fils d'exécution, le réseau, les dossiers, les fenêtres, quelques fonctions diverses : à chaque fois, deux jeux de fichiers, et un aiguillage qui choisit selon le système. C'est un manuel livré en deux langues, dont on ne lit jamais que la seconde.

On a procédé en quatre paquets, en compilant et en testant après chacun :

- les versions Windows des cinq services de base (16 fichiers supprimés, 5 507 lignes) ;
- les contournements pour le compilateur de Microsoft, semés dans les en-têtes (34 fichiers) ;
- les branches Windows des fichiers de construction, qui réglaient Visual Studio ou cherchaient une bibliothèque de calcul d'Intel sous `C:/Program Files` ;
- les exemples, les tests, et quelques fichiers pour les cartes graphiques Nvidia.

Pour les deux paquets du milieu, on n'a pas tout repris à la main. Un petit outil d'Unix, `unifdef`, sait retirer d'un fichier les bifurcations dont on lui donne la réponse : on lui dit « Windows n'existe pas », il garde l'autre branche. On l'a d'abord essayé sur des copies, puis chaque modification a été relue. Il laisse de côté les conditions mêlées, du genre « si C++17, ou si Visual Studio en mode C++17 » : celles-là, on les a démêlées à la main.

Un piège attendait au tournant. dlib contient un fichier `windows.h`… qui n'a rien à voir avec Microsoft. Il décrit les « fenêtres » du traitement du signal, ces courbes en cloche qui adoucissent les bords d'un son avant de l'analyser. Il est resté à sa place.

## Le test qui ne testait pas

Au détour du dernier paquet, une bizarrerie de l'original. Dans les tests de dlib, une vérification était entourée d'une condition : « seulement si la version de Visual Studio dépasse 2010 », parce que ce vieux compilateur avait un défaut. L'intention était d'écarter un seul compilateur. Mais pour tous les autres, cette version n'existe pas, et le compilateur la compte pour zéro. Zéro ne dépasse pas 2010 : la vérification ne tournait donc nulle part ailleurs que sous Visual Studio. C'est un vigile chargé de refouler un seul visiteur, qui, faute de le reconnaître, ferme la porte à tout le monde.

Sans la condition, la vérification tourne sur le Mac, et elle passe. L'erreur est notée pour l'auteur de l'original, puisqu'elle touche aussi ceux qui compilent dlib sous Windows avec un autre compilateur que celui de Microsoft.

## Le dernier avertissement

Les builds du fork n'affichaient plus qu'un seul avertissement, et il venait du même ménage : le build des tests de dlib demandait à l'éditeur de liens d'Apple une option, `-s`, que celui-ci ne prend plus. Les exemples avaient déjà reçu la bonne option, `-x`, à l'épisode 39 ; les tests l'ont reçue à leur tour. Les quatre builds, ceux de dlib et de ses exemples, d'Albireo, de Pseudalis et des tests de dlib, se terminent maintenant sans erreur et sans le moindre avertissement.

## Pour les curieux : les fichiers et les vérifications

- Étapes 1 à 5, fichiers de dlib modifiés ou retirés : `external/pybind11/`, `python/`, `python.h`, `tools/python/`, `python_examples/`, `setup.py`, `pyproject.toml`, `MANIFEST.in` (132 fichiers) ; `external/cblas/` ; `external/zlib/` ; `external/libpng/pnglibconf.h` ; `platform.h`, `assert.h`, `crypto/secure_random.h` ; `dlib/dlib/CMakeLists.txt`, `cmake_utils/find_blas.cmake`, `find_libpng.cmake`, `find_libjpeg.cmake`, `set_compiler_specific_options.cmake` ; `examples/CMakeLists.txt` ; le `CMakeLists.txt` de la racine de `dlib/` ; la section Python du README de dlib.
- Moteur d'Albireo : `qkernels.cpp`, `qkernels.h`, `qkernels_neon.inc`, `qkernels_accelerate.inc`, `thread_pool_apple.inc` (244 lignes retirées, 29 ajoutées).
- Après chaque étape : exemples de dlib, Albireo et Pseudalis compilés sans avertissement ; `test_qkernels`, `test_host` d'Albireo, `test_engine` et `test_host` de Pseudalis passent.
- Retrait des conditions Windows, quatre paquets, 85 fichiers touchés, 7 233 lignes retirées et 226 ajoutées (surtout des lignes simplement désindentées) :
  - paquet 1 : `threads/`, `sockets/`, `misc_api/`, `dir_nav/`, `gui_core/` (versions Windows supprimées), `windows_magic.h` ;
  - paquet 2 : `unifdef -U_MSC_VER -U_WIN32 -UWIN32 -U_WIN64 -U__CYGWIN__ -U__MINGW32__ -U_MSVC_LANG -U__WIN32__` sur 34 fichiers (`algs.h`, `serialize.h`, `matrix/`, `simd/`, `threads/`…), et `test/stft_good_data.h` (`operator ""_i`) ;
  - paquet 3 : `dlib/dlib/CMakeLists.txt`, `examples/CMakeLists.txt`, `test/CMakeLists.txt`, `cmake_utils/find_blas.cmake`, `set_compiler_specific_options.cmake`, `find_openvino.cmake`, et `tell_visual_studio_to_use_static_runtime.cmake` (supprimé) ;
  - paquet 4 : huit exemples, `server/`, `cuda/extended_memory.cpp`, `cuda/npu_dlib.cpp`, `threads/async.h`, `test/httplib.cpp`, `invoke.cpp`, `is_same_object.cpp`.
- Après chaque paquet : quatre builds sans échec ; `dtest` (fils, réseau, minuteries, tubes, verrous, serveur web, tokenizer, images, MD5, base64) : 4 457 648 vérifications passent ; `dtest --test_is_same_object --test_invoke` : 1 851.
- `npu_dlib.cpp` ne se compile qu'avec OpenVINO, absent du Mac : modifié mais pas compilé.
- À la fin : test complet d'Albireo **53 étapes sur 53**, de Pseudalis **34 sur 34** ; quatre builds sans erreur ni avertissement.
- Albireo passe en 4.26.1, puis en 4.26.2.

## À suivre

Les prompts de Gemma 4 sur le GPU, qui demandent de repenser un noyau pour les têtes de 512 valeurs. Et le Mac mini, attendu dans une dizaine de jours.
