// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_VERSION_H_
#define PSEUDALIS_COMMON_VERSION_H_

// Version des programmes et des moteurs Pseudalis, comparée par le flux de mises à jour et par
// l'installateur lors d'une installation par-dessus une autre.
#define PSEUDALIS_VERSION_MAJOR 1
#define PSEUDALIS_VERSION_MINOR 0
#define PSEUDALIS_VERSION_PATCH 5
#define PSEUDALIS_VERSION "1.0.5"

#define PSEUDALIS_PRODUCT_NAME "Pseudalis"
/*
    L'édition : Community, libre et open source sous la Boost Software License (README.md,
    Editions).  Les programmes installés, la fenêtre « À propos », l'application web et les
    fichiers des programmes la nomment.
*/
#ifndef PSEUDALIS_EDITION
#define PSEUDALIS_EDITION "Community"
#endif
// Le nom et son édition en un seul littéral, qu'une édition définit avec PSEUDALIS_EDITION : le
// compilateur de ressources de Windows ne concatène pas les littéraux adjacents.
#ifndef PSEUDALIS_FULL_NAME
#define PSEUDALIS_FULL_NAME "Pseudalis Community"
#endif
#define PSEUDALIS_PUBLISHER "Cydral Technology"
#define PSEUDALIS_AUTHOR_NAME "Aldric Pierrain"
#define PSEUDALIS_AUTHOR_EMAIL "cydraltechnology@gmail.com"
#define PSEUDALIS_COPYRIGHT "Copyright (C) 2026 Cydral Technology"
#define PSEUDALIS_PROJECT_URL "https://github.com/Cydral/Dlib-Transformer-extensions"

#endif // PSEUDALIS_COMMON_VERSION_H_
