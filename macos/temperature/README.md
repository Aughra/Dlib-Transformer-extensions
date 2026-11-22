# Température de la puce

Un petit outil qui lit, sans mot de passe administrateur, l'état thermique de macOS et les sondes de température de la puce (la plus chaude et la moyenne des sondes `PMU tdie`). Il sert aux mesures de vitesse : `tools/bench_moteurs.sh` de Gulliver note la température au début et à la fin de chaque campagne quand l'outil est construit.

```
clang -fobjc-arc -framework Foundation -framework IOKit -o build/temperature macos/temperature/temperature.m
build/temperature
```

Sortie :

```
état thermique de macOS : normal
puce : 24.6 °C au plus, 23.2 °C en moyenne (42 sondes)
```

Les sondes passent par `IOHIDEventSystemClient`, une interface interne d'Apple (non documentée, employée par des utilitaires comme Stats) : elle peut changer avec une version de macOS. `powermetrics`, l'outil officiel, demande les droits d'administrateur et ne donne sur les puces Apple qu'un niveau de pression thermique.

Relevé du 22 novembre 2026 (Mac mini M5 Pro surélevé) : 23 °C au repos ; 42,8 °C au plus après six lectures et écritures de Gemma 3 4B à 16 384 tokens d'affilée, sans aucune baisse de vitesse.
