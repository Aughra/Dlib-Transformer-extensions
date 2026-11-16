# Consignes de l'humain : backend Metal pour les tenseurs de dlib (phases 1 à 8 sur 8)

> Reçues le 2026-11-16, à intégrer **une par une**, une fois terminée la liste en cours (pistes A à F, « en attendant le Mac mini »). Recopiées telles que l'humain les a données. La phase 1 commence par un plan court **soumis à sa validation avant toute modification**.

---

## Phase 1 sur 8

Contexte : fork de Dlib (Transformers C++14, import GGUF, entraînement, LoRA, distillation). Objectif global : un backend Metal pour Apple Silicon uniquement. Cette session ne porte que la phase 1 sur 8.

Tâche :
1. Lis l'arborescence, le système de build (CMake) et les abstractions de tenseur existantes. Présente un plan court et attends ma validation avant de modifier quoi que ce soit.
2. Après validation : ajoute une étiquette de dispositif (CPU ou Metal) aux tenseurs et une couche de dispatch qui choisit le chemin d'exécution.
3. Ajoute le squelette Metal : MTLDevice, MTLCommandQueue, buffers en storageModeShared. Les fichiers .mm doivent compiler dans le build existant, sous condition de plateforme Apple.
4. Le chemin CPU reste inchangé et ses tests existants passent.

Contraintes :
- Pas de copie hôte-dispositif systématique.
- Pas de repli silencieux sur CPU : erreur explicite ou journal clair.
- Ne copie aucun code d'un autre projet. Garde les interfaces publiques de Dlib stables.

Critère de fin :
- Compilation sur macOS Apple Silicon, tests CPU verts.
- Un test crée un tenseur Metal et relit ses valeurs.
- Le rapport indique ce qui est couvert par un test et ce qui ne l'est pas.

---

## Phase 2 sur 8

Contexte : fork de Dlib, backend Metal. Phase 1 terminée (dispositif, squelette Metal, CPU intact). Cette session ne porte que la phase 2 sur 8.

Tâche :
1. Porte en Metal : addition élémentaire, multiplication élémentaire, softmax, activations GELU et SiLU.
2. Pour chaque opération, un test de parité : entrées aléatoires à graine fixée, résultat CPU de référence, résultat Metal, comparaison avec une tolérance documentée (point de départ : 1e-5 en absolu et en relatif sur float32, à justifier).
3. Passe à l'opération suivante seulement si le test de la précédente est vert.

Contraintes :
- Un kernel en échec lève une erreur explicite.
- Ne copie aucun code d'un autre projet ; écris les kernels à partir de la documentation Metal.
- Pour chaque kernel, indique les formes supportées et celles qui lèvent une erreur.

Critère de fin :
- Tous les tests de parité de cette phase passent, les tests CPU aussi.
- Le rapport liste, par opération, la tolérance, l'écart maximal mesuré et les formes non supportées.

---

## Phase 3 sur 8

Contexte : fork de Dlib, backend Metal. Phases 1 et 2 terminées. Cette session ne porte que la phase 3 sur 8.

Tâche :
1. Porte le produit matriciel float32 en Metal. Choisis entre MPSMatrixMultiplication et un kernel maison, en justifiant le choix dans le plan.
2. Test de parité sur plusieurs formes : carrées, rectangulaires, petites et grandes dimensions, transposition si Dlib la gère.
3. Mesure le temps d'exécution CPU et Metal sur trois tailles, avec la méthode utilisée (nombre de répétitions, échauffement).

Contraintes :
- Pas de repli silencieux.
- Ne copie aucun code d'un autre projet.

Critère de fin :
- Tests de parité verts sur toutes les formes testées.
- Le rapport donne les temps mesurés, la méthode, et les formes non couvertes.

---

## Phase 4 sur 8

Contexte : fork de Dlib, backend Metal. Phases 1 à 3 terminées. Cette session ne porte que la phase 4 sur 8.

Tâche :
1. Porte la normalisation de couche (LayerNorm) en Metal, avec epsilon et paramètres gamma et bêta comme dans le chemin CPU de Dlib.
2. Test de parité sur plusieurs tailles de dimension, y compris des dimensions qui ne sont pas des multiples de la largeur de groupe du GPU.
3. Vérifie que les tests des phases précédentes passent toujours.

Contraintes :
- Pas de repli silencieux.
- Ne copie aucun code d'un autre projet.

Critère de fin :
- Tests de parité LayerNorm verts sur toutes les tailles.
- Le rapport liste les tailles testées et l'écart maximal mesuré.

---

## Phase 5 sur 8

> Reçue le 2026-11-16, juste après les phases 1 à 4. Son contexte dit les phases 1 à 4 terminées : elles ne l'étaient pas encore à sa réception (pas même commencées). Elle vient après elles.

Contexte : fork de Dlib, backend Metal. Phases 1 à 4 terminées : dispositif, opérations élémentaires, produit matriciel, LayerNorm, tous testés en parité. Cette session ne porte que la phase 5 sur 8.

Tâche :
1. Implémente l'attention en version naïve, en assemblant les kernels validés : score QK^T mis à l'échelle, softmax, produit par V. Prévois le masque causal si Dlib l'utilise.
2. Test de parité contre le chemin CPU, sur plusieurs longueurs de séquence et nombres de têtes.
3. Ne fais aucune version par tuiles dans cette phase ; liste-la comme suite.

Contraintes :
- Pas de repli silencieux.
- Ne copie aucun code d'un autre projet.

Critère de fin :
- Tests de parité de l'attention verts.
- Le rapport précise les formes supportées, le masque traité ou non, et ce qui reste (tuiles, performances).

---

## Phase 6 sur 8

> Reçue le 2026-11-16 (avec un nouvel envoi de la phase 5, déjà notée plus haut). Même remarque : son contexte dit les phases précédentes terminées ; elle vient après elles.

Contexte : fork de Dlib, backend Metal. Phases 1 à 5 terminées. Cette session ne porte que la phase 6 sur 8.

Tâche :
1. Lis dans le code d'import GGUF existant le format des blocs Q4_0 et Q8_0 (blocs de 32 valeurs, facteur d'échelle par bloc).
2. Écris un kernel qui dequantifie à la volée pendant le produit matriciel, pour ne pas matérialiser les poids en float32 au chargement.
3. Test de parité avec la dequantification CPU existante. La tolérance est plus large que pour float32 ; justifie-la dans le rapport.
4. Mesure la mémoire occupée par un petit modèle en Q4 contre sa version float32, avec la méthode utilisée.

Contraintes :
- Si le GGUF contient un cas non géré, lève une erreur claire plutôt que de deviner.
- Pas de repli silencieux. Ne copie aucun code d'un autre projet.

Critère de fin :
- Tests de parité Q4_0 et Q8_0 verts.
- Mesure de mémoire chiffrée avec sa méthode.
- Le rapport liste ce qui n'est pas couvert : autres formats GGUF, performances.

---

## Phase 7 sur 8

Contexte : fork de Dlib, backend Metal. Phases 1 à 6 terminées : toutes les opérations forward sont portées et testées. Cette session ne porte que la phase 7 sur 8.

Tâche :
1. Pour chaque opération forward déjà portée, écris la passe arrière en Metal.
2. Test de gradient : comparaison au gradient CPU, et vérification par différences finies sur quelques éléments choisis.
3. Si une passe arrière n'est pas implémentée, lève une erreur explicite. Ne renvoie jamais de zéros silencieusement.

Critère de fin :
- Tous les tests de gradient verts, y compris les différences finies.
- Le rapport liste les opérations sans passe arrière.

---

## Phase 8 sur 8

Contexte : fork de Dlib, backend Metal. Phases 1 à 7 terminées : forward et backward testés. Cette session ne porte que la phase 8 sur 8.

Tâche :
1. Porte SGD puis Adam en Metal, avec test de parité sur plusieurs pas.
2. Sur un petit modèle, compare la trajectoire de perte Metal et CPU avec la même graine, sur quelques dizaines de pas.
3. Ne porte ni LoRA ni la distillation ; liste-les comme suite.

Contraintes :
- Pas de repli silencieux. Ne copie aucun code d'un autre projet.

Critère de fin :
- Tests d'optimiseurs verts.
- Trajectoires Metal et CPU concordantes à la tolérance documentée.
- Le rapport liste ce qui reste : LoRA, distillation, performances, grands modèles.
