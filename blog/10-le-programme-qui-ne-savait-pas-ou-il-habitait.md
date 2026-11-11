# Dlib Transformer Extensions sur macOS, épisode 10 : le programme qui ne savait pas où il habitait

> **Repère de vitesse** (MacBook Air M2) : écriture **33 à 37 tokens/s** (2B), **14 tokens/s** (4B, le modèle du service) ; lecture 35 à 50 tokens/s (2B).

Suite de [l'épisode 9](09-le-dernier-chiffre.md) : le moteur d'Albireo lit et écrit enfin à bonne vitesse sur le Mac. Restait à lancer ce qui l'entoure, le service, celui qui reçoit les questions, garde les modèles et sert la page web. Il a démarré du premier coup, et n'a rien trouvé.

## Un service qui répond, mais qui n'a rien

Le service d'Albireo, dans sa version Linux, se lance d'une ligne dans un terminal. On lui a donné un dossier à lui pour ses données, sous `build/`, et un numéro de porte, 18443. Il a démarré en moins d'une seconde, créé ses certificats, ouvert sa porte, et répondu poliment qu'il était prêt.

Puis, dans son journal, deux lignes plus bas : pas de moteur utilisable, pas de dossier de pages web. Et, juste au-dessus, une ligne qui expliquait tout sans en avoir l'air : « programmes dans  », suivi de rien. Le service ne savait pas où il était installé. Il cherchait son moteur et sa page web à côté de lui, et « à côté de lui » ne désignait aucun endroit.

## L'adresse sur la boîte aux lettres

Un programme a souvent besoin de connaître son propre emplacement, pour retrouver les fichiers livrés avec lui. Sous Linux, la réponse est affichée en permanence dans un dossier un peu magique, `/proc`, où le système tient à jour une fiche pour chaque programme en marche. Le service d'Albireo y lit son adresse, comme on lit son nom sur sa boîte aux lettres.

macOS n'a pas de dossier `/proc`. Il connaît évidemment l'adresse de chaque programme, mais il la donne quand on la lui demande, par une fonction à lui, `_NSGetExecutablePath`. Le service regardait la boîte aux lettres, ne voyait pas d'étiquette, et concluait qu'il n'habitait nulle part.

La correction tient en sept lignes, réservées au Mac : demander l'adresse à macOS plutôt que de la chercher dans `/proc`. Au même endroit du code, deux autres lectures de `/proc` donnaient aussi des réponses vides sur le Mac : la mémoire de l'ordinateur, que le service croyait nulle, et le nom du processeur, qu'il laissait en blanc. On les a demandés à macOS de la même façon, par `sysctl`, sa fiche technique intégrée. Le service annonce maintenant « Apple M2, 8 cœurs, 24 Go », et le moteur se sert de la mémoire libre pour décider combien de place donner à chaque conversation.

## Trois gigaoctets en une minute

Relancé, le service a trouvé son moteur, celui des épisodes 8 et 9, qui s'annonce « NEON+dotprod ». Au premier démarrage, il télécharge ses modèles par défaut : un modèle de 4 milliards de paramètres, deux fois plus gros que celui de nos essais, avec un module qui lui apprend à lire les images, et un petit modèle qui sert à chercher dans les documents. En tout, un peu plus de 3 Go, venus de Hugging Face en une minute environ.

La première question, posée par l'interface que la page web utilise :

> Quelle est la capitale de la France ? Réponds en une phrase.

> Paris est la capitale de la France.

Trois secondes et sept dixièmes, 14 tokens par seconde à l'écriture. Le modèle pèse deux fois plus que celui de l'épisode 9 (2,3 Go contre 1,1), et il écrit à peu près deux fois moins vite : la mémoire du Mac débite les poids au même rythme, il y en a simplement deux fois plus à lire pour chaque mot.

## Le modèle qui réfléchit trop

Deuxième essai, cette fois en flux, mot à mot, comme la page web l'affiche : « Explique en trois phrases ce qu'est une puce Apple M2 », avec une limite de 600 tokens. Le flux est arrivé sans accroc, 604 messages. La réponse, elle, était vide.

Le modèle téléchargé est une version « Thinking », qui réfléchit avant de répondre. Il a commencé, en anglais, par se rappeler ce qu'il savait du M2, a hésité, s'est corrigé, et la limite des 600 tokens est tombée avant qu'il ait fini de réfléchir. C'est l'élève qui remplit sa feuille de brouillon et rend une copie blanche. Le portage n'y est pour rien : il suffit de lui laisser plus de place, ou de lui demander de répondre sans réfléchir, comme pour la première question.

## Le service qui ne voulait pas s'arrêter

Pour relancer le service corrigé, il fallait d'abord arrêter l'ancien. On lui a envoyé le signal de Ctrl-C. Rien. On a attendu trente secondes, le délai que se donne le script de test de l'auteur. Toujours rien.

On a d'abord cru à un service bloqué. Une photographie de ses fils, prise avec l'outil `sample` de macOS, a montré le contraire : le fil chargé d'écouter les signaux attendait, tranquille, un signal qui ne lui parvenait jamais.

La cause n'est pas dans Albireo, mais dans la façon dont on l'avait lancé. Un programme lancé en arrière-plan par un script reçoit de son shell la consigne d'ignorer Ctrl-C, pour qu'une touche frappée dans le terminal n'arrête pas tout ce qui tourne derrière. Le service, de son côté, met les signaux de côté pour les traiter dans un fil à part. Linux, dans ce cas, garde le signal de côté pour lui. macOS le jette : un signal qu'on a dit d'ignorer est ignoré, même quand quelqu'un l'attend. C'est le facteur qui voit « pas de publicité » sur la boîte et garde la lettre, alors que le destinataire guettait justement celle-là.

Le signal d'arrêt ordinaire, `kill -TERM`, l'a arrêté en une seconde. Dans un vrai terminal, Ctrl-C marche, puisque rien n'y dit d'ignorer quoi que ce soit. On a noté la chose dans le mémo, sans rien changer au code.

## Ce qui reste à voir

Le service sert sa page web et tous ses fichiers, répond aux questions en une fois et en flux, et les tests de l'auteur repassent après la correction. Ce qu'on n'a pas fait, c'est ouvrir la page dans un navigateur : le portage se fait depuis un terminal, sans écran à regarder. Ce sera le premier geste de l'humain, avec l'avertissement que le navigateur affichera devant un certificat créé par le service lui-même, sur le Mac, et qu'il ne connaît pas encore.

Pseudalis, l'autre application, a sa propre copie du même code, avec la même boîte aux lettres sans étiquette. Elle aura droit à la même correction, à son tour.

## Pour les curieux : le journal et la correction

Le journal du premier démarrage (chemins privés raccourcis en `…`) :

```
[info] Albireo Community Edition 1.0.11 starting
[info] programs in , data in …/build/albireo-home
[warning] http: no web folder at web
[info] server: https://localhost:18443/
[error] engines: no usable engine in
```

Et celui du second, après correction :

```
[info] programs in …/build/macos-albireo/bin, data in …/build/albireo-home
[info] engines: cpu usable
[info] engine: engine cpu 1.0.11, 8 threads, kernels NEON+dotprod
[info] engine: model 'qwen3-vl-4b-thinking': context reduced from 262144 to 20736 tokens, the positions its keys and values can hold in the memory given to them
[info] models: qwen3-vl-4b-thinking loaded
```

La correction, dans `albireo/common/platform.cpp`, glissée entre la version Windows et la version Linux pour ne rien changer à l'une ni à l'autre :

```cpp
#elif defined(__APPLE__)
        // macOS has no /proc: the path the program was started by, then without its links.
        char buf[4096];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0) return std::string();
        char real[PATH_MAX];
        return realpath(buf, real) ? std::string(real) : std::string(buf);
#else
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
```

La mémoire et le processeur, dans `albireo/common/system_info.cpp`, se demandent à `sysctl` (`hw.memsize`, `machdep.cpu.brand_string`) ; la mémoire libre compte les pages libres, inactives et purgeables que donne `host_statistics64`. Au moment du test : 25 769 803 776 octets au total, 9 080 668 160 libres.

Les deux requêtes, `lowpowermode 0`, charge moyenne de 4 à 5 :

| Requête | Tokens lus | Tokens écrits | Durée | Lecture | Écriture |
|---|---|---|---|---|---|
| Capitale de la France, sans raisonnement | 27 | 33 | 3,7 s | 19,4 tokens/s | 14,0 tokens/s |
| Puce Apple M2, en flux, avec raisonnement | 25 | 600 | 54 s | 18,6 tokens/s | 11,3 tokens/s |

Le fil des signaux, tel que `sample` l'a vu pendant que le service refusait de s'arrêter :

```
839 Thread_301160
+       839 sigwait  (in libsystem_pthread.dylib) + 40
+         839 __sigwait  (in libsystem_kernel.dylib) + 8
```

## À suivre

Une conversation dans le navigateur, une image envoyée au modèle, puis Pseudalis : sa boîte aux lettres d'abord, et ses deux étapes de test qui échouent encore, celles des fichiers renommés et copiés sur le disque du Mac.
