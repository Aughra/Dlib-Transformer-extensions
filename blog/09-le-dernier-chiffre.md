# Dlib Transformer Extensions sur macOS, épisode 9 : le dernier chiffre

> **Repère de vitesse** (MacBook Air M2) : écriture **33 à 37 tokens/s** (2B), lecture 35 à 50 tokens/s.

Suite de [l'épisode 8](08-seize-a-la-fois.md) : Albireo écrit sa réponse trois fois plus vite, mais il lit encore la question à une douzaine de tokens par seconde. Cet épisode s'occupe de la lecture, et d'un dernier chiffre qui refusait de tomber juste.

## Lire n'est pas écrire

Pour écrire, le moteur produit un token à la fois : chaque poids du modèle sert une seule fois par token. Pour lire, il reçoit toute la question d'un coup, et peut traiter plusieurs tokens avec le même poids. L'auteur d'Albireo a donc prévu deux chemins. À partir de 8 tokens, le moteur développe les poids compressés en nombres ordinaires, puis les multiplie par paquets de tokens.

Sur un PC Intel, ce second chemin a son calcul accéléré : chaque poids lu sert trois tokens, avec des instructions qui traitent huit nombres d'un coup. Sur le Mac, il n'avait que la version de secours, un poids pour un token, un nombre à la fois.

## Quatre tokens par poids

Le nouveau calcul prend quatre lignes de poids et quatre tokens, et fait les seize produits ensemble. Chaque poids lu en mémoire sert ainsi quatre fois au lieu d'une, et chaque instruction traite quatre nombres. C'est le facteur qui, au lieu de faire une tournée par lettre, met dans sa sacoche le courrier de toute la rue.

Comme à l'épisode 8, on voulait que la réponse ne change pas d'un caractère. Le calcul de référence additionne ses nombres dans quatre tiroirs séparés, puis réunit les tiroirs deux par deux. C'est exactement la forme d'un registre NEON de quatre nombres. Il suffisait, croyait-on, de garder les quatre tiroirs et le même ordre de réunion.

## Le dernier chiffre

Le contrôle a dit non. Sur 32 436 résultats comparés à ceux du calcul de référence, 23 953 différaient. De très peu : le dernier chiffre, une ou deux unités sur la septième décimale. Mais différaient.

L'explication tient dans un geste que les processeurs modernes savent faire : multiplier puis additionner d'un seul coup, sans arrondir entre les deux. C'est plus rapide et même un peu plus précis, et notre premier essai l'utilisait. Le calcul de référence, lui, tel que le compilateur d'Apple le traduit, arrondit d'abord le produit, puis l'additionne. Deux arrondis au lieu d'un : la septième décimale ne tombe plus au même endroit.

On aurait pu s'en accommoder. Une différence au dernier chiffre ne rend pas un calcul faux, et le test de l'auteur, avec sa tolérance de deux millionièmes, l'acceptait. Mais un modèle de langage choisit chaque mot parmi des candidats parfois presque à égalité, et un dernier chiffre peut suffire à faire basculer un mot, puis toute la fin d'une réponse. On a donc fait comme la référence : multiplier, arrondir, additionner. Nouveau contrôle : zéro différence sur 32 436. Le geste en deux temps est en principe un peu plus lent, ce qu'on n'a pas mesuré ; il achète une réponse qui ne dépend pas du calcul qui l'a produite.

## Quatre fois plus vite

Mesures à tour de rôle, comme toujours, contre la version de l'épisode 8 :

| Question de… | Avant | Après |
|---|---|---|
| 64 tokens | 11 à 13 tokens/s | 38 à 47 tokens/s |
| 256 tokens | 10 à 13 tokens/s | 45 à 50 tokens/s |
| 21 tokens (notre question habituelle) | 5 à 11 tokens/s | 35 à 39 tokens/s |

La lecture va trois fois et demie à quatre fois plus vite. Et la réponse à la question habituelle est toujours, octet pour octet, celle du moteur d'origine, celle de l'épisode 6. Depuis le début de l'accélération, le modèle n'a pas changé un seul mot.

En deux épisodes, Albireo est passé sur le Mac de 11 à environ 35 tokens par seconde pour écrire, et de 11 à environ 45 pour lire. Le Mac avait de la marge ; il fallait seulement lui parler sa langue.

## Pour les curieux : le cœur du calcul

Le noyau `gemm_neon`, dans `engine/qkernels_neon.inc`. Quatre lignes de poids chargées, puis, pour chacun des tokens, quatre produits rangés dans leurs tiroirs :

```cpp
const float32x4_t w0 = vld1q_f32(W + k);
const float32x4_t w1 = vld1q_f32(W + K + k);
const float32x4_t w2 = vld1q_f32(W + 2 * K + k);
const float32x4_t w3 = vld1q_f32(W + 3 * K + k);
for (int i = 0; i < NT; ++i)
{
    const float32x4_t x = vld1q_f32(X + i * xs + k);
    acc[i][0] = vaddq_f32(acc[i][0], vmulq_f32(w0, x));
    acc[i][1] = vaddq_f32(acc[i][1], vmulq_f32(w1, x));
    acc[i][2] = vaddq_f32(acc[i][2], vmulq_f32(w2, x));
    acc[i][3] = vaddq_f32(acc[i][3], vmulq_f32(w3, x));
}
```

La première version écrivait `vfmaq_f32(acc[i][0], w0, x)`, la multiplication-addition d'un seul geste. Le contrôle, avant et après :

```
kernels NEON+dotprod: 23953 of 32436 float products differ from qk_dot_f32
kernels NEON+dotprod: 0 of 32436 float products differ from qk_dot_f32
```

La référence que le nouveau noyau reproduit, `dot_f32_ref`, avec ses quatre tiroirs :

```cpp
float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
long i = 0;
for (; i + 4 <= n; i += 4)
{
    s0 += a[i] * b[i];
    s1 += a[i + 1] * b[i + 1];
    s2 += a[i + 2] * b[i + 2];
    s3 += a[i + 3] * b[i + 3];
}
for (; i < n; ++i) s0 += a[i] * b[i];
return (s0 + s1) + (s2 + s3);
```

Les mesures alternées, `lowpowermode 0`, 8 fils, charge moyenne de 6 à 23 :

| Programme | Essai | Avant | Après |
|---|---|---|---|
| `bench_runtime`, 64 tokens | 1 | 13,1 tokens/s | 38,3 tokens/s |
| `bench_runtime`, 256 tokens | 1 | 12,8 tokens/s | 50,4 tokens/s |
| `engine_chat`, 21 tokens | 1 | 4,6 tokens/s | 34,6 tokens/s |
| `bench_runtime`, 64 tokens | 2 | 10,8 tokens/s | 46,8 tokens/s |
| `bench_runtime`, 256 tokens | 2 | 10,3 tokens/s | 44,9 tokens/s |
| `engine_chat`, 21 tokens | 2 | 11,0 tokens/s | 39,1 tokens/s |

Pendant cette série, la charge du Mac est montée jusqu'à 23, et la vitesse d'écriture, que ce changement ne touche pas, a varié de 14 à 34 tokens/s d'un essai à l'autre. C'est la raison pour laquelle on ne compare que des mesures faites côte à côte.

## À suivre

Le moteur est prêt. Reste à brancher ce qui l'entoure : le service d'Albireo, son interface web, et une première conversation sur le Mac comme on l'aurait sur Windows, dans un navigateur.
