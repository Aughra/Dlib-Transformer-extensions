// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet « Service » des paramètres : chaque réglage du service que la page peut changer, avec son
    effet en une ligne et, derrière son point d'interrogation, une fiche à la manière du glossaire de
    TortueTech : le principe, les avantages et les inconvénients, un schéma, puis trois explications,
    normale, très technique (le hibou) et pour un ado (la marmotte), et ce qui arrive si on le pousse
    trop loin ou qu'on s'en passe (l'abeille).

    Les réglages « en direct » s'appliquent dès qu'on les change ; ceux qui demandent un redémarrage du
    service attendent le bouton « Appliquer et redémarrer », qui les envoie ensemble.  Les textes sont
    en français seulement : cette version est personnelle.
*/

// ---------------------------------------------------------------------------------------------------
// Les personnages, en petits portraits SVG.

export const portraits = {
  // Le hibou : l'explication très technique (lunettes rondes, aigrettes, carnet).
  owl: `<svg viewBox="0 0 64 64" aria-hidden="true">
    <circle cx="32" cy="32" r="31" fill="#2f6f73"/>
    <path d="M14 22 L19 9 L26 19 Z M50 22 L45 9 L38 19 Z" fill="#8a6a4a"/>
    <ellipse cx="32" cy="36" rx="20" ry="21" fill="#a07a55"/>
    <ellipse cx="32" cy="44" rx="11" ry="12" fill="#e9d8bf"/>
    <circle cx="23" cy="30" r="8.5" fill="#fff"/><circle cx="41" cy="30" r="8.5" fill="#fff"/>
    <circle cx="23" cy="30" r="8.5" fill="none" stroke="#1d2433" stroke-width="2.4"/>
    <circle cx="41" cy="30" r="8.5" fill="none" stroke="#1d2433" stroke-width="2.4"/>
    <path d="M31.5 30 h1" stroke="#1d2433" stroke-width="2.4"/>
    <circle cx="24" cy="31" r="3.6" fill="#1d2433"/><circle cx="40" cy="31" r="3.6" fill="#1d2433"/>
    <circle cx="25.2" cy="29.6" r="1.1" fill="#fff"/><circle cx="41.2" cy="29.6" r="1.1" fill="#fff"/>
    <path d="M29 37 L32 42 L35 37 Z" fill="#f2b544"/>
    <rect x="38" y="47" width="12" height="9" rx="1.5" fill="#fff" stroke="#1d2433" stroke-width="1.2"/>
    <path d="M40 50 h8 M40 53 h6" stroke="#5b8cff" stroke-width="1.2"/>
  </svg>`,
  // La marmotte : l'explication pour un ado.
  marmot: `<svg viewBox="0 0 64 64" aria-hidden="true">
    <circle cx="32" cy="32" r="31" fill="#e56b3f"/>
    <circle cx="16" cy="18" r="6" fill="#7a5235"/><circle cx="48" cy="18" r="6" fill="#7a5235"/>
    <circle cx="16" cy="18" r="3" fill="#c99b74"/><circle cx="48" cy="18" r="3" fill="#c99b74"/>
    <ellipse cx="32" cy="35" rx="20" ry="19" fill="#9a6a43"/>
    <ellipse cx="32" cy="42" rx="12" ry="10" fill="#e3c39d"/>
    <circle cx="24" cy="30" r="3.4" fill="#1d1a17"/><circle cx="40" cy="30" r="3.4" fill="#1d1a17"/>
    <circle cx="25" cy="29" r="1" fill="#fff"/><circle cx="41" cy="29" r="1" fill="#fff"/>
    <ellipse cx="32" cy="37" rx="3.4" ry="2.4" fill="#3a2a20"/>
    <path d="M32 39.5 v3" stroke="#3a2a20" stroke-width="1.4"/>
    <rect x="29.4" y="42.4" width="2.4" height="4" rx="0.6" fill="#fff"/>
    <rect x="32.2" y="42.4" width="2.4" height="4" rx="0.6" fill="#fff"/>
  </svg>`,
  // L'abeille : ce qui arrive si on s'en passe, ou si on pousse trop.
  bee: `<svg viewBox="0 0 64 64" aria-hidden="true">
    <circle cx="32" cy="32" r="31" fill="#f2b544"/>
    <ellipse cx="21" cy="22" rx="9" ry="6" fill="#ffffff" opacity="0.85" transform="rotate(-25 21 22)"/>
    <ellipse cx="43" cy="22" rx="9" ry="6" fill="#ffffff" opacity="0.85" transform="rotate(25 43 22)"/>
    <ellipse cx="32" cy="38" rx="15" ry="16" fill="#ffd34d" stroke="#1d1a17" stroke-width="1.6"/>
    <path d="M18.5 34 h27 M17.5 41 h29 M20 48 h24" stroke="#1d1a17" stroke-width="4"/>
    <circle cx="26" cy="29" r="2.6" fill="#1d1a17"/><circle cx="38" cy="29" r="2.6" fill="#1d1a17"/>
    <path d="M28 25 Q24 14 20 12 M36 25 Q40 14 44 12" stroke="#1d1a17" stroke-width="1.6" fill="none"/>
    <circle cx="20" cy="12" r="2" fill="#1d1a17"/><circle cx="44" cy="12" r="2" fill="#1d1a17"/>
  </svg>`
};

// ---------------------------------------------------------------------------------------------------
// De quoi dessiner les schémas : des boîtes, des flèches et du texte, aux couleurs du thème.

export function box(x, y, w, hgt, label, cls, sub) {
  const cy = sub ? y + hgt / 2 - 4 : y + hgt / 2 + 5;
  return `<rect x="${x}" y="${y}" width="${w}" height="${hgt}" rx="9" class="s-box ${cls || ''}"/>` +
    `<text x="${x + w / 2}" y="${cy}" class="s-t" text-anchor="middle">${label}</text>` +
    (sub ? `<text x="${x + w / 2}" y="${cy + 17}" class="s-f" text-anchor="middle">${sub}</text>` : '');
}

export function arrow(x1, y1, x2, y2, label, cls) {
  const a = Math.atan2(y2 - y1, x2 - x1), l = 9;
  const p1 = [x2 - l * Math.cos(a - 0.45), y2 - l * Math.sin(a - 0.45)];
  const p2 = [x2 - l * Math.cos(a + 0.45), y2 - l * Math.sin(a + 0.45)];
  const mx = (x1 + x2) / 2, my = (y1 + y2) / 2 - 7;
  return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" class="s-line ${cls || ''}"/>` +
    `<polygon points="${x2},${y2} ${p1[0]},${p1[1]} ${p2[0]},${p2[1]}" class="s-head ${cls || ''}"/>` +
    (label ? `<text x="${mx}" y="${my}" class="s-f" text-anchor="middle">${label}</text>` : '');
}

export function text(x, y, s, cls, anchor) {
  return `<text x="${x}" y="${y}" class="${cls || 's-t'}" text-anchor="${anchor || 'middle'}">${s}</text>`;
}

export function schema(title, inner, height) {
  return `<svg viewBox="0 0 600 ${height || 220}" role="img" aria-label="${title}" class="svc-svg">` +
    `<text x="16" y="26" class="s-title">${title}</text>${inner}</svg>`;
}

export const schemas = {
  accelerator: schema('La puce du Mac : un seul plan de travail pour deux cuisiniers',
    `<rect x="40" y="44" width="520" height="160" rx="16" class="s-zone"/>` + text(300, 64, 'Puce Apple M2', 's-f') +
    box(70, 78, 170, 56, 'Processeur', 's-gold', 'contrôle, découpe, images') +
    box(360, 78, 170, 56, 'GPU', 's-blue', 'écrit et lit en grand') +
    box(70, 152, 460, 40, 'Mémoire unifiée : le modèle, les clés et valeurs, sans copie', 's-soft') +
    arrow(155, 134, 155, 152) + arrow(445, 134, 445, 152) +
    arrow(240, 98, 360, 98, 'prompts ≥ 24 tokens') + arrow(240, 120, 360, 120, 'chaque token écrit'), 215),
  threads: schema('Sur un M2 : deux familles de cœurs, deux équipes',
    [0, 1, 2, 3].map((i) => box(40 + i * 70, 60, 60, 46, 'P' + (i + 1), 's-gold')).join('') +
    [0, 1, 2, 3].map((i) => box(330 + i * 60, 66, 50, 34, 'E' + (i + 1), 's-soft')).join('') +
    text(155, 128, 'cœurs de performance', 's-f') + text(435, 128, "cœurs d'efficacité", 's-f') +
    `<rect x="32" y="146" width="280" height="30" rx="8" class="s-band s-blue"/>` +
    text(172, 166, 'écrire un token : 4 threads rapides', 's-t') +
    `<rect x="32" y="182" width="556" height="30" rx="8" class="s-band s-gold"/>` +
    text(310, 202, 'lire un long prompt : tous les threads', 's-t'), 222),
  max_sequences: schema('Plusieurs questions dans le même passage',
    box(20, 60, 110, 34, 'question A') + box(20, 104, 110, 34, 'question B') + box(20, 148, 110, 34, 'question C', 's-soft', 'attend') +
    arrow(130, 77, 230, 110) + arrow(130, 121, 230, 121) + text(180, 180, 'file', 's-f') +
    box(230, 84, 150, 74, 'Modèle', 's-blue', '1 passage = A + B') +
    arrow(380, 104, 470, 80) + arrow(380, 138, 470, 160) +
    box(470, 60, 110, 40, 'mémoire A', 's-gold') + box(470, 140, 110, 40, 'mémoire B', 's-gold') +
    text(300, 205, 'réglage = 2 : deux à la fois, la troisième attend', 's-f'), 215),
  context: schema('La fenêtre de contexte : ce que le modèle voit à la fois',
    Array.from({ length: 22 }, (_, i) => `<rect x="${24 + i * 25}" y="86" width="21" height="34" rx="4" class="s-box ${i < 7 ? 's-soft' : 's-gold'}"/>`).join('') +
    `<rect x="195" y="76" width="380" height="54" rx="10" class="s-window"/>` +
    text(385, 154, 'contexte : le modèle relit tout ce qui est dans la fenêtre', 's-f') +
    text(110, 154, 'trop ancien : hors de vue', 's-f') +
    arrow(560, 66, 590, 66) + text(530, 70, 'nouveau texte', 's-f', 'end'), 175),
  kv_mb: schema('La mémoire du Mac et la part des clés et valeurs',
    `<rect x="24" y="70" width="552" height="46" rx="10" class="s-box"/>` +
    `<rect x="24" y="70" width="150" height="46" rx="10" class="s-soft"/>` + text(99, 98, 'macOS, apps') +
    `<rect x="174" y="70" width="120" height="46" class="s-blue"/>` + text(234, 98, 'modèles') +
    `<rect x="294" y="70" width="190" height="46" class="s-gold"/>` + text(389, 98, 'clés et valeurs') +
    text(530, 98, 'réserve') +
    arrow(389, 146, 389, 120) + text(389, 164, 'kv_mb : la taille de cette part', 's-f') +
    text(300, 196, 'auto : deux contextes complets, en gardant une réserve au système', 's-f'), 210),
  kv_type: schema('Une clé de 32 nombres, en deux formats',
    text(24, 76, 'f16', 's-t', 'start') +
    Array.from({ length: 16 }, (_, i) => `<rect x="${70 + i * 31}" y="58" width="28" height="28" rx="4" class="s-box s-blue"/>`).join('') +
    text(70 + 16 * 31 + 4, 77, '64 o', 's-f', 'start') +
    text(24, 136, 'q8', 's-t', 'start') +
    Array.from({ length: 8 }, (_, i) => `<rect x="${70 + i * 31}" y="118" width="28" height="28" rx="4" class="s-box s-gold"/>`).join('') +
    `<rect x="${70 + 8 * 31}" y="118" width="20" height="28" rx="4" class="s-box s-soft"/>` +
    text(70 + 8 * 31 + 30, 137, '34 o (+ une échelle)', 's-f', 'start') +
    text(300, 186, "q8 : presque moitié moins de mémoire, un arrondi minuscule", 's-f'), 200),
  lan: schema('Le téléphone sur le canapé, le Mac au bureau',
    box(20, 70, 120, 60, 'Téléphone', 's-soft', 'navigateur') +
    arrow(140, 100, 250, 100, 'Wi-Fi') +
    `<rect x="250" y="56" width="90" height="88" rx="12" class="s-box s-gold"/>` + text(295, 96, 'porte') + text(295, 114, 'ouverte') +
    arrow(340, 100, 430, 100, 'http') +
    box(430, 60, 150, 80, 'Mac', 's-blue', 'http, port 8444') +
    box(430, 160, 150, 40, 'localhost : libre', 's-soft') +
    text(80, 166, 'chaque appareil garde', 's-f') + text(80, 182, 'ses conversations', 's-f'), 215),
  lan_port: schema('Deux portes au même immeuble',
    `<rect x="190" y="44" width="220" height="160" rx="12" class="s-zone"/>` + text(300, 66, 'le Mac', 's-f') +
    box(210, 84, 80, 96, '18443', 's-blue', 'Mac seul') + box(310, 84, 80, 96, '8444', 's-gold', 'réseau') +
    arrow(80, 132, 210, 132, 'localhost') + arrow(520, 132, 390, 132, 'Wi-Fi') +
    text(70, 160, 'le Mac lui-même', 's-f') + text(530, 160, 'les autres appareils', 's-f'), 215),
  port: schema("L'adresse de la page sur le Mac",
    box(30, 80, 150, 56, 'Navigateur', 's-soft') + arrow(180, 108, 330, 108, 'https://localhost:18443') +
    box(330, 80, 150, 56, 'Service', 's-blue') +
    `<rect x="30" y="150" width="150" height="44" rx="9" class="s-box s-gold"/>` + text(105, 170, 'conversations', 's-t') +
    text(105, 186, 'rangées par adresse', 's-f') +
    text(400, 176, 'nouvelle adresse = page vide', 's-f'), 210),
  offline: schema('Hors ligne : rien ne sort du Mac',
    box(40, 80, 150, 60, 'Mac', 's-blue', 'service') +
    `<ellipse cx="460" cy="108" rx="100" ry="44" class="s-box s-soft"/>` + text(460, 113, 'Internet') +
    arrow(190, 108, 355, 108, '', 's-cut') + `<path d="M262 92 l16 32 M278 92 l-16 32" class="s-cross"/>` +
    box(40, 160, 150, 40, 'imports + réserve', 's-gold') + arrow(115, 160, 115, 140), 215),
  web_access: schema('Le bouton Web de la page',
    box(20, 80, 120, 56, 'Question', 's-soft') + arrow(140, 108, 230, 108) +
    box(230, 80, 130, 56, 'Service', 's-blue', 'recherche, lecture') + arrow(360, 108, 450, 108) +
    box(450, 80, 130, 56, 'Pages web', 's-gold') +
    text(300, 170, 'coupé : le modèle répond avec ce qu’il sait déjà', 's-f'), 190),
  web_search: schema('Où le service cherche',
    box(30, 80, 140, 56, 'Recherche', 's-blue') + arrow(170, 108, 300, 108) +
    box(300, 80, 170, 56, 'Wikipédia', 's-gold', 'articles, liens') +
    text(300, 170, 'aucun : le bouton Web lit seulement les adresses données', 's-f'), 190),
  proxy: schema('Le relais vers Internet',
    box(20, 80, 120, 56, 'Mac', 's-blue') + arrow(140, 108, 240, 108) +
    box(240, 80, 120, 56, 'Proxy', 's-gold', 'relais') + arrow(360, 108, 460, 108) +
    box(460, 80, 120, 56, 'Internet', 's-soft') +
    text(300, 170, 'vide : les variables HTTPS_PROXY ; « none » : en direct', 's-f'), 190),
  mail_from: schema('Le code part de chez toi, par Proton',
    box(16, 70, 120, 60, 'Gulliver', 's-blue', 'code, lien') + arrow(136, 100, 236, 100, '127.0.0.1') +
    box(236, 70, 128, 60, 'Bridge', 's-gold', 'chiffre') + arrow(364, 100, 464, 100, 'Proton') +
    box(464, 70, 120, 60, 'Ta boîte', 's-soft', 'le mail') +
    text(300, 170, 'expéditeur : l’adresse réglée ici', 's-f'), 190),
  mail_port: schema('Une porte sur le Mac lui-même',
    `<rect x="150" y="44" width="300" height="140" rx="12" class="s-zone"/>` + text(300, 66, 'le Mac', 's-f') +
    box(170, 86, 110, 70, 'Gulliver', 's-blue') + arrow(280, 121, 330, 121, '1025') +
    box(330, 86, 100, 70, 'Bridge', 's-gold', 'SMTP') +
    text(300, 205, 'rien ne sort avant le Bridge', 's-f'), 220),
  mail_secret: schema('La clé du Bridge, rangée à part',
    box(30, 76, 150, 60, 'settings.json', 's-soft', '********') +
    box(225, 76, 150, 60, 'mail.secret', 's-gold', 'droits 0600') + arrow(375, 106, 445, 106, 'AUTH') +
    box(445, 76, 130, 60, 'Bridge', 's-blue') +
    text(300, 172, 'la page ne voit que le masque', 's-f'), 190),
  mail_link: schema('Le lien du mail ouvre la page',
    box(20, 76, 130, 60, 'Mail', 's-soft', 'le lien') + arrow(150, 106, 250, 106, 'clic') +
    box(250, 76, 150, 60, 'Page', 's-blue', 'adresse réglée') + arrow(400, 106, 480, 106) +
    box(480, 76, 100, 60, 'Mot', 's-gold', 'de passe') +
    text(300, 172, 'vide : http://adresse-du-mac:8444/', 's-f'), 190),
  relay: schema('Le relais : la question part pseudonymisée, la réponse revient en clair',
    box(16, 62, 120, 56, 'Gulliver', 's-blue', 'la discussion') + arrow(136, 90, 236, 90, 'texte') +
    box(236, 62, 128, 56, 'Pseudalis', 's-gold', 'clé de séance') + arrow(364, 90, 464, 90, 'pseudonymes') +
    box(464, 62, 120, 56, 'Moteur', 's-soft', 'Claude, Ollama…') +
    `<line x1="524" y1="118" x2="524" y2="176" class="s-line"/>` +
    arrow(524, 176, 136, 176, 'réponse : les vrais noms remis au fil du flux') +
    box(16, 154, 120, 44, 'Réponse', 's-blue') +
    text(300, 222, 'les images restent sur le Mac : un modèle local les décrit, la description part protégée', 's-f'), 235),
  warm_history: schema('Pendant que tu lis, le moteur prépare la suite',
    box(20, 70, 130, 56, 'Réponse finie', 's-blue') + arrow(150, 98, 250, 98) +
    box(250, 70, 150, 56, 'Relecture', 's-gold', 'en arrière-plan') + arrow(400, 98, 480, 98) +
    box(480, 70, 100, 56, 'Prête', 's-soft') +
    text(300, 160, 'question suivante : seule la question est encore à lire', 's-f'), 185),
  spin_us: schema('Entre deux passes, les threads attendent',
    `<rect x="40" y="80" width="200" height="40" rx="8" class="s-box s-gold"/>` + text(140, 105, 'attente active') +
    `<rect x="240" y="80" width="320" height="40" rx="8" class="s-box s-soft"/>` + text(400, 105, 'sommeil') +
    arrow(240, 150, 240, 124) + text(240, 168, 'spin_us : la limite', 's-f') +
    text(300, 196, 'actif : repart tout de suite, mais le cœur reste occupé', 's-f'), 210),
  gpu_prompt_tokens: schema('À partir de combien de tokens le GPU lit',
    `<line x1="40" y1="110" x2="560" y2="110" class="s-line"/>` +
    [8, 16, 24, 32, 64, 128].map((n, i) => `<line x1="${60 + i * 95}" y1="102" x2="${60 + i * 95}" y2="118" class="s-line"/>` +
      text(60 + i * 95, 140, String(n), 's-f')).join('') +
    `<rect x="60" y="66" width="190" height="28" rx="6" class="s-gold"/>` + text(155, 85, 'processeur') +
    `<rect x="250" y="66" width="310" height="28" rx="6" class="s-blue"/>` + text(405, 85, 'GPU') +
    text(300, 176, 'mesuré sur le M2 : le GPU perd à 16 tokens, gagne dès 24', 's-f'), 195),
  draft_tokens: schema('Un brouillon, vérifié en une passe',
    box(20, 70, 150, 56, 'Conversation', 's-soft', '« … le moteur Metal »') + arrow(170, 98, 230, 98) +
    box(230, 70, 150, 56, 'Brouillon', 's-gold', 'jusqu’à 7 mots') + arrow(380, 98, 440, 98) +
    box(440, 70, 140, 56, 'Une passe', 's-blue', 'garde les bons') +
    text(300, 160, 'les mots justes sont gardés ; au premier faux, le modèle reprend la main', 's-f'), 185),
  gpu_prompt_context: schema('Jusqu’à quelle longueur le GPU lit',
    `<rect x="40" y="70" width="320" height="40" rx="8" class="s-blue"/>` + text(200, 95, 'GPU') +
    `<rect x="360" y="70" width="200" height="40" rx="8" class="s-gold"/>` + text(460, 95, 'processeur') +
    arrow(360, 140, 360, 114) + text(360, 158, 'limite (par défaut : la longueur du modèle)', 's-f') +
    text(300, 190, 'le GPU lit plus vite à toutes les longueurs mesurées', 's-f'), 205),
  models_dir: schema('Où vivent les modèles',
    box(30, 76, 150, 56, 'Service', 's-blue') + arrow(180, 104, 300, 104, 'lit') +
    box(300, 60, 260, 88, 'Dossier des modèles', 's-gold', 'vide : dans les données') +
    text(300, 180, 'un disque externe convient, s’il reste branché', 's-f'), 200),
  reserve_gb: schema('La réserve : un placard à côté de l’atelier',
    box(20, 70, 150, 64, 'Modèles', 's-blue', 'installés') + arrow(170, 92, 300, 92, 'clone') +
    box(300, 60, 170, 84, 'Réserve', 's-gold', 'gardés par empreinte') + arrow(300, 124, 170, 124, 'restaure') +
    box(500, 70, 80, 64, 'HF', 's-soft') + arrow(500, 102, 470, 102) +
    text(300, 178, 'clone d’un modèle installé : aucune place ; plafond : les copies des modèles supprimés', 's-f') +
    text(300, 198, 'au-delà du plafond, les plus anciennes partent d’abord', 's-f'), 215),
  library_dir: schema('La bibliothèque : une étagère que le service consulte',
    box(20, 70, 170, 64, 'Bibliothèque', 's-gold', 'vos fichiers .gguf') + arrow(190, 102, 330, 102, 'clone') +
    box(330, 70, 150, 64, 'Gulliver', 's-blue', 'modèles installés') +
    box(510, 70, 70, 64, 'HF', 's-soft') + arrow(510, 102, 480, 102) +
    text(300, 172, 'lue sans réseau, jamais modifiée ; « Rafraîchir depuis le Mac » en donne la liste', 's-f') +
    text(300, 192, 'une copie par clone APFS : aucune place prise sur le même disque', 's-f'), 210)
};
