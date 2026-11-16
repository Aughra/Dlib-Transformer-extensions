# Pseudalis sur macOS, épisode 4 : des détecteurs réentraînés

> **Repère de débit** (MacBook Air M2, mode économie d'énergie coupé, charge 2,4 à 2,9) : protection d'un texte de 30 000 octets en **1,46 à 1,48 s**, soit **20,3 à 20,6 Ko/s**, avec les nouveaux détecteurs (1,44 à 1,46 s avec les anciens, sur le même texte ; mesures non alternées).

Suite de [l'épisode 3](03-les-colonnes-designees.md) : les colonnes désignées d'un fichier CSV. Dernière version de l'original à reprendre, la 1.0.11, qui ne change presque rien au programme et beaucoup à ce qu'il sait reconnaître.

## Des yeux neufs

Pseudalis trouve les données personnelles avec deux « reconnaisseurs » appris : un étiqueteur, qui lit le texte mot par mot, et un modèle qui repère des passages entiers. Ce sont des programmes entraînés sur des exemples, comme un apprenti à qui l'on montre des milliers de documents annotés. L'original les a entraînés de nouveau avec des exemples d'un genre qui leur manquait : des tableaux sans ligne d'en-tête, des pages de titre, des courriers médicaux, des rapports économiques, du code source, en cinq langues.

Le résultat se voit sur une phrase d'essai. Dans « Le Dr Martin (LANR 123456789) a vu Hélène Lemoine à Rennes ; expiry date 07/29 », le nom du médecin, son numéro d'ordre allemand après « LANR », la patiente, la ville sont remplacés, et la date d'expiration de carte devient « 02/28 » : décalée, comme toutes les dates, d'un écart secret. À l'inverse, les apprentis ont appris à laisser tranquilles des mots qui ressemblaient à des noms sans en être : `True`, `False` ou `None` dans du code, les noms de médicaments, les termes de statistique.

## Un faux procès de clang

Ces poids vivent dans dlib, sous forme de texte compressé dans un fichier d'exemple, et les règles du détecteur dans son dossier `privacy`. Tout s'est appliqué sans conflit. Un seul accroc : le compilateur du Mac a refusé une liste de mots. Sa dernière entrée était écrite sur deux lignes, deux morceaux de texte collés l'un à l'autre, et clang y a vu une virgule oubliée entre deux entrées. Ce n'en était pas une : le texte collé est voulu. Des parenthèses autour de l'entrée suffisent à le dire au compilateur, sans rien changer d'autre. L'erreur est notée pour l'auteur de l'original.

## Pour les curieux : les chiffres et les fichiers

- dlib, repris de l'original (`40bd030`) : `privacy/date_formats_abstract.h`, `document_anonymizer.h`, `document_anonymizer_abstract.h`, `id_checks.h`, `pii_detector.h`, `pii_detector_abstract.h`, `pii_lexicons.h`, `pseudonymizer.h`, `pseudonymizer_abstract.h`, `test/privacy.cpp`, `examples/slm_document_anonymizer_ex.h` (les poids). Corrigé par le fork : l'entrée de `pii_lexicons.h` entre parenthèses (notes pour l'auteur, n° 24).
- Le message de clang : « suspicious concatenation of string literals in an array initialization; did you mean to separate the elements with a comma? ».
- Tests : `dtest --test_privacy`, 1 407 vérifications ; tests du moteur et du service de Pseudalis ; test complet, 37 sur 37.
- Pseudalis passe en **3.3.0**.

## À suivre

Les versions de l'original sont toutes reprises. Le portage passe à un autre chantier : un backend Metal pour les tenseurs de dlib.
