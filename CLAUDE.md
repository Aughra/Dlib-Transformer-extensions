# CLAUDE.md — Dlib-Transformer-extensions (fork Mac)

## Langue et communication

- **Répondre exclusivement en français, sur un ton doux et concis.**
- Expliquer simplement : chaque mot technique avec une comparaison de la vie courante, comme pour un ado. Sections courtes, tableaux quand ça aide.
- Expliquer brièvement l'intention d'une modification avant d'en montrer les détails, et nommer les fichiers et sections exacts qu'elle touche.
- Les messages de commit sont en français. Les fichiers propres au fork (ce fichier, `logs/`, le mémo de portage) sont en français ; le blog est en français seulement ; le code et les documents de l'original restent en anglais, comme leur auteur les écrit.
- Si une question est ambiguë, poser **une seule** question de clarification, précise.
- Quand il y a plusieurs options, les présenter avec leurs pour et leurs contre, et dire laquelle est recommandée.
- Pas de flatterie, pas de résumé de la question avant de répondre.

---

## Le projet

Ce dépôt est un **fork** de [`Cydral/Dlib-Transformer-extensions`](https://github.com/Cydral/Dlib-Transformer-extensions) : dlib, des extensions Transformer (modèles de langage, GGUF, LoRA, distillation, vision) et deux applications Windows 11 construites dessus, **Albireo** (moteur GGUF et service de chat) et **Pseudalis** (anonymisation de documents).

**But du fork : faire tourner le projet sur Mac Apple Silicon (arm64), et seulement là.** Machine actuelle : Mac M2 (8 cœurs, 24 Go). Machine prévue : Mac mini M5 Pro, 64 Go. Windows, MSVC, les processeurs Intel/AMD et CUDA sont hors du périmètre : ne pas chercher à les garder en état de marche dans nos changements, sans pour autant les casser quand rien ne l'impose.

Les solutions propres au Mac sont permises (instructions NEON, `__fp16`, Accelerate, Metal, un standard C++ plus récent). Leur coût, c'est l'écart avec l'original : le signaler une fois quand on propose une telle solution.

### Deux dépôts distants

| Nom | Dépôt | Rôle |
|---|---|---|
| `origin` | `PapyTortueTech/Dlib-Transformer-extensions` | Le fork, où l'on pousse. |
| `upstream` | `Cydral/Dlib-Transformer-extensions` | L'original, qui continue d'avancer ; on ne fait qu'y lire. |

### Rester facile à fusionner

L'original reçoit souvent de gros commits. Chaque ligne que le fork change dans un fichier de l'original peut devenir un conflit à la prochaine fusion. Donc :

- **Préférer un fichier neuf** à la modification d'un fichier de l'original.
- Quand il faut toucher un fichier de l'original, faire le **plus petit changement possible**, de préférence sous une condition qui ne s'applique qu'au Mac (`if (APPLE)` dans CMake, `#ifdef __APPLE__` dans le code) plutôt qu'en réécrivant la version Linux ou Windows.
- Ne jamais reformater, renommer ou réorganiser du code de l'original sans nécessité.
- Ne pas toucher à `dist/` (installateurs et flux de mises à jour signés de l'original) ni aux scripts Windows (`*.ps1`, `windows/`).

---

## Construire sur Mac

La référence des commandes et des résultats est [docs/memo-portage-macos-arm64.md](docs/memo-portage-macos-arm64.md). En bref :

- **dlib + exemples** : `cmake -S dlib/examples -B build/macos-arm64-cpu` (options CPU seules, voir le mémo), puis `cmake --build build/macos-arm64-cpu`.
- **Tests dlib** : `cmake -S dlib/dlib/test -B build/macos-arm64-tests`, puis `build/macos-arm64-tests/dtest`.
- **Albireo / Pseudalis** : `cmake -S dlib/dlib/external/<app> -B build/macos-<app> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64`.

Tous les dossiers de build vont sous `build/`, que `.gitignore` ignore déjà. Un dossier par configuration. Avec Ninja, `-- -k 0` relève toutes les erreurs en un seul passage. Les compilations longues tournent en arrière-plan.

Outils présents sur le Mac : AppleClang (Xcode), CMake et Ninja (Homebrew), `gh`.

---

## Méthode de travail

- **Au début de chaque session : lire [logs/reprise.md](logs/reprise.md) en premier**, puis lancer `git fetch upstream` et dire combien de commits l'original a reçus depuis la dernière fusion.
- Énoncer ses hypothèses et traduire la demande en **critère vérifiable** avant de coder (par exemple « `cmake --build` finit sans erreur et `test_host` affiche *all tests passed* »).
- Écrire le minimum qui résout le problème. Ne pas toucher aux fichiers hors du sujet de la demande.
- Signaler toute dépendance ajoutée, et pourquoi.
- Ne déclarer « ça marche » que ce qui a été vérifié sur le Mac. Ce qui a compilé sans avoir été lancé se dit « compilé, pas testé ».
- Les analyses techniques vont au fond : dire clairement quand une architecture a un défaut.

### Sécurité

- Ne jamais afficher, journaliser ni commiter de secrets : clés (`~/keys/`, clés privées des flux de mises à jour), jetons, mots de passe.
- Ne pas regénérer ni re-signer les fichiers `*.sig` et les clés publiques de l'original.
- Moindre privilège dans toute proposition d'architecture ; signaler tout anti-pattern de sécurité repéré.

---

## Reprise, journal et documentation

### `logs/reprise.md` : l'état vivant du chantier

[logs/reprise.md](logs/reprise.md) dit où en est le chantier : objectif, fait, en cours, à faire, décisions, problèmes ouverts, commandes pour reprendre. Il est **réécrit** en continu, et son but est qu'une coupure (session fermée, contexte perdu, changement de machine) ne fasse jamais perdre plus d'une sous-étape.

- **Le mettre à jour à chaque fin de sous-étape**, avant de passer à la suivante.
- **Le lire en premier** au début de chaque session.

### `logs/journal-modifications.md` : l'historique

Un seul fichier, où chaque intervention **ajoute une entrée en tête** (le plus récent en haut), sans réécrire les précédentes :

```markdown
## AAAA-MM-JJ — <titre court à l'impératif>

- **Quoi** : résumé du changement.
- **Fichiers** : chemins touchés.
- **Pourquoi** : intention.
- **Étapes** : actions réalisées (build, tests, commit…).
```

Ce fichier-ci (CLAUDE.md) décrit l'**état actuel** des règles et du projet : quand une section devient fausse, on la réécrit. Les dates et l'historique vont au journal.

### La documentation suit le code

Documents du fork qui doivent toujours rester **vrais** :

- [docs/memo-portage-macos-arm64.md](docs/memo-portage-macos-arm64.md) : commandes de build pour Mac et section « État constaté » (ce qui compile, ce qui passe les tests, les problèmes restants) ;
- ce fichier CLAUDE.md ;
- [logs/reprise.md](logs/reprise.md).

**À la fin de chaque sous-étape**, avant de la déclarer terminée : relire ces documents et corriger ce que le changement rend faux ou incomplet. Une doc qui décrit un état dépassé est un défaut, au même titre qu'un test qui ment. Critère vérifiable : aucune commande, option, cible ou résultat cité dans ces documents ne contredit le code ni le dernier build.

Les documents de l'original (`Readme.md`, `examples/Readme.md`, les README d'Albireo et de Pseudalis) ne sont modifiés que si un changement du fork les rend faux, et le plus légèrement possible (règle de fusion).

### Le blog du portage (une page par grande étape, en français)

Le blog raconte le portage **comme un roman**, page après page, pour un lecteur qui découvre le projet. Il vit dans [blog/](blog/), un dossier propre au fork (aucun conflit avec l'original). Claude ne publie rien en ligne : l'humain décide où et quand.

- **Une page par grande étape, en français seulement** : `blog/NN-nom.md`, numérotée dans l'ordre du récit. Est une grande étape : un programme ou une application qui compile ou tourne pour la première fois sur Mac, une décision de portage importante, une fusion de l'original qui change la donne, un tournant. Une étape neuve, c'est une page neuve : on ne corrige une ancienne page que pour une erreur de fait.
- **Écrite dans le même commit que la fin de l'étape**, avec l'entrée du journal. Ne jamais laisser une grande étape sans sa page.
- **Le sommaire** [blog/README.md](blog/README.md) liste chaque page.
- **Récit d'abord** : à la première personne (Claude), avec l'humain comme l'autre voix du récit (ses demandes, ses questions, ses corrections sont des scènes). Dans le blog, l'utilisateur s'appelle **« l'humain »**, jamais par son nom ni par celui de ses comptes ; un extrait de log qui contient un nom de compte est raccourci à la ligne utile. Le ton concis du reste du projet ne s'applique pas ici ; l'exactitude, si.
- **Chaque page se termine par un « Carnet de bord »** (citation Markdown `>`) qui consigne, sans rien embellir : les **résultats vérifiés** (chiffres tirés du journal, des commits ou des sorties de build), les **choix** de Claude et pourquoi, les **questions** posées à l'humain et **ses réponses** (fidèles), les **difficultés et erreurs** (celles de Claude comprises), et les **conseils** qu'on en tire.
- **Le code montré est réel** : un extrait de code, de log ou de commande est copié du dépôt ou d'une sortie réelle, jamais inventé ; la page nomme le commit ou le fichier d'où il vient.
- **Aucune donnée personnelle ni secret** : pas d'adresse e-mail, de nom de compte, de chemin privé (`/Users/...`), de clé ni de jeton.
- Critère vérifiable : chaque page `blog/NN-*.md` figure au sommaire, et aucune ne contient de nom de compte, d'adresse e-mail ni de chemin `/Users/`.

### Autorité

CLAUDE.md est le document d'autorité du fork. En cas de contradiction avec un autre document, la signaler puis la résoudre ici.

---

## Git

- On travaille sur `main`. Pour garder un état, poser un tag (`archive/<sujet>-<date>`).
- **Commit de soi-même** après chaque unité de travail cohérente et **vérifiée** (build ou test réussi), avec son entrée dans le journal. Messages en français, concis, à l'impératif : le *quoi* et le *pourquoi*.
- **Push vers `origin` seulement**, après chaque commit. Jamais de push vers `upstream`, jamais de `--force`, jamais de PR vers l'original sans demande explicite.
- **Fusionner l'original** (`git fetch upstream` puis `git merge upstream/main`) seulement à la demande. En cas de conflit, s'arrêter, montrer les deux versions et expliquer le choix à faire.
- Ne jamais commiter `build/`, les modèles `.gguf` ni les `.dat` (déjà ignorés).
