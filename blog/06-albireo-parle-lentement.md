# Dlib Transformer Extensions sur macOS, épisode 6 : Albireo parle, lentement

*Correction : toutes les mesures de cet épisode ont été prises alors que le Mac était, sans qu'on le sache, en mode économie d'énergie. La « seconde cause » décrite plus bas venait de là, et non du moteur. Les vraies mesures sont dans [l'épisode 7](07-le-mode-economie.md).*

Suite de [l'épisode 5](05-cinq-corrections.md) : Albireo et Pseudalis compilent sur le Mac, et leurs tests passent. Il restait à donner un modèle de langage à Albireo, à lui poser une question, et à chronométrer sa réponse.

## Un premier modèle

Albireo ne vient pas avec un cerveau : il sait faire tourner des modèles de langage, mais il faut lui en fournir un. Son catalogue en liste une vingtaine, de 1 à 19 Go. Pour un premier essai, on a pris le plus petit de la famille qu'Albireo installe par défaut, Qwen3-VL 2B Instruct : 1,1 Go, et une variante « Instruct » qui répond directement, sans longue réflexion préalable.

Le fichier vient de l'adresse inscrite dans le catalogue, et sa taille et son empreinte ont été comparées à celles que le catalogue annonce. C'est la vérification qu'Albireo fait lui-même avant d'utiliser un modèle : une empreinte est le relevé d'identité d'un fichier, et un seul octet changé suffit à la rendre méconnaissable. Les deux concordaient.

## Une réponse juste

La question : « Explique en trois phrases simples ce qu'est un compilateur. » La réponse, mot pour mot :

> Un compilateur est un programme qui prend un code écrit en une langue de programmation (comme Python ou C++) et le transforme en code machine compréhensible par l'ordinateur. Il analyse le code, effectue des vérifications et génère du code binaire que l'ordinateur peut exécuter. Enfin, il permet d'exécuter le programme à partir du code compilé.

On peut discuter l'exemple de Python, qu'on interprète plus souvent qu'on ne le compile, mais l'essentiel est juste, en bon français, et en trois phrases comme demandé. Pour la première fois, le moteur d'Albireo a répondu à une question sur un Mac.

Il lui a fallu 2 minutes et 13 secondes.

## Moins d'un mot par seconde

Un modèle de langage écrit sa réponse morceau par morceau. Ces morceaux, des « tokens », valent à peu près une syllabe ou un petit mot. Albireo en a produit 0,8 par seconde. À ce rythme, une réponse d'un paragraphe se lit plus vite qu'elle ne s'écrit : on attend le mot suivant comme une goutte d'un robinet qui fuit.

Deux causes se dessinent, et on les a vues l'une après l'autre en observant le moteur au travail. L'outil `sample` de macOS relève, des centaines de fois par seconde, dans quelle fonction se trouve chaque fil de calcul : c'est la photo prise à intervalles réguliers dans un atelier, qui montre qui fait quoi.

## Première cause : un nombre à la fois

Presque toutes les photos montrent la même chose : le moteur multiplie des nombres. C'est normal, un modèle de langage n'est guère qu'une immense suite de multiplications. Ce qui l'est moins, c'est la façon dont il les fait sur le Mac.

Sur un PC Intel ou AMD, Albireo utilise les instructions AVX2, qui multiplient 8 nombres d'un seul geste. Sur le Mac, ces instructions n'existent pas. L'épisode 5 a donc fait passer Albireo sur sa version « portable », qui marche partout, mais qui calcule les nombres un par un. C'est le copiste qui recopie un livre lettre par lettre, quand son voisin recopie des mots entiers.

La puce Apple a pourtant son équivalent, NEON, qui traite 4 nombres d'un geste. Personne ne l'a encore écrit pour Albireo, puisqu'Albireo n'avait jamais vu de Mac. C'est le chantier le plus prometteur.

## Seconde cause : un seul cœur au travail

Le M2 a huit cœurs, quatre rapides et quatre économes. Albireo annonce qu'il en utilise huit. La montre dit autre chose : pendant les 133 secondes de la réponse, le processeur n'a travaillé que 120 secondes en tout. C'est moins d'un cœur occupé, en moyenne.

Pour en avoir le cœur net, on a pris le banc d'essai d'Albireo, un petit programme qui contient le même moteur et le chronomètre. Lui va deux fois plus vite, 1,9 token par seconde, et occupe deux cœurs. Le même code, compilé dans un programme plutôt que dans la bibliothèque qu'Albireo charge, ne se comporte donc pas pareil.

Les photos du moteur en bibliothèque montrent bien quatre fils de calcul, et du travail pour chacun. Mais elles les surprennent presque aussi souvent en train de céder leur tour qu'en train de calculer. Sur un PC Intel, un fil qui attend son prochain travail fait une courte pause sur place. Sur le Mac, faute de cette instruction, le code lui fait céder sa place au système. Est-ce la cause, ou seulement un symptôme ? L'enquête le dira.

Enfin, huit fils ne vont pas plus vite que quatre. Les quatre cœurs économes du M2, faits pour durer plutôt que pour courir, n'apportent rien ici.

## Une fausse alerte

Le banc d'essai a planté au premier essai, avec un message inquiétant : un « token » hors du vocabulaire du modèle. L'erreur était la nôtre. Le banc mesure à la fin plusieurs réponses écrites en même temps, et il les fait toutes partir des 64 premiers tokens du texte de départ. On ne lui en avait donné que 32 : il a lu au-delà de la fin, et y a trouvé n'importe quoi. Avec 64 tokens, plus aucun plantage. Avant d'accuser un moteur, il vaut mieux relire le mode d'emploi de son chronomètre.

## Pour les curieux : les chiffres

Le modèle et sa vérification :

```
Qwen3VL-2B-Instruct-Q4_K_M.gguf
taille   1107409952 octets
SHA-256  089d75c52f4b7ffc56ba998ffc50aae89fcafc755f9e7208aacca281dca6c2ae
```

Ce que le moteur dit de lui-même au chargement :

```
[engine 1] engine cpu 1.0.11, 8 threads, kernels portable
[engine 1] model 'm': processor runtime (portable, 8 threads), Qwen3Vl 2b Instruct (qwen3vl, 28 layers,
width 2048, heads 16/8 of 128, ffn 6144, vocabulary 151936, ...), chat format chatml, context 4096
```

Les mesures, toutes avec ce modèle et une température de 0 :

| Programme | Fils | Lecture de la question | Écriture de la réponse | Cœurs occupés |
|---|---|---|---|---|
| `engine_chat` (moteur en bibliothèque) | 8 | 0,99 token/s | 0,78 token/s | 0,9 |
| `engine_chat` (moteur en bibliothèque) | 4 | 0,99 token/s | 0,85 token/s | 0,95 |
| `bench_runtime` (moteur dans le programme) | 1 | 0,5 token/s | 0,5 token/s | 0,5 |
| `bench_runtime` | 4 | 2,5 tokens/s | 1,9 token/s | 2,1 |
| `bench_runtime` | 8 | 2,1 tokens/s | 1,8 token/s | 2,1 |

Les fonctions où le moteur passe son temps, d'après `sample`. D'abord pendant la lecture de la question, dans le banc d'essai à 1 fil :

```
matmul_float(...)      3402
gguf_dq_q4_K(...)        67
qk_attention_scores      8
```

Puis pendant l'écriture de la réponse, dans `engine_chat` à 4 fils, sur 6 secondes :

```
qk_dot_portable(...)   6143
swtch_pri              5664
__psynch_cvwait        2980
```

`swtch_pri` est la fonction du système qu'appelle `std::this_thread::yield()`, « je cède mon tour ». C'est elle que le moteur utilise pour faire patienter un fil hors des processeurs Intel :

```cpp
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define ALBIREO_CPU_RELAX() _mm_pause()
#else
#define ALBIREO_CPU_RELAX() std::this_thread::yield()
#endif
```

Les commandes, pour refaire les mesures :

```sh
build/macos-albireo/tests/engine_chat build/macos-albireo/bin/engines/libalbireo-engine-cpu.dylib \
  build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf --threads 4 --max 40 --temperature 0 \
  "Explique en trois phrases simples ce qu'est un compilateur."
build/macos-albireo/tests/bench_runtime build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf 4 64 16 2
```

## À suivre

Deux chantiers pour faire accélérer Albireo, dans l'ordre : comprendre pourquoi le moteur en bibliothèque laisse ses cœurs au repos, puis lui apprendre NEON, pour qu'il calcule quatre nombres d'un geste au lieu d'un. Le prochain épisode dira combien chacun rapporte.
