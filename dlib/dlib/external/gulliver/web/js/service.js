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

import * as api from './api.js';

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

const schemas = {
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

// ---------------------------------------------------------------------------------------------------
// Les réglages et leurs fiches.

const contexts = [0, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144];
const kvSizes = [0, 512, 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384];

function formatTokens(n) {
  return n >= 1024 ? (n / 1024) + ' K tokens' : n + ' tokens';
}

function formatMb(n) {
  return n >= 1024 ? (n / 1024).toLocaleString('fr-FR') + ' Go' : n + ' Mo';
}

/*
    Les réglages conseillés pour ce Mac, d'après les mesures du portage (MacBook Air M2, 24 Go, journal du fork) :
    le bouton « Revenir aux réglages conseillés » les remet, et chaque carte dit sa valeur conseillée quand elle en
    diffère.  À tenir à jour à chaque mesure qui change le meilleur choix (règle de CLAUDE.md).  Les réglages
    personnels (ports, accès réseau, dossier des modèles, langue) n'y sont pas.
*/
const recommended = {
  accelerator: 'auto',        // GPU : écriture 21-22 contre 18-19 tokens/s (4B), lecture x1,7 (épisodes 19, 20)
  gpu_prompt_tokens: 24,      // le GPU perd à 16 tokens, gagne dès 24 (épisode 20)
  gpu_prompt_context: 262144, // attention matricielle (4.11.0), 2B, mesures alternées : GPU 423-424 / 295 / 171 contre 170-177 / 115-117 / 67 à 4 096 / 8 192 / 16 384
  threads: 0,                 // tous les cœurs physiques (épisode 16) ; 18 sur le M5 Pro, meilleur que 14, 12, 10, 6 (mesure du 2026-11-19)
  spin_us: 2000,
  max_sequences: 4,
  context: 0,
  kv_mb: 0,
  kv_type: 'auto',            // q8 : moitié moins de mémoire, mêmes réponses dans les essais (Albireo 1.0.12)
  warm_history: true,
  offline: false,
  web_access: true,
  web_search: 'wikipedia',
  proxy: '',
  rag_engine: 'hybrid',   // mots + sens : le plus complet par défaut (à mesurer sur le Mac mini)
  rag_top_k: 12,
  rag_rerank: false       // tri fin optionnel : laissé éteint par défaut (gain à mesurer sur le Mac mini)
};

const groups = [
  {
    title: 'Le moteur',
    note: 'Ces réglages demandent un redémarrage du service : quelques secondes, le temps de recharger les modèles.',
    items: ['accelerator', 'gpu_prompt_tokens', 'gpu_prompt_context', 'threads', 'spin_us', 'max_sequences', 'context',
      'kv_mb', 'kv_type', 'warm_history']
  },
  {
    title: 'Documents',
    items: ['rag_engine', 'rag_top_k', 'rag_rerank']
  },
  {
    title: 'Modèles',
    note: 'Le dossier des modèles demande un redémarrage ; la bibliothèque et la réserve s’appliquent tout de suite.',
    items: ['models_dir', 'library_dir', 'reserve_gb']
  },
  {
    title: 'Accès depuis le réseau',
    note: 'Pour ouvrir Gulliver depuis un téléphone ou une tablette de la maison.',
    items: ['lan', 'lan_port', 'port']
  },
  {
    title: 'Internet',
    note: "Ces réglages s'appliquent tout de suite, sans redémarrage.",
    items: ['offline', 'web_access', 'web_search', 'proxy']
  },
  {
    title: 'Moteurs extérieurs',
    note: 'Pour envoyer une discussion à un modèle qui ne tourne pas sur le Mac. S’applique tout de suite.',
    items: ['relay']
  }
];

const items = {
  accelerator: {
    title: 'GPU du Mac',
    control: { kind: 'switch', on: 'auto', off: 'none' },
    restart: true,
    effect: 'Allumé : écriture environ 15 % plus rapide et lecture des documents plus de deux fois plus rapide.',
    principle: "Le moteur confie les gros calculs au GPU de la puce Apple (Metal), et garde le processeur pour le reste.",
    pros: ['Écrit plus vite : 21 à 22 tokens/s au lieu de 18 à 19 pour le modèle de 4B, sur un M2.',
      'Lit les prompts de 24 tokens ou plus deux à trois fois plus vite, à toutes les longueurs.',
      'Contrôlé à chaque chargement : au moindre écart, le moteur repasse seul au processeur.'],
    cons: ['Pas identique au dernier chiffre près au processeur (écart de 0,04 % à quelques % sur les scores, même mot choisi).',
      'Environ 200 Mo de mémoire en plus pour ses tampons.',
      'Seuls les poids en Q4_K et Q6_K y passent (aussi Q5_K, Q8_0 et Q5_0 pour Qwen 3.5, LFM2 et les mélanges d’experts).',
      'Restent sur le processeur : Gemma 3 1B (largeur de 1 152), Gemma 4 26B-A4B (ses experts), le modèle de recherche, ' +
        'les passes qui lisent une image, et les prompts des mélanges d’experts autres que Qwen 3.6.'],
    normal: "Le Mac a deux moteurs de calcul sur la même puce : le processeur, polyvalent, et le GPU, fait pour répéter " +
      "la même opération sur des milliers de nombres à la fois. Écrire une réponse, c’est relire tout le modèle pour " +
      "chaque mot : le GPU lit cette mémoire plus vite. Lire un long document, c’est surtout multiplier : le GPU calcule " +
      "plus. Le moteur l’emploie pour ces deux travaux et vérifie au chargement qu’il donne les mêmes réponses.",
    owl: "Le moteur ouvre un MTLDevice et compile ses noyaux Metal au démarrage (newLibraryWithSource). Les poids du " +
      "fichier GGUF sont pris sans copie (newBufferWithBytesNoCopy sur la projection mmap alignée sur les pages), de même " +
      "que la zone des clés et valeurs. Un pas d’écriture (1 à 8 tokens) est encodé en une seule commande : matvec Q4_K/Q6_K " +
      "en simdgroups, RMSNorm, RoPE, écriture des clés et valeurs q8 tournées (Walsh-Hadamard), attention en ligne par morceaux de 256 " +
      "positions puis fusion. Un prompt de 24 tokens ou plus décompresse chaque matrice en float32 puis passe par " +
      "MPSMatrixMultiplication (les experts de Qwen 3.6 par produits groupés, matmul_id) ; son attention multiplie des " +
      "matrices 8 × 8 (simdgroup_matrix), les têtes de 512 de Gemma 4 en deux tranches de 256 pour tenir dans les 32 Ko " +
      "de mémoire de threadgroup. Au chargement, une sonde de 29 tokens et deux pas sont comparés au processeur en " +
      "produits flottants : logits à 1 % près (5 % en clés 8 bits, 25 % pour un mélange d’experts en 8 bits), même " +
      "argmax, sinon repli.",
    marmot: "Imagine une cuisine avec un chef et une brigade de commis. Le chef (le processeur) sait tout faire, mais " +
      "un à la fois. Les commis (le GPU) épluchent mille carottes en même temps. Comme ils partagent le même plan de " +
      "travail, personne ne transporte rien : on leur donne les carottes, et le repas est prêt plus vite.",
    bee: "Coupé, tout repasse sur le processeur : les réponses restent justes, mais arrivent un peu moins vite, et un long " +
      "document met plus de deux fois plus de temps à être lu. Utile pour comparer, ou si le contrôle du chargement " +
      "signale un souci dans le journal du service."
  },
  threads: {
    title: 'Threads du moteur',
    control: { kind: 'range', values: (env) => [0].concat(Array.from({ length: env.logical }, (_, i) => i + 1)),
      label: (v, env) => v === 0 ? 'Auto (' + env.cores + ')' : String(v) },
    restart: true,
    effect: 'Combien de cœurs du processeur le moteur occupe. Auto : tous les cœurs physiques.',
    principle: 'Le moteur partage les calculs du processeur entre plusieurs threads, un par cœur. Plus de threads, plus de calcul en parallèle, jusqu’au nombre de cœurs.',
    pros: ['Auto donne le meilleur résultat sur un Mac qui ne fait que ça.',
      'Moins de threads laisse des cœurs libres pour le reste (visio, montage, compilation).'],
    cons: ['Moins de threads : lecture des prompts sur le processeur plus lente, presque en proportion.',
      'Au-delà du nombre de cœurs, aucun gain : les threads se gênent.'],
    normal: "Le moteur lance un thread par cœur. Le processeur du M2 a huit cœurs : quatre rapides (performance) et " +
      "quatre économes (efficacité). Pour écrire un token, il n’y emploie que l’équipe rapide, car attendre les cœurs lents " +
      "ferait perdre du temps ; pour lire un long prompt sur le processeur, il les prend tous. Le Mac mini M5 Pro a dix-huit " +
      "cœurs tous rapides (six « Super », douze « Performance ») : il n’y a pas d’équipe rapide, et Auto (18) est le meilleur " +
      "choix mesuré, en lecture comme en écriture (devant 14, 12, 10 et 6). Avec le GPU allumé, ce réglage compte moins : " +
      "le GPU fait la plus grosse part.",
    owl: "La thread pool d’Gulliver crée N threads (0 = cœurs physiques, sysctl hw.physicalcpu) qui attendent en spin " +
      "2 ms au plus entre deux passes. Sur Apple Silicon, les passes de moins de 24 tokens tournent sur une « fast team » " +
      "limitée à hw.perflevel0.physicalcpu threads (cœurs P), pour éviter que la barrière de fin de passe n’attende un " +
      "cœur E — seulement si hw.perflevel1.name vaut « Efficiency » (Gulliver 6.4.0 : sur un M5 Pro, Super + Performance, " +
      "l’équipe de 6 écrivait à 47 tokens/s contre 65 sur les 18 cœurs, 4B Thinking). Les produits des prompts sur le processeur passent par Accelerate (cblas_sgemm sur le coprocesseur " +
      "matriciel), alimenté par les threads qui décompressent les poids par tranches de 256 lignes.",
    marmot: "C’est comme le nombre de caisses ouvertes au supermarché. Plus il y en a, plus la file avance, mais il " +
      "n’y a que huit caissiers dans le magasin du M2 (dix-huit dans celui du Mac mini) : en ouvrir plus ne sert à rien. Et si tu en fermes quelques-unes, les " +
      "caissiers libres peuvent ranger les rayons pendant ce temps.",
    bee: "Trop bas, le moteur se traîne, surtout pour lire de longs documents sans GPU. Trop haut, les threads se " +
      "disputent les cœurs et le Mac devient moins réactif sans aller plus vite."
  },
  max_sequences: {
    title: 'Questions traitées ensemble',
    control: { kind: 'range', values: () => Array.from({ length: 16 }, (_, i) => i + 1), label: (v) => String(v) },
    restart: true,
    effect: 'Combien de conversations un modèle avance en même temps. 4 par défaut.',
    principle: 'Le moteur regroupe plusieurs questions dans le même passage du modèle : relire les poids une fois sert à toutes.',
    pros: ['Plusieurs onglets ou appareils obtiennent leur réponse en même temps, sans attendre leur tour.',
      'Le débit total augmente : un passage pour plusieurs questions coûte à peine plus qu’un seul.'],
    cons: ['Chaque question occupe sa propre mémoire de contexte : plus de questions, plus de mémoire réservée.',
      'Chacune avance un peu moins vite quand elles sont nombreuses.'],
    normal: "Quand deux questions arrivent en même temps, le moteur peut les faire avancer ensemble : à chaque passage, " +
      "il écrit un mot pour chacune. Le plus cher, relire tout le modèle, n’est payé qu’une fois. Au-delà du nombre " +
      "réglé, les questions suivantes attendent leur tour. Seul sur ton Mac, 2 à 4 suffisent largement.",
    owl: "max_sequences borne le nombre de séquences actives par modèle dans l’ordonnanceur (1 à 64). Les pas de " +
      "génération de plusieurs séquences forment un forward_batch : chaque matvec traite T tokens d’un coup (jusqu’à " +
      "8 sur le GPU, metal_device::max_tokens), donc le coût mémoire-bande des poids est amorti sur T. Chaque séquence " +
      "possède ses blocs de clés et valeurs dans le kv_pool ; le budget kv_mb est partagé entre elles.",
    marmot: "Pense à un bus plutôt qu’à un taxi. Le trajet coûte presque pareil avec un ou quatre passagers, donc on " +
      "en prend plusieurs. Mais chaque passager a besoin de son siège : un bus de seize places prend plus de place " +
      "au garage.",
    bee: "À 1, une seconde question (un autre onglet, le téléphone) attend que la première soit finie. Trop haut, la " +
      "mémoire réservée aux contextes est partagée en petits morceaux, et un long document peut ne plus tenir."
  },
  context: {
    title: 'Contexte des modèles',
    control: { kind: 'range', values: () => contexts, label: (v) => v === 0 ? 'Auto (celui du modèle)' : formatTokens(v) },
    restart: true,
    effect: 'La longueur de texte qu’un modèle garde en tête. Auto : celle de son entraînement, réduite si la mémoire manque.',
    principle: 'Le contexte est la fenêtre de tokens que le modèle relit pour écrire le mot suivant : la conversation, les documents, sa réponse.',
    pros: ['Plus grand : de longs documents et de longues conversations sans oubli.',
      'Plus petit : moins de mémoire, et un démarrage plus léger.'],
    cons: ['Plus grand : plus de mémoire pour les clés et valeurs, et une écriture qui ralentit quand le texte s’allonge.',
      'Plus petit : le modèle oublie le début des longues conversations, ou refuse un document trop long.'],
    normal: "Un modèle ne lit pas un livre : il voit une fenêtre de texte, mesurée en tokens (des morceaux de mots). " +
      "Tout ce qui est dans la fenêtre compte pour la réponse ; ce qui en sort est oublié. En auto, le moteur prend la " +
      "fenêtre prévue par le modèle et la réduit d’elle-même si la mémoire du Mac ne peut pas la contenir.",
    owl: "context = 0 prend n_ctx_train du GGUF, puis le moteur le ramène à kv_bytes_budget / kv_bytes_per_token " +
      "(journal : « context reduced from … to … »). Les clés et valeurs coûtent 2 × couches × têtes KV × head_dim × " +
      "octets par token : pour le 4B du service (36 couches, 8 têtes KV de 128) en q8, environ 76 Ko par token, " +
      "0,3 Go pour 4 096 tokens ; sur un Mac de 24 Go, l’auto lui donne 250 112 tokens sur 262 144. L’attention de chaque pas lit toutes " +
      "les clés : coût linéaire en longueur pour l’écriture, quadratique pour la lecture d’un prompt. Sur le GPU, les " +
      "prompts passent jusqu’à la longueur fixée par le réglage « GPU : prompts jusqu’à » (par défaut, celle du modèle).",
    marmot: "C’est la taille de ton bureau. Sur un petit bureau, tu ne poses que deux pages : pour lire la troisième, " +
      "tu en ranges une et tu l’oublies. Un grand bureau garde tout le dossier ouvert, mais il prend de la place dans " +
      "la pièce et tu mets plus longtemps à tout parcourir des yeux.",
    bee: "Trop petit, le modèle oublie le début de la conversation ou refuse les documents longs. Trop grand pour la " +
      "mémoire, le moteur le réduit de lui-même au chargement : le réglage n’est alors pas tenu, et le journal le dit."
  },
  kv_mb: {
    title: 'Mémoire des clés et valeurs',
    control: { kind: 'range', values: () => kvSizes, label: (v) => v === 0 ? 'Auto' : formatMb(v) },
    restart: true,
    effect: 'La mémoire réservée aux contextes des conversations. Auto : deux contextes complets, avec une réserve pour macOS.',
    principle: 'Pour ne pas tout recalculer à chaque mot, le modèle garde des « clés et valeurs » pour chaque token déjà lu. Ce réglage fixe leur budget total.',
    pros: ['Plus : des contextes plus longs, plus de conversations gardées en mémoire pour reprendre vite.',
      'Moins : plus de mémoire libre pour les autres applications.'],
    cons: ['Plus : le Mac peut manquer de mémoire et ralentir (swap).',
      'Moins : contextes réduits au chargement, conversations relues depuis le début plus souvent.'],
    normal: "Chaque token lu laisse une petite fiche dans la mémoire du moteur : ce sont les clés et valeurs. Elles " +
      "permettent d’écrire le mot suivant sans relire tout le texte depuis le début. Ce réglage dit combien de mémoire " +
      "leur donner. En auto, le moteur prend de quoi tenir deux contextes complets tout en laissant de la marge au système.",
    owl: "kv_mb = 0 : budget = 2 contextes dans la mémoire engageable (sur macOS, la mémoire physique), moins une " +
      "réserve ; sinon kv_mb mégaoctets. Le kv_pool réserve une seule région mmap pour tous ses blocs (pages alignées), " +
      "engagée et rendue par madvise (MADV_FREE_REUSABLE / REUSE) : seuls les blocs utilisés coûtent de la mémoire " +
      "réelle. Le GPU lit cette même région sans copie. Le contexte de chaque modèle est plafonné à ce que le budget peut " +
      "tenir (18,7 Go de clés et valeurs pour le 4B, sur un Mac de 24 Go).",
    marmot: "C’est la taille de ton classeur de notes. Un gros classeur garde les notes de toutes tes matières, tu " +
      "retrouves vite où tu en étais. Mais il prend de la place dans ton sac, et il en reste moins pour le reste.",
    bee: "Trop gros, le Mac commence à échanger avec le disque et tout ralentit, y compris les autres applications. " +
      "Trop petit, les longs documents ne tiennent plus et le moteur réduit le contexte au chargement."
  },
  kv_type: {
    title: 'Format des clés et valeurs',
    control: { kind: 'choice', options: [['auto', 'Auto'], ['q8', '8 bits'], ['f16', '16 bits']] },
    restart: true,
    effect: 'Auto : 8 bits pour les modèles de langue, presque deux fois moins de mémoire pour le même contexte.',
    principle: 'Les clés et valeurs peuvent être rangées en demi-précision (16 bits) ou en entiers de 8 bits avec une échelle par groupe de 32.',
    pros: ['8 bits : presque moitié moins de mémoire, donc des contextes deux fois plus longs.',
      '16 bits : aucun arrondi supplémentaire.'],
    cons: ['8 bits : un arrondi minuscule, qui peut changer un mot dans une très longue réponse.',
      '16 bits : deux fois plus de mémoire, contexte plus court à mémoire égale.'],
    normal: "Chaque fiche des clés et valeurs est une liste de nombres. En 16 bits, on les garde presque tels quels. " +
      "En 8 bits, on les arrondit, mais intelligemment : par petits groupes, avec une échelle pour chacun. La différence " +
      "de qualité est à peine mesurable, et la mémoire gagnée permet de lire des documents deux fois plus longs.",
    owl: "q8 : groupes de 32 valeurs en int8 plus une échelle en half (34 octets contre 64 en f16). Les clés et les " +
      "valeurs sont d’abord tournées (signes pseudo-aléatoires puis transformée de Walsh-Hadamard, qk_rotate_row) pour " +
      "étaler les valeurs extrêmes avant l’arrondi ; les requêtes subissent la même rotation, ce qui laisse les produits " +
      "scalaires inchangés, et la sortie de l’attention, moyenne des valeurs tournées, est remise en place " +
      "(qk_unrotate_row), comme le fait llama.cpp. Auto choisit q8 pour les modèles de langue dont la tête contient des groupes entiers de 32, f16 pour " +
      "les modèles de recherche. Noyaux NEON et Metal pour les deux formats ; le contrôle du GPU tolère 5 % sur un prompt en q8.",
    marmot: "C’est comme une photo en haute qualité ou bien compressée. Sur ton téléphone, tu ne vois presque pas la " +
      "différence, mais la photo compressée prend deux fois moins de place, et tu peux en garder deux fois plus.",
    bee: "En 16 bits, un long document peut ne plus tenir dans la mémoire réservée et le contexte est réduit. En 8 bits, " +
      "une réponse très longue peut, rarement, prendre un autre tournant qu’en 16 bits, sans être moins juste."
  },
  lan: {
    title: 'Accès depuis le réseau',
    control: { kind: 'switch', on: true, off: false },
    restart: true,
    effect: 'Ouvre la page aux appareils de la maison (http, port réseau ci-dessous), sans mot de passe.',
    principle: 'Le service ouvre une seconde porte, sur l’adresse du Mac dans ton réseau, pour le téléphone ou la tablette.',
    pros: ['Gulliver depuis le canapé, sur le téléphone ou la tablette, sans rien installer.',
      'Le Mac fait tout le calcul : le téléphone n’affiche que la page.'],
    cons: ['Pas de mot de passe : tout appareil de ton Wi-Fi peut s’en servir et tout régler.',
      'En http : le trafic n’est pas chiffré sur le Wi-Fi.',
      'Chaque appareil garde ses propres conversations : celles du téléphone ne sont pas celles du Mac.',
      'Pas de dictée au micro sur le téléphone (elle exige https).'],
    normal: "Normalement, la page n’est visible que sur le Mac lui-même. Allumé, ce réglage ouvre une seconde entrée " +
      "pour les autres appareils de ton réseau, sans mot de passe : la porte est ouverte à toute la maison. " +
      "Les conversations sont rangées dans le navigateur de chaque appareil : le " +
      "téléphone a donc les siennes. Le calcul, lui, se fait toujours sur le Mac.",
    owl: "Un second serveur httplib (HTTP, sans TLS) écoute sur 0.0.0.0:lan_port avec les mêmes routes, sans " +
      "authentification (le mot de passe facultatif des versions antérieures est retiré depuis la 5.0.0). Le serveur ne répond qu’aux appareils qui peuvent joindre le Mac : ceux du réseau local, " +
      "sauf si la box redirige ce port depuis Internet (elle ne le fait pas d’elle-même).",
    marmot: "C’est une deuxième porte d’entrée, côté jardin, sans digicode : n’importe qui dans le jardin peut " +
      "entrer. Chez toi, ce sont ta famille et tes invités.",
    bee: "Un invité, une télé connectée ou un objet piraté de ton Wi-Fi peut utiliser Gulliver, changer ses " +
      "réglages ou effacer des modèles et des documents. Sur un Wi-Fi partagé ou public, ne l’allume pas."
  },
  lan_port: {
    title: 'Port réseau',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'Le numéro de la porte réseau. L’adresse à taper sur le téléphone s’affiche plus bas.',
    principle: 'Une adresse réseau a des « ports », comme un immeuble a des portes numérotées. Celui-ci sert aux autres appareils.',
    pros: ['Séparé du port du Mac : on peut couper l’un sans toucher à l’autre.'],
    cons: ['Changer le port change l’adresse à taper sur chaque appareil.'],
    normal: "Le Mac a une adresse sur ton réseau (par exemple 192.168.1.12), et chaque service y a son numéro de porte. " +
      "Gulliver utilise 8444 par défaut pour les autres appareils. On tape donc « http://adresse-du-mac:8444/ » sur le téléphone.",
    owl: "Entier de 1024 à 65535, différent du port HTTPS du Mac. Le service vérifie qu’il peut lier 0.0.0.0 sur ce port " +
      "avant d’enregistrer (409 port_unavailable sinon). macOS peut demander, au premier lancement, d’autoriser les " +
      "connexions entrantes pour gulliver-service (pare-feu applicatif).",
    marmot: "Ton immeuble a une adresse, et chaque appartement un numéro. Les copains doivent connaître les deux pour " +
      "sonner chez toi. Changer de numéro, c’est leur redonner la nouvelle adresse.",
    bee: "Un port déjà pris par un autre programme est refusé. Si tu le changes, les favoris enregistrés sur les " +
      "appareils ne mènent plus nulle part."
  },
  port: {
    title: 'Port du Mac',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'L’adresse de la page sur ce Mac (https://localhost:port). La changer change l’adresse de la page.',
    principle: 'Le service écoute sur ce port, seulement pour le Mac lui-même (127.0.0.1), en https.',
    pros: ['Utile seulement si un autre programme veut ce numéro.'],
    cons: ['Les conversations sont rangées par adresse dans le navigateur : à la nouvelle adresse, la page est vide.',
      'Il faut mettre à jour les favoris et les outils qui appellent l’API.'],
    normal: "C’est la porte d’Gulliver sur le Mac lui-même. La page que tu regardes y est servie. Le navigateur range " +
      "les conversations selon l’adresse exacte : en changeant de port, tu arrives sur une page neuve, et les anciennes " +
      "conversations restent à l’ancienne adresse (exporte-les d’abord depuis l’onglet Données).",
    owl: "Serveur httplib SSLServer lié à 127.0.0.1:port, certificat émis par l’autorité locale du dossier de données. " +
      "Le stockage de la page (IndexedDB, localStorage) dépend de l’origine https://localhost:port. Le service vérifie " +
      "que le port est libre avant d’enregistrer ; au démarrage, un port pris le fait revenir au dernier port servi.",
    marmot: "C’est le numéro de ta chambre dans un hôtel. Si tu changes de chambre, tes affaires restent dans " +
      "l’ancienne : il faut les déménager avant.",
    bee: "Sans export préalable, tes conversations semblent avoir disparu (elles sont toujours à l’ancienne adresse). " +
      "Un programme ou un script qui parlait à l’ancien port ne trouve plus le service."
  },
  offline: {
    title: 'Hors ligne',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Plus aucune connexion vers Internet : ni mise à jour du catalogue, ni téléchargement réseau, ni recherche web.',
    principle: 'Le service coupe ses connexions sortantes ; les modèles viennent de la bibliothèque du Mac, des imports ou de la réserve locale.',
    pros: ['Rien ne sort du Mac : confidentialité totale.', 'Aucun téléchargement surprise.'],
    cons: ['Pas de modèles qui ne sont pas déjà sur le Mac, pas de bouton Web.',
      'Un téléchargement en cours s’arrête (sa progression est gardée).'],
    normal: "Gulliver calcule déjà tout sur le Mac. Il ne va sur Internet que pour trois choses : le catalogue des " +
      "modèles, leur téléchargement, et la recherche du bouton Web. Hors ligne, il n’y va plus du tout. On peut " +
      "toujours charger un modèle de la bibliothèque du Mac (« Rafraîchir depuis le Mac », section « Sur ce Mac »), " +
      "importer un modèle comme fichier, ou restaurer celui dont tous les fichiers sont vérifiés dans la réserve.",
    owl: "offline = true annule à l’instant les transferts en cours (http_cancel_all, raison « offline mode »), refuse " +
      "les routes qui ont besoin du réseau (refuse_offline : 409, code offline) et ferme " +
      "l’accès web (403, code offline). Les restaurations de la réserve vérifient taille et SHA-256, sans aucun repli " +
      "réseau, même si le réglage change pendant le travail. Le dossier d’import et la bibliothèque du Mac " +
      "(GET /gulliver/v1/library) restent lus. Remis à faux, http_restore() " +
      "rouvre les transferts.",
    marmot: "C’est le mode avion de ton téléphone, mais pour Gulliver. Tout ce qui est déjà sur le Mac marche toujours ; " +
      "seulement, plus rien ne part vers Internet, et rien n’en vient.",
    bee: "Oublié allumé, le catalogue ne se met plus à jour et le bouton Web répond « hors ligne ». Si la réserve " +
      "ne contient pas tous les fichiers vérifiés, il faut les importer ou revenir en ligne."
  },
  web_access: {
    title: 'Bouton Web',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Permet au modèle de chercher sur le web et de lire des pages pour répondre.',
    principle: 'Le service fait les recherches et lit les pages pour la page, qui les donne ensuite au modèle.',
    pros: ['Des réponses à jour, avec des sources.', 'Le modèle peut lire une adresse que tu lui donnes.'],
    cons: ['Les mots de ta recherche partent vers le site cherché.', 'Plus lent : il faut télécharger puis lire les pages.'],
    normal: "Un modèle ne connaît que ce qu’il a appris jusqu’à une certaine date. Avec le bouton Web, il peut chercher " +
      "et lire des pages, puis répondre en citant ce qu’il a lu. Coupé, il répond seulement avec ce qu’il sait déjà.",
    owl: "Routes POST /gulliver/v1/web/search (1 à 10 résultats, http et https seulement) et /gulliver/v1/web/fetch " +
      "(octets de la page en base64, adresses publiques seulement : les adresses locales et privées sont refusées pour " +
      "éviter de servir de relais vers le réseau de la maison). web_access = false les ferme (403).",
    marmot: "C’est comme autoriser ton assistant à aller à la bibliothèque. Il revient avec des livres récents et te dit " +
      "où il a trouvé l’info. Sans ça, il te répond de mémoire.",
    bee: "Coupé, les questions sur l’actualité reçoivent des réponses datées. Allumé, ce que tu cherches sort du Mac " +
      "vers Wikipédia et les sites lus."
  },
  web_search: {
    title: 'Recherche Wikipédia',
    control: { kind: 'switch', on: 'wikipedia', off: 'none' },
    restart: false,
    effect: 'Le moteur de recherche du bouton Web : Wikipédia, ou aucun.',
    principle: 'Le service interroge Wikipédia dans la langue de la question et rend les articles trouvés.',
    pros: ['Sans clé, sans compte, et des sources de bonne qualité.'],
    cons: ['Seulement ce que Wikipédia couvre : ni actualité fine, ni sites commerciaux.'],
    normal: "Quand le modèle a besoin de chercher, le service pose la question à Wikipédia et rapporte les articles. " +
      "Coupée, la recherche ne marche plus, mais le bouton Web sait toujours lire une adresse précise que tu donnes.",
    owl: "web_search = « wikipedia » ou « none ». Les anciennes valeurs (« auto », « brave », « searxng ») sont ramenées " +
      "à Wikipédia à la lecture des réglages. La recherche passe par l’API publique de Wikipédia, dans la langue de la " +
      "requête (paramètre language), via le proxy éventuel.",
    marmot: "C’est le moteur de recherche que ton assistant utilise : une grande encyclopédie. Pratique et fiable, mais " +
      "il n’y trouvera pas le prix du dernier jeu vidéo.",
    bee: "Coupée, le modèle ne trouve plus rien seul : il faut lui donner les adresses des pages à lire."
  },
  gpu_prompt_tokens: {
    title: 'GPU : prompts à partir de',
    control: { kind: 'range', values: () => [16, 24, 32, 48, 64, 128, 256, 512], label: (v) => v + ' tokens' },
    restart: true,
    effect: 'La longueur à partir de laquelle un prompt est lu par le GPU plutôt que par le processeur.',
    principle: 'Le GPU doit d’abord déplier les poids de chaque couche : cela ne vaut la peine que pour des passes assez longues.',
    pros: ['24 : le seuil mesuré où le GPU devient plus rapide que le processeur sur le M2.',
      'Plus haut : les petites questions restent sur le processeur, qui n’a rien à déplier.'],
    cons: ['Plus bas que 24 : le GPU perd du temps à déplier les poids pour peu de tokens.',
      'Plus haut : les prompts moyens perdent l’accélération du GPU.'],
    normal: "Lire un prompt, c’est multiplier chaque token par tous les poids du modèle. Le GPU le fait très vite, mais il " +
      "doit d’abord décompresser les poids, ce qui prend un temps fixe. Pour une petite question, ce temps fixe coûte " +
      "plus que ce qu’il fait gagner. Ce réglage dit à partir de combien de tokens le GPU prend la main.",
    owl: "Sur le chemin des prompts, chaque matrice Q4_K/Q6_K est décompressée en float32 (dequant_q4_K/q6_K) puis " +
      "multipliée par MPSMatrixMultiplication : environ 200 ms de décompression par passe pour le 2B. Mesures (2B, une " +
      "passe, clés q8) : 12 tokens 85,7 contre 59,5 tokens/s (processeur gagnant), 16 tokens 84,4 contre 77,7, 24 tokens " +
      "63,2 contre 114,3 (GPU gagnant). Le seuil vaut 9 au moins : les passes de 1 à 8 tokens sont des pas d’écriture, " +
      "toujours sur le GPU. 24 coïncide avec qk_float_product_tokens, où le processeur passe lui-même à Accelerate.",
    marmot: "Sortir le grand robot de cuisine pour couper une seule carotte, c’est plus long que de la couper au " +
      "couteau. Pour un sac entier, le robot gagne. Ce réglage dit à partir de combien de carottes on le sort.",
    bee: "Trop bas, les petites questions démarrent plus lentement. Trop haut, les documents moyens sont lus deux fois " +
      "moins vite qu’ils pourraient l’être."
  },
  gpu_prompt_context: {
    title: 'GPU : prompts jusqu’à',
    control: { kind: 'range', values: () => [0, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144],
      label: (v) => v === 0 ? 'jamais (processeur)' : formatTokens(v) },
    restart: true,
    effect: 'La longueur de contexte jusqu’à laquelle le GPU lit les prompts. Au-delà, le processeur reprend.',
    principle: 'Le GPU lit les prompts dont le contexte reste sous cette longueur ; au-delà, le processeur.',
    pros: ['Par défaut, la longueur du modèle : le GPU lit tout, et il gagne à toutes les longueurs mesurées.',
      'Plus bas : seulement pour comparer, ou si une machine donnait l’avantage au processeur sur les longs textes.'],
    cons: ['Plus bas : les longs documents sont lus par le processeur, deux à trois fois plus lentement.',
      '0 : plus aucun prompt sur le GPU, la lecture redevient deux fois plus lente.'],
    normal: "Pour chaque token d’un prompt, le modèle relit tous ceux qui le précèdent. Sur un long document, ce travail " +
      "grossit vite. L’attention du GPU partage chaque tranche de clés entre plusieurs tokens et calcule par petites " +
      "matrices : elle reste plus rapide que le processeur même sur de très longs textes. Ce réglage fixe une frontière, " +
      "par défaut au bout du modèle.",
    owl: "Le chemin des prompts sur le GPU (takes_gpu) exige que la séquence finisse dans gpu_prompt_context positions. " +
      "L’attention de prompt (attention_mtile) prend 32 lignes de requête (16 pour des têtes de 256 valeurs, 8 pour " +
      "celles de 512 de Gemma 4, coupées en deux tranches de 256 dont chacune calcule tous les scores) par tête " +
      "de clés, charge chaque tranche de 32 positions une fois en mémoire de threadgroup (8 valeurs par thread, une " +
      "échelle par groupe) et calcule scores et sorties par simdgroup_matrix 8 x 8 ; la fenêtre glissante de Gemma 3 " +
      "et 4 y est appliquée. Mesures alternées (bench_runtime, " +
      "clés q8, 2B, Gulliver 4.11.0) : 4 096 tokens 423 à 424 tokens/s contre 170 à 177 sur le processeur ; 8 192 tokens " +
      "295 contre 115 à 117 ; 16 384 tokens 171 contre 67.",
    marmot: "C’est comme un livreur qui a appris à charger tout un carton au lieu d’un colis à la fois : même pour les " +
      "longues tournées, il reste le plus rapide. Ce réglage dit jusqu’où on l’envoie ; par défaut, partout.",
    bee: "Trop bas, les longs documents sont lus par le processeur, deux à trois fois plus lentement. À 0, le GPU ne lit " +
      "plus aucun prompt : l’écriture reste rapide, mais la lecture redevient deux fois plus lente."
  },
  spin_us: {
    title: 'Attente active des threads',
    control: { kind: 'range', values: () => [0, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000],
      label: (v) => v === 0 ? 'aucune' : v >= 1000 ? (v / 1000) + ' ms' : v + ' µs' },
    restart: true,
    effect: 'Combien de temps les threads restent éveillés entre deux passes avant de dormir. 2 ms par défaut.',
    principle: 'Un thread qui dort met du temps à se réveiller ; un thread qui attend éveillé repart tout de suite, mais occupe son cœur.',
    pros: ['Plus long : les passes s’enchaînent sans délai de réveil.',
      'Plus court : moins de chaleur et de batterie quand le moteur attend.'],
    cons: ['Plus long : les cœurs tournent à vide entre deux questions, le Mac chauffe un peu.',
      'Aucune : chaque passe paie le réveil des threads, l’écriture ralentit sur le processeur.'],
    normal: "Entre deux étapes de calcul, les threads du moteur peuvent soit attendre éveillés, soit s’endormir. " +
      "Éveillés, ils repartent instantanément ; endormis, ils économisent l’énergie mais mettent un peu de temps à " +
      "redémarrer. Avec le GPU, qui écrit les réponses, ce réglage compte surtout pour la lecture sur le processeur.",
    owl: "thread_pool::set_spin_time : après une section parallèle, chaque worker boucle (ALBIREO_CPU_RELAX, soit " +
      "std::this_thread::yield sur ARM64) jusqu’à spin_us microsecondes, puis attend sur une variable de condition. " +
      "Défaut 2 000 µs, plafond 100 000 µs (au-delà, le processeur resterait occupé entre deux passes). Sur macOS, " +
      "yield fait tomber la priorité du thread : l’attente active coûte peu aux autres programmes.",
    marmot: "C’est la différence entre attendre le bus debout à l’arrêt, prêt à monter, et rentrer s’asseoir au café " +
      "d’à côté. Au café, tu te reposes, mais tu rates parfois le départ.",
    bee: "Trop long, le Mac sans ventilateur chauffe et la batterie descend même quand personne ne pose de question. " +
      "À zéro, les réponses écrites par le processeur ralentissent."
  },
  warm_history: {
    title: 'Préparer la question suivante',
    control: { kind: 'switch', on: true, off: false },
    restart: true,
    effect: 'Après chaque réponse, relit la conversation en arrière-plan pour que la question suivante parte plus vite.',
    principle: 'Pendant que tu lis la réponse, le moteur relit l’historique tel que la prochaine question le présentera.',
    pros: ['La question suivante ne paie que sa propre lecture : réponse plus rapide.'],
    cons: ['Du calcul après chaque réponse, même si tu ne poses plus de question.',
      'Un peu de mémoire pour garder cette relecture.'],
    normal: "À chaque tour, le modèle doit relire toute la conversation. Mais la réponse qu’il a écrite n’est pas " +
      "rangée exactement comme il la relira (les modèles qui raisonnent effacent leur raisonnement des tours passés). " +
      "Le moteur profite donc du temps où tu lis pour préparer cette relecture. Ta question suivante démarre alors " +
      "presque tout de suite.",
    owl: "Quand une réponse de chat finit, son historique est rendu comme la requête suivante le rendra (gabarit de chat, " +
      "raisonnement des tours passés retiré, réponse retokenisée), puis lu en arrière-plan (warm-up, priorité basse). " +
      "Les clés et valeurs restent en cache ; une requête qui commence par les mêmes tokens les reprend. Une requête " +
      "d’un client qui attend interrompt les warm-ups (yield_warm_ups). warm_history = false les supprime.",
    marmot: "C’est comme relire tes notes du cours pendant la pause, pour être prêt quand le prof repose une question. " +
      "Si tu ne l’as pas fait, il faut tout relire au moment de répondre.",
    bee: "Coupé, chaque nouvelle question d’une longue conversation commence par relire tout l’historique : plusieurs " +
      "secondes d’attente avant le premier mot."
  },
  models_dir: {
    title: 'Dossier des modèles',
    control: { kind: 'text', placeholder: 'vide : dans les données du service' },
    restart: true,
    effect: 'Où le service range et cherche ses modèles. Vide : le dossier « models » de ses données.',
    principle: 'Les modèles sont de gros fichiers (2 à 4 Go) : on peut les ranger ailleurs, sur un autre disque.',
    pros: ['Libère le disque du Mac en mettant les modèles sur un disque externe.'],
    cons: ['Les modèles de l’ancien dossier ne sont pas déplacés : il faut le faire dans le Finder.',
      'Disque débranché : plus de modèle au démarrage.'],
    normal: "Le service garde ses modèles dans un dossier. En changer, c’est lui dire de regarder ailleurs : il ne " +
      "déplace rien lui-même. Déplace d’abord les fichiers .gguf dans le nouveau dossier, puis indique-le ici. Le " +
      "dossier doit exister.",
    owl: "models_dir vide : <données>/models (effective_models_dir). Le service vérifie que le dossier existe avant " +
      "d’enregistrer. Les modèles installés sont reconnus par leurs fichiers dans ce dossier ; les poids sont ouverts " +
      "par mmap et pris par le GPU sans copie, donc un disque lent ralentit surtout le chargement.",
    marmot: "C’est le placard où tu ranges tes jeux. Si tu changes de placard, il faut porter les jeux dedans ; sinon " +
      "tu ouvres un placard vide.",
    bee: "Un dossier vide ou un disque débranché, et le service démarre sans modèle : plus de réponses jusqu’à ce que " +
      "les fichiers reviennent."
  },
  library_dir: {
    title: 'Bibliothèque du Mac',
    control: { kind: 'text', placeholder: 'vide : le dossier « library » des données du service' },
    restart: false,
    effect: 'Le dossier de fichiers .gguf que « Rafraîchir depuis le Mac » liste dans l’onglet Modèles, pour les charger sans réseau.',
    principle: 'Des modèles déjà présents sur le Mac, venus d’ailleurs qu’Gulliver, deviennent utilisables même sans Hugging Face.',
    pros: ['Un modèle se charge sans Internet, en quelques secondes : le temps de vérifier son empreinte.',
      'La copie ne prend pas de place sur le même disque (clone APFS), et le fichier reste dans la bibliothèque.',
      'Chaque modèle vient avec son projecteur d’images, quand le dossier le contient.'],
    cons: ['Sur un autre disque que celui des modèles, la copie prend vraiment sa taille.',
      'Le service refuse un dossier que d’autres comptes du Mac peuvent modifier.'],
    normal: "Dépose des fichiers .gguf dans ce dossier avec le Finder, puis clique « Rafraîchir depuis le Mac » dans " +
      "l’onglet Modèles : la section « Sur ce Mac » les liste, avec leur architecture, leur taille et le projecteur " +
      "qui va avec chacun. « Charger dans Gulliver » en fait une copie dans le dossier des modèles. Le service ne " +
      "déplace, ne renomme et n’efface jamais rien dans la bibliothèque. Vide : <données>/library, créé à la première " +
      "lecture. Un autre dossier doit exister et être nommé par son chemin complet.",
    owl: "library_dir vide : <données>/library (effective_library_dir), créé par make_dirs. GET /gulliver/v1/library : " +
      "le dossier doit passer folder_admin_only (propriétaire, permissions POSIX, ACL) ; chaque .gguf du dossier, sans " +
      "sous-dossier, est ouvert par link_free_file (aucun lien, un seul nom) et décrit par describe_gguf ; les " +
      "projecteurs sont appariés par pair_projectors, comme un import. POST /gulliver/v1/library/load {file} : " +
      "model_manager::import_files(fichier et projecteur, move = false), SHA-256 par CommonCrypto, copie par " +
      "fclonefileat. « Déjà dans Gulliver » : un modèle installé de même taille et de même architecture ; l’import " +
      "tranche par l’empreinte.",
    marmot: "C’est l’étagère de ta chambre où tu ranges des jeux que tu as eus ailleurs. Le service regarde l’étagère " +
      "et te montre ce qu’il y a ; si tu choisis un jeu, il en fait une photocopie magique qui ne prend pas de place, " +
      "et laisse l’original sur l’étagère.",
    bee: "Un dossier que n’importe quel compte du Mac peut modifier, et le service ne le lit plus : quelqu’un pourrait " +
      "y glisser un faux modèle. Un dossier sur une clé USB débranchée, et la liste est vide."
  },
  reserve_gb: {
    title: 'Réserve des modèles',
    control: { kind: 'range', values: () => [0, 50, 100, 200, 300, 500, 1000],
      label: (v) => v === 0 ? 'Rien de plus' : v.toLocaleString('fr-FR') + ' Go' },
    restart: false,
    effect: 'La place que peuvent prendre, sur le Mac, les modèles supprimés gardés pour revenir sans réseau.',
    principle: 'Chaque modèle téléchargé est gardé dans la réserve ; le réinstaller devient une copie locale, sans Internet.',
    pros: ['Réinstaller un modèle prend quelques secondes, sans réseau, même si Hugging Face est en panne.',
      'Tant qu’un modèle reste installé, sa copie de réserve ne prend aucune place (un clone APFS).'],
    cons: ['Les modèles supprimés continuent d’occuper le disque, jusqu’au plafond.',
      'Plafond baissé : les copies les plus anciennes partent aussitôt.'],
    normal: "Quand un modèle finit de se télécharger, le service en garde une copie dans le dossier « reserve » de ses " +
      "données. Quand tu supprimes le modèle, la copie reste : le réinstaller la reprend sur le Mac, sans Internet. Le " +
      "plafond ne compte que les copies des modèles supprimés ; au-delà, les moins récemment utilisées partent d’abord. " +
      "« Vider la réserve » retire toutes les copies.",
    owl: "<données>/reserve/<sha256>.gguf, dossier 0700, fichiers 0600. La copie est un clone APFS (fclonefileat) : " +
      "les blocs sont partagés avec le fichier du modèle tant qu’aucun des deux ne change. Chaque copie est relue et " +
      "hachée (SHA-256 par CommonCrypto). Restauration fichier par fichier : en ligne, seuls les fichiers absents ou " +
      "abîmés se téléchargent ; une copie abîmée est retirée. Plafond (reserve_gb) : somme des copies dont l’empreinte " +
      "n’est plus celle d’un modèle installé ; tri par mtime, rafraîchie à chaque restauration (trim_reserve).",
    marmot: "C’est comme ranger un jeu que tu n’utilises plus dans un carton au grenier au lieu de le jeter : si tu " +
      "le veux de nouveau, tu remontes le chercher au lieu de le racheter. Le plafond, c’est la taille du grenier.",
    bee: "À 0, rien n’est gardé : un modèle supprimé se retélécharge en entier (2 à 3 Go). Trop haut, le disque se " +
      "remplit de modèles oubliés sans que tu le voies : regarde la place utilisée sous ce réglage."
  },
  proxy: {
    title: 'Proxy',
    control: { kind: 'text', placeholder: 'vide, none, ou http://hôte:port' },
    restart: false,
    effect: 'Un relais pour sortir sur Internet, si ton réseau l’exige. Vide le plus souvent.',
    principle: 'Toutes les connexions du service vers Internet passent par ce relais.',
    pros: ['Indispensable sur certains réseaux d’entreprise ou d’école.'],
    cons: ['Mal réglé, plus rien ne passe : catalogue, téléchargements et recherche échouent.'],
    normal: "Sur certains réseaux, on ne sort pas directement sur Internet : on passe par un relais, le proxy. Vide, " +
      "le service suit les variables du système (HTTPS_PROXY), et sinon sort en direct. « none » force le direct.",
    owl: "Vide : HTTPS_PROXY / HTTP_PROXY / ALL_PROXY de l’environnement du service (pas les réglages de proxy des " +
      "Réglages Système de macOS), NO_PROXY respecté, localhost toujours en direct. « none » : jamais de proxy. " +
      "Sinon http://[utilisateur:mot-de-passe@]hôte:port ; le mot de passe est rendu masqué (********) et gardé à part " +
      "dans proxy.secret. Appliqué à chaud (set_http_proxy).",
    marmot: "C’est comme passer par l’accueil d’un bâtiment pour sortir : certains endroits l’exigent. Chez toi, la " +
      "porte donne directement sur la rue, et tu n’en as pas besoin.",
    bee: "Une adresse fausse coupe Gulliver d’Internet sans prévenir : téléchargements et recherches échouent avec " +
      "« the proxy refuses the connection ». Vide-le pour revenir au réglage normal."
  },
  relay: {
    title: 'Relais vers un moteur extérieur',
    control: { kind: 'relay' },
    restart: false,
    effect: 'Envoie les discussions qui le demandent à Claude, à Ollama ou à un autre moteur, et rend leur réponse ici.',
    principle: 'Le service sert d’intermédiaire : il envoie la discussion au moteur extérieur avec ta clé, et rend sa ' +
      'réponse comme celle d’un modèle du Mac.',
    pros: ['Des modèles bien plus grands que ceux qui tiennent dans le Mac (Claude : un million de tokens de contexte).',
      'Avec Pseudalis, les noms, adresses et numéros partent remplacés par des pseudonymes, et la réponse revient ' +
        'avec les vrais.',
      'La clé reste dans le service : la page et les autres programmes ne la voient jamais.',
      'Hors ligne, un moteur du réseau de la maison (un autre ordinateur) reste joignable.'],
    cons: ['La discussion quitte le Mac : seule la pseudonymisation en retire les données personnelles, et elle ne ' +
        'reconnaît pas tout (un détail qui identifie quelqu’un sans être un nom, par exemple).',
      'Payant chez Claude et Ollama : chaque question consomme le crédit du compte.',
      'Ni outils, ni sons ; la recherche sur le web n’existe qu’avec Claude.',
      'Avec l’accès depuis le réseau ouvert, tout appareil du Wi-Fi peut y envoyer des discussions, donc utiliser ' +
        'ton crédit ; ses réglages, eux, ne se changent que sur le Mac.'],
    normal: "Un modèle du Mac répond sans que rien ne sorte. Pour une question difficile, on peut préférer un modèle " +
      "beaucoup plus grand, qui tourne chez Anthropic (Claude), chez Ollama, ou sur un autre ordinateur. Le relais " +
      "envoie alors la discussion à ce moteur, avec ta clé, puis rend sa réponse dans Gulliver. Avec la " +
      "pseudonymisation, Pseudalis remplace d’abord chaque donnée personnelle par un pseudonyme ; le moteur ne voit " +
      "que les pseudonymes, et Gulliver remet les vrais noms dans la réponse pendant qu’elle s’écrit. Les images ne " +
      "partent pas : un modèle du Mac qui lit les images les décrit, et la description part protégée elle aussi.",
    owl: "Le modèle albireo-relay de l’interface (POST /v1/chat/completions) passe par relay_service (host/relay.cpp, " +
      "repris de l’original 1.0.23) : huit réponses à la fois au plus, chacune sur son thread. OpenAI : " +
      "/chat/completions à l’adresse donnée (/v1 ajouté si seuls ses modèles y répondent) ; Ollama : " +
      "https://ollama.com/v1 ; Claude : /v1/messages, système à part, raisonnement adaptatif, effort, 64 000 tokens " +
      "de réponse sans borne de la requête, web_search à cinq recherches au plus. La clé ne va que dans les en-têtes " +
      "(x-api-key ou Authorization), et vit dans relay.secret (0600) ; settings.json n’a que ********, valable pour le " +
      "même fournisseur, la même adresse et le même utilisateur. Réglages et essai depuis n’importe quel appareil du " +
      "Wi-Fi, le téléphone compris (6.0.0 ; Wi-Fi personnel maîtrisé). Pseudonymisation : POST /v1/text/anonymize de " +
      "Pseudalis avec session: true (sa clé de séance, 3.1.0), réponse values ; une mémoire de 2 h (4 096 textes, " +
      "16 384 valeurs) évite de reprotéger les messages renvoyés à chaque tour ; stream_restorer remet les valeurs au " +
      "fil des fragments, mots seuls de quatre octets ou plus compris. Les séquences d’arrêt ne partent pas avec la " +
      "pseudonymisation. Hors ligne : seules les adresses de bouclage, privées et lien-local, vérifiées avant la " +
      "connexion.",
    marmot: "Imagine que tu demandes de l’aide pour un devoir à un prof d’une autre école. Le relais, c’est le " +
      "messager qui porte ta question et te rapporte la réponse. La pseudonymisation, c’est comme si le messager " +
      "remplaçait ton nom et celui de tes potes par des surnoms avant de partir, puis remettait les vrais noms en " +
      "revenant : le prof aide sans savoir de qui on parle.",
    bee: "Coupé, tout reste sur le Mac. Allumé sans pseudonymisation, la discussion part telle quelle chez le " +
      "fournisseur. Pseudalis arrêté ou trop ancien alors que la pseudonymisation est demandée : la question ne part " +
      "pas du tout, plutôt que de partir en clair. Une clé fausse ou un compte sans crédit donnent une erreur, sans " +
      "jamais montrer la clé."
  },
  rag_engine: {
    title: 'Comment chercher dans les documents',
    control: { kind: 'choice', options: [['hybrid', 'Mots et sens'], ['dense', 'Sens seul'], ['lexical', 'Mots seuls']] },
    restart: false,
    effect: 'La façon dont le service choisit les extraits d’un document pour répondre : par les mots, par le sens, ou les deux.',
    principle: 'Chaque document est coupé en passages. « Mots » compare les mots de la question à ceux des passages (BM25) ; « sens » compare des vecteurs qui résument l’idée (il faut un modèle d’embedding) ; « mots et sens » fond les deux.',
    pros: ['« Mots et sens » trouve large : les bons mots comme les bonnes idées.',
      '« Mots seuls » n’a besoin d’aucun modèle d’embedding et vise juste sur du technique exact (noms de fichiers, codes d’erreur, IP).',
      '« Sens seul » retrouve une idée même quand les mots diffèrent.'],
    cons: ['« Sens » et « mots et sens » exigent un modèle d’embedding chargé et des passages vectorisés.',
      '« Mots seuls » manque une reformulation qui ne partage aucun mot avec le document.'],
    normal: "Quand tu poses une question sur tes documents, le service en remonte les passages les plus utiles. Il peut " +
      "les choisir par les mots communs, par le sens (plus malin, mais il faut un modèle qui transforme le texte en " +
      "vecteurs), ou mélanger les deux. Le mélange est le plus sûr ; « mots seuls » marche partout, même sans modèle " +
      "d’embedding, et excelle sur les termes techniques exacts.",
    owl: "La recherche est portée en C++ dans host/rag_engine.{h,cpp} : rag_chunk découpe (fidèle à chunk() de la page), " +
      "rag_terms tokenise (latin : accents retirés, mots vides écartés), BM25 pour les mots, cosinus des vecteurs pour " +
      "le sens, fusion par rang réciproque, pertinence marginale maximale, prolongement par le passage suivant. " +
      "engine_for(rag_engine) rend lexical, dense ou hybrid. Route POST /gulliver/v1/profile/search.",
    marmot: "C’est comme chercher dans un gros classeur. « Mots » cherche les pages qui répètent tes mots. « Sens » " +
      "cherche les pages qui parlent de la même chose, même avec d’autres mots. « Les deux » fait les deux à la fois.",
    bee: "Sur « sens » ou « mots et sens » sans modèle d’embedding chargé, la recherche retombe sur les mots. Sur " +
      "« mots seuls », une question qui reformule sans reprendre aucun mot du document peut ne rien trouver."
  },
  rag_top_k: {
    title: 'Combien d’extraits de documents',
    control: { kind: 'number', min: 1, max: 50 },
    restart: false,
    effect: 'Le nombre d’extraits de documents qu’une recherche remonte au plus pour une question.',
    principle: 'Plus d’extraits donnent plus de contexte au modèle, mais remplissent sa fenêtre et laissent moins de place à la conversation.',
    pros: ['Plus d’extraits : moins de chance de manquer le passage utile.'],
    cons: ['Trop d’extraits noient la question, coûtent des tokens et peuvent dépasser le budget.'],
    normal: "Pour répondre à partir de tes documents, le service joint quelques extraits à ta question. Ce réglage dit " +
      "combien au maximum. Douze est un bon équilibre ; monte-le pour des documents longs où la réponse est éparpillée, " +
      "baisse-le pour laisser plus de place à la conversation.",
    owl: "rag_top_k borne le nombre d’extraits après la pertinence marginale maximale (options.limit de rag_search_options) ; " +
      "un budget de caractères les coupe aussi. Les extraits consécutifs d’un document fondent en un seul.",
    marmot: "C’est le nombre de bouts de tes documents que l’assistant lit avant de répondre. Peu : il va vite mais peut " +
      "rater un détail. Beaucoup : il lit plus, mais il a moins de place pour le reste.",
    bee: "Trop haut, les extraits mangent toute la fenêtre du modèle et il reste peu de place à ta question et à la " +
      "discussion ; certains extraits sont alors coupés par le budget."
  },
  rag_rerank: {
    title: 'Affiner le tri des extraits',
    control: { kind: 'switch' },
    restart: false,
    effect: 'Reclasse les extraits trouvés par un second tri, plus fin, avant de les joindre à la question.',
    principle: 'La recherche remonte les extraits par passages. Le reranker les reclasse sur l’extrait entier : il récompense celui qui couvre le plus de mots de la question, le plus densément et le plus serré.',
    pros: ['Le meilleur extrait remonte souvent en tête.', 'Sans modèle en plus : aucun coût de chargement, aucune attente.'],
    cons: ['Gain modeste : c’est un tri par les mots, pas un modèle qui « comprend » la question.', 'Un tout petit temps de calcul en plus par recherche.'],
    normal: "Après avoir trouvé les extraits, le service peut les reclasser une seconde fois pour mettre devant celui qui " +
      "répond le mieux à l’ensemble des mots de ta question. C’est léger et sans modèle supplémentaire ; laisse-le activé si " +
      "les meilleurs extraits ne sont pas toujours en tête, désactivé sinon.",
    owl: "rerank_excerpts (host/rag_engine.cpp) reclasse les extraits assemblés : score = 2·couverture + 1·densité + " +
      "0,5·proximité, sur rag_terms(question). Tri stable (garde l’ordre de fusion à égalité). Réglage rag_rerank " +
      "(options.rerank de rag_search_options), appliqué par les trois moteurs après assemble(). Ce n’est pas un cross-encoder " +
      "neuronal (remis au Mac mini).",
    marmot: "Quand l’assistant a trouvé des bouts de tes documents, ce réglage les range une deuxième fois pour mettre en " +
      "premier celui qui colle le mieux à TOUTE ta question, pas juste à un mot.",
    bee: "Sans lui, le meilleur extrait peut arriver deuxième ou troisième. Avec lui, il remonte ; mais ne lui demande pas de " +
      "deviner le sens — il compte les mots, finement, rien de plus."
  }
};

// ---------------------------------------------------------------------------------------------------
// Le rendu.

/*
    serviceTab(body, env) remplit body.  env : h (le constructeur d'éléments de la page), toast,
    status() (l'état du service), refreshStatus().
*/
export function serviceTab(body, env) {
  const { h, toast } = env;
  const st = env.status() || {};
  const cpu = (st.host && st.host.cpu) || {};
  const machine = { logical: cpu.threads || 8, cores: cpu.cores || 8 };
  const pending = {};
  let current = null;

  const intro = h('div', { class: 'svc-intro' },
    h('p', null, 'Les réglages du service Gulliver. Chaque ligne dit ce que le réglage change ; le ',
      h('b', null, '?'), ' ouvre sa fiche, avec trois explications : normale, ', h('b', null, 'le hibou'),
      ' (très technique) et ', h('b', null, 'la marmotte'), ' (pour un ado). ', h('b', null, "L'abeille"),
      ' dit ce qui arrive si on pousse trop ou qu’on s’en passe.'));
  const list = h('div', { class: 'svc-list' }, h('p', { class: 'help' }, 'Lecture des réglages…'));
  const bar = h('div', { class: 'svc-apply', hidden: true });
  body.append(intro, list, bar);

  const valueOf = (key) => (key in pending ? pending[key] : current[key]);

  function renderBar() {
    const keys = Object.keys(pending);
    bar.hidden = keys.length === 0;
    if (!keys.length) return;
    bar.replaceChildren(
      h('span', null, keys.length === 1 ? '1 réglage à appliquer' : keys.length + ' réglages à appliquer',
        h('small', null, ' · le service redémarre')),
      h('button', { class: 'button', type: 'button', onclick: () => { for (const k of keys) delete pending[k]; render(); } },
        'Annuler'),
      h('button', { class: 'button primary', type: 'button', onclick: apply }, 'Appliquer et redémarrer'));
  }

  async function apply() {
    const changes = Object.assign({}, pending);
    try {
      const r = await api.putJSON('/gulliver/v1/settings', changes);
      for (const k of Object.keys(changes)) delete pending[k];
      if (r.restart) await waitRestart(changes);
      await load();
    } catch (e) {
      toast(e.message, true);
    }
  }

  // Attend que le service revienne ; un nouveau port envoie la page à sa nouvelle adresse.
  async function waitRestart(changes) {
    const onLan = location.protocol === 'http:';
    const moved = onLan ? changes.lan_port : changes.port;
    if (moved && Number(moved) !== Number(location.port)) {
      toast('Le service redémarre sur le port ' + moved + '…');
      setTimeout(() => { location.href = location.protocol + '//' + location.hostname + ':' + moved + '/'; }, 4000);
      return;
    }
    if (onLan && changes.lan === false) {
      toast("L'accès réseau se ferme : cette page ne répondra plus.", true);
      return;
    }
    bar.hidden = false;
    bar.replaceChildren(h('span', null, 'Redémarrage du service…'));
    await new Promise((r) => setTimeout(r, 1500));
    for (let i = 0; i < 120; ++i) {
      try {
        const s = await api.status();
        if (s && s.engine && s.engine.running) break;
      } catch (e) {
        // Pas encore revenu.
      }
      await new Promise((r) => setTimeout(r, 1000));
    }
    toast('Service redémarré.');
    if (env.refreshStatus) env.refreshStatus();
  }

  async function setLive(key, value) {
    try {
      await api.putJSON('/gulliver/v1/settings', { [key]: value });
      current[key] = value;
      toast('Appliqué.');
    } catch (e) {
      toast(e.message, true);
    }
    render();
  }

  function change(key, value) {
    const item = items[key];
    if (!item.restart) return setLive(key, value);
    if (value === current[key]) delete pending[key];
    else pending[key] = value;
    render();
  }

  function control(key) {
    const item = items[key], c = item.control, v = valueOf(key);
    if (c.kind === 'switch') {
      const on = key === 'accelerator' ? v !== 'none' : v === c.on;
      return h('button', {
        class: 'svc-switch' + (on ? ' on' : ''), type: 'button', role: 'switch', 'aria-checked': String(on),
        'aria-label': item.title, onclick: () => change(key, on ? c.off : c.on)
      }, h('span'));
    }
    if (c.kind === 'range') {
      const values = c.values(machine);
      let index = values.indexOf(Number(v));
      if (index < 0) index = values.reduce((b, x, i) => Math.abs(x - v) < Math.abs(values[b] - v) ? i : b, 0);
      const out = h('output', null, c.label(values[index], machine));
      const input = h('input', { type: 'range', min: 0, max: values.length - 1, step: 1, value: index, 'aria-label': item.title });
      input.addEventListener('input', () => { out.textContent = c.label(values[Number(input.value)], machine); });
      input.addEventListener('change', () => change(key, values[Number(input.value)]));
      return h('div', { class: 'svc-range' }, input, out);
    }
    if (c.kind === 'choice') {
      return h('div', { class: 'svc-choice', role: 'radiogroup', 'aria-label': item.title },
        c.options.map(([value, label]) => h('button', {
          type: 'button', role: 'radio', 'aria-checked': String(v === value), class: v === value ? 'on' : '',
          onclick: () => change(key, value)
        }, label)));
    }
    if (c.kind === 'relay') {
      const r = current.relay || {};
      return h('span', { class: 'svc-badge' + (r.enabled ? ' changed' : '') }, r.enabled ? 'allumé' : 'coupé');
    }
    if (c.kind === 'number' || c.kind === 'text') {
      const input = h('input', {
        type: c.kind === 'number' ? 'number' : 'text', value: v == null ? '' : v, min: c.min, max: c.max,
        placeholder: c.placeholder, 'aria-label': item.title, spellcheck: 'false', autocomplete: 'off'
      });
      input.addEventListener('change', () => change(key, c.kind === 'number' ? Number(input.value) : input.value.trim()));
      return h('div', { class: 'svc-input' }, input);
    }
    return null;
  }

  function sheet(item, key) {
    const pros = h('div', { class: 'svc-pc pros' }, h('h5', null, 'Avantages'), h('ul', null, item.pros.map((p) => h('li', null, p))));
    const cons = h('div', { class: 'svc-pc cons' }, h('h5', null, 'Inconvénients'), h('ul', null, item.cons.map((p) => h('li', null, p))));
    const voice = (who, title, words) => h('section', { class: 'svc-voice ' + who },
      h('div', { class: 'svc-portrait', html: portraits[who] }),
      h('div', null, h('h5', null, title), h('p', null, words)));
    return h('div', { class: 'svc-detail', hidden: true },
      h('figure', { class: 'svc-schema', html: schemas[key] }),
      h('p', { class: 'svc-principle' }, h('b', null, 'Principe : '), item.principle),
      h('div', { class: 'svc-pcs' }, pros, cons),
      h('section', { class: 'svc-normal' }, h('h5', null, 'Explication'), h('p', null, item.normal)),
      voice('owl', 'Le hibou, version très technique', item.owl),
      voice('marmot', 'La marmotte, pour un ado', item.marmot),
      voice('bee', 'L’abeille : ce qui arrive si on pousse trop, ou qu’on s’en passe', item.bee),
      h('button', { type: 'button', class: 'svc-collapse', onclick: (e) => toggle(e.target.closest('.svc-card'), false) },
        'Réduire ⌃'));
  }

  function toggle(card, open) {
    const detail = card.querySelector('.svc-detail'), help = card.querySelector('.svc-help');
    const show = open == null ? detail.hidden : open;
    detail.hidden = !show;
    help.setAttribute('aria-expanded', String(show));
    card.classList.toggle('open', show);
  }

  function card(key, item) {
    const help = h('button', { type: 'button', class: 'svc-help', 'aria-expanded': 'false',
      'aria-label': 'Explication de « ' + item.title + ' »', title: item.principle,
      onclick: (e) => toggle(e.target.closest('.svc-card')) }, '?');
    const badge = item.restart === undefined ? null : h('span', { class: 'svc-badge' + (item.restart ? ' restart' : '') },
      item.restart ? 'redémarrage' : 'en direct');
    const changed = key in pending ? h('span', { class: 'svc-badge changed' }, 'modifié') : null;
    const el = h('div', { class: 'svc-card', 'data-key': key },
      h('div', { class: 'svc-head' },
        h('div', { class: 'svc-title' }, h('span', null, item.title), help, badge, changed),
        control(key)),
      h('p', { class: 'svc-effect' }, item.effect),
      advice(key),
      key === 'lan' ? lanAddresses() : null,
      key === 'reserve_gb' ? reserveBox() : null,
      key === 'relay' ? relayBox() : null,
      h('div', { class: 'svc-bubble', role: 'tooltip' }, item.principle),
      sheet(item, key));
    return el;
  }

  // La valeur conseillée d'un réglage, quand la valeur choisie en diffère.
  function advice(key) {
    if (!(key in recommended) || same(valueOf(key), recommended[key], key)) return null;
    return h('p', { class: 'svc-advice' }, 'Conseillé : ' + describe(key, recommended[key]));
  }

  function same(a, b, key) {
    if (key === 'accelerator') return (a === 'none') === (b === 'none');
    return a === b || (a == null && b === '') || Number(a) === Number(b) && typeof b === 'number';
  }

  function describe(key, v) {
    const c = items[key].control;
    if (c.kind === 'switch') return (key === 'accelerator' ? v !== 'none' : v === c.on) ? 'allumé' : 'coupé';
    if (c.kind === 'range') return c.label(v, machine);
    if (c.kind === 'choice') return (c.options.find((o) => o[0] === v) || [v, v])[1];
    return v === '' ? 'vide' : String(v);
  }

  // Envoie des réglages d'un coup (réglages conseillés, sauvegarde), puis attend le service s'il redémarre.
  async function sendAll(changes, what) {
    if (!Object.keys(changes).length) return toast('Rien à changer : ' + what + ' sont déjà en place.');
    try {
      const r = await api.putJSON('/gulliver/v1/settings', changes);
      for (const k of Object.keys(changes)) delete pending[k];
      if (r.restart) await waitRestart(changes);
      else toast('Appliqué.');
      await load();
    } catch (e) {
      toast(e.message, true);
    }
  }

  function differences() {
    const out = {};
    for (const [k, v] of Object.entries(recommended)) if (!same(current[k], v, k)) out[k] = v;
    return out;
  }

  // Les réglages conseillés et les sauvegardes, en tête de l'onglet.
  function tools() {
    const diff = differences();
    const n = Object.keys(diff).length;
    const reset = h('button', { class: 'button', type: 'button', disabled: n === 0, onclick: async () => {
      const list = Object.entries(diff).map(([k, v]) => items[k].title + ' : ' + describe(k, v)).join('\n');
      if (!window.confirm('Revenir aux réglages conseillés ?\n\n' + list)) return;
      await sendAll(diff, 'les réglages conseillés');
    } }, 'Revenir aux réglages conseillés');
    const label = h('input', { type: 'text', placeholder: 'Libellé, par exemple « avant essai GPU »', maxlength: 120,
      'aria-label': 'Libellé de la sauvegarde' });
    const save = h('button', { class: 'button', type: 'button', onclick: async () => {
      const name = label.value.trim();
      if (!name) return toast('Donne un libellé à la sauvegarde.', true);
      try {
        await api.postJSON('/gulliver/v1/settings/profiles', { label: name });
        toast('Réglages sauvés sous « ' + name + ' ».');
        label.value = '';
        loadProfiles();
      } catch (e) {
        toast(e.message, true);
      }
    } }, 'Sauver les réglages actuels');
    return h('div', { class: 'svc-card svc-tools' },
      h('div', { class: 'svc-head' }, h('div', { class: 'svc-title' }, h('span', null, 'Réglages conseillés')), reset),
      h('p', { class: 'svc-effect' }, n === 0
        ? 'Tous les réglages du moteur et d’Internet sont aux valeurs conseillées pour ce Mac (mesures du portage sur le M2).'
        : n + (n === 1 ? ' réglage diffère' : ' réglages diffèrent') + ' des valeurs conseillées pour ce Mac ; chaque carte concernée le signale.'),
      h('div', { class: 'svc-head', style: { marginTop: '12px' } }, h('div', { class: 'svc-title' }, h('span', null, 'Sauvegardes'))),
      h('p', { class: 'svc-effect' }, 'Garde les réglages actuels sous un libellé, pour y revenir plus tard.'),
      h('div', { class: 'svc-input', style: { marginTop: '8px', flexWrap: 'wrap' } }, label, save),
      profilesBox);
  }

  const profilesBox = h('div', { class: 'svc-profiles' });

  async function loadProfiles() {
    let list = [];
    try {
      list = (await api.getJSON('/gulliver/v1/settings/profiles')).profiles || [];
    } catch (e) {
      profilesBox.replaceChildren(h('p', { class: 'help' }, e.message));
      return;
    }
    list.sort((a, b) => b.saved - a.saved);
    const date = (sec) => new Date(sec * 1000).toLocaleString('fr-FR', { dateStyle: 'medium', timeStyle: 'short' });
    profilesBox.replaceChildren(...(list.length ? list.map((p) => h('div', { class: 'svc-profile' },
      h('span', { class: 'name' }, p.label), h('span', { class: 'meta' }, date(p.saved)),
      h('button', { class: 'button', type: 'button', onclick: async () => {
        const changes = {};
        for (const [k, v] of Object.entries(p.settings || {})) {
          if (k === 'proxy' && String(v).includes('********')) continue;
          if (current[k] !== v) changes[k] = v;
        }
        if (!window.confirm('Restaurer « ' + p.label + ' » ?' + (Object.keys(changes).length ? '' : ' (rien ne change)'))) return;
        await sendAll(changes, 'ces réglages');
      } }, 'Restaurer'),
      h('button', { class: 'button', type: 'button', onclick: async () => {
        if (!window.confirm('Supprimer la sauvegarde « ' + p.label + ' » ?')) return;
        await fetch('/gulliver/v1/settings/profiles?label=' + encodeURIComponent(p.label), { method: 'DELETE' });
        loadProfiles();
      } }, 'Supprimer'))) : [h('p', { class: 'help' }, 'Aucune sauvegarde pour l’instant.')]));
  }

  /*
      Le relais vers un moteur extérieur (fork, repris de l'original 1.0.23) : son état, ses réglages et leur essai.
      Le secret ne revient jamais du service (********) : un champ vide garde celui qui est enregistré.
  */
  const relayReasons = {
    'the relay is turned off': 'le relais est coupé',
    'the relay has no valid address': 'il manque une adresse valable (http:// ou https://)',
    'the relay has no model': 'il manque le modèle',
    'the model of the relay is not one of its provider here': 'ce modèle n’est pas l’un de ceux de ce fournisseur',
    'the relay has no user': 'il manque l’utilisateur',
    'the relay has no password': 'il manque le mot de passe',
    'the relay has no key': 'il manque la clé',
    'offline mode: Claude needs the Internet': 'hors ligne : Claude a besoin d’Internet',
    'offline mode: Ollama needs the Internet': 'hors ligne : Ollama a besoin d’Internet',
    'offline mode: the address of the engine is neither on this computer nor on its local network':
      'hors ligne : l’adresse du moteur n’est ni sur ce Mac ni sur le réseau de la maison',
    'Pseudalis is not installed': 'Pseudalis n’est pas trouvé sur ce Mac',
    'the local authority of Pseudalis is missing': 'l’autorité locale de Pseudalis manque',
    'the data folder of Pseudalis is not protected': 'le dossier de Pseudalis pourrait être modifié par un autre compte'
  };
  const relayCodes = {
    relay_address: 'adresse refusée : un hôte et un port seulement, sans utilisateur, requête ni fragment',
    relay_key_missing: 'il manque la clé', relay_password_missing: 'il manque le mot de passe',
    relay_offline: 'hors ligne : ce fournisseur a besoin d’Internet',
    relay_offline_remote: 'hors ligne : l’adresse n’est pas sur le réseau de la maison',
    relay_offline_unresolved: 'hors ligne : le nom du moteur n’a pas d’adresse sur ce réseau',
    relay_unreachable: 'le moteur ne répond pas à cette adresse', relay_proxy: 'le proxy ne répond pas',
    relay_certificate: 'le certificat du moteur n’est pas reconnu', relay_no_answer: 'le moteur a cessé de répondre',
    relay_http_key: 'le moteur refuse la clé ou l’utilisateur', relay_http_credit: 'le compte n’a plus de crédit',
    relay_http_address: 'le moteur ne connaît pas cette adresse', relay_http_size: 'requête trop grande',
    relay_http_limit: 'le moteur limite les requêtes ou le crédit', relay_http_failing: 'le moteur est surchargé ou en panne',
    relay_http_refused: 'le moteur refuse la requête', relay_not_interface: 'cette adresse ne répond pas comme une interface OpenAI',
    relay_secret: 'la clé enregistrée ne vaut que pour l’ancien fournisseur, adresse et utilisateur : retape-la'
  };
  const relayWhy = (why) => relayReasons[why] || why;
  // Une fenêtre de contexte : « 1 M tokens », « 256 K tokens ».
  const relayTokens = (n) => n >= 1000000 ? (n / 1000000).toLocaleString('fr-FR') + ' M tokens'
    : Math.round(n / 1024) + ' K tokens';

  function relayBox() {
    const box = h('div', { class: 'svc-relay' }, h('p', { class: 'help' }, 'Lecture du relais…'));
    (async () => {
      let info;
      try {
        info = await api.getJSON('/gulliver/v1/relay');
      } catch (e) {
        box.replaceChildren(h('p', { class: 'help' }, e.message));
        return;
      }
      const saved = current.relay || {};
      const draft = Object.assign({}, saved);
      delete draft.secret;
      let typedSecret = '';
      let found = [];
      const providers = info.providers || [];
      const result = h('p', { class: 'svc-where' });

      const field = (label, input, note) => h('label', { class: 'svc-field' }, h('span', null, label), input,
        note ? h('small', null, note) : null);
      const check = (key, label) => {
        const input = h('input', { type: 'checkbox', checked: !!draft[key] });
        input.addEventListener('change', () => { draft[key] = input.checked; paint(); });
        return h('label', { class: 'svc-check' }, input, h('span', null, label));
      };
      const textInput = (key, placeholder, type) => {
        const input = h('input', { type: type || 'text', value: draft[key] == null ? '' : draft[key], placeholder,
          spellcheck: 'false', autocomplete: 'off' });
        input.addEventListener('change', () => {
          draft[key] = type === 'number' ? Number(input.value) : input.value.trim();
        });
        return input;
      };

      // Ce que le service enverrait : le secret seulement s'il vient d'être tapé.
      function body() {
        const b = {};
        for (const k of ['enabled', 'provider', 'url', 'model', 'auth', 'user', 'context', 'max_output', 'images',
          'reasoning', 'pseudonymize', 'keep_substitutes']) if (k in draft) b[k] = draft[k];
        if (typedSecret) b.secret = typedSecret;
        return b;
      }

      function stateLine() {
        const st = info.relay || {};
        if (!saved.enabled) return h('p', { class: 'svc-where' }, 'Coupé : toutes les discussions restent sur le Mac.');
        if (!st.ready) return h('p', { class: 'svc-where' }, 'Pas prêt : ' + relayWhy(st.reason) + '.');
        return h('p', { class: 'svc-where' }, 'Prêt : ', h('b', null, (st.name || st.model) + ' (' + st.provider_name + ')'),
          st.pseudonymize ? ', pseudonymisé par Pseudalis' : ', sans pseudonymisation : la discussion part telle quelle',
          st.image_mode === 'described' ? ' ; images décrites sur le Mac' : st.image_mode === 'sent'
            ? ' ; images envoyées' : ' ; pas d’images', '.');
      }

      function pseudalisLine() {
        const p = info.pseudalis || {};
        if (p.found) return h('small', null, 'Pseudalis répond sur ce Mac (port ' + p.port + ').');
        return h('small', null, 'Pseudalis : ' + relayWhy(p.reason || '') + '. Sans lui, aucune question ne part.');
      }

      function paint() {
        const provider = providers.find((p) => p.id === draft.provider) || providers[0] || { id: 'openai', models: [] };
        draft.provider = provider.id;
        const openai = provider.id === 'openai';
        const auth = openai ? (draft.auth || 'key') : 'key';
        const enabled = check('enabled', 'Allumer le relais');
        const choose = h('select', null, providers.map((p) => h('option', { value: p.id, selected: p.id === provider.id },
          p.id === 'openai' ? 'Interface OpenAI (autre moteur, ordinateur de la maison)' : p.name)));
        choose.addEventListener('change', () => {
          draft.provider = choose.value;
          const next = providers.find((p) => p.id === draft.provider);
          if (next && next.models && next.models.length) draft.model = next.models[0].id;
          paint();
        });
        let model;
        if (!openai) {
          model = h('select', null, (provider.models || []).map((m) => h('option', { value: m.id, selected: m.id === draft.model },
            m.name + ' · ' + relayTokens(m.context) + (m.images ? ' · images' : ''))));
          if (!draft.model && provider.models && provider.models.length) draft.model = provider.models[0].id;
          model.addEventListener('change', () => { draft.model = model.value; });
        } else {
          model = textInput('model', 'le nom du modèle chez ce moteur');
          model.setAttribute('list', 'svc-relay-models');
        }
        const models = h('datalist', { id: 'svc-relay-models' }, found.map((m) => h('option', { value: m })));
        const authChoice = h('select', null, [['key', 'Une clé'], ['basic', 'Utilisateur et mot de passe'], ['none', 'Aucune']]
          .map(([v, l]) => h('option', { value: v, selected: v === auth }, l)));
        authChoice.addEventListener('change', () => { draft.auth = authChoice.value; paint(); });
        const secret = h('input', { type: 'password', autocomplete: 'off', value: typedSecret,
          placeholder: saved.secret ? 'enregistrée (********) : vide pour la garder' : 'colle-la ici' });
        secret.addEventListener('input', () => { typedSecret = secret.value; });
        const test = h('button', { class: 'button', type: 'button', onclick: async () => {
          result.textContent = 'Essai…';
          try {
            const r = await api.postJSON('/gulliver/v1/relay/test', body());
            if (r.ok) {
              found = r.models || [];
              if (r.url && openai) draft.url = r.url;
              result.textContent = 'Le moteur répond : ' + found.length + (found.length > 1 ? ' modèles' : ' modèle') +
                (r.url && openai ? ', à l’adresse ' + r.url : '') + '.';
              paint();
            } else {
              result.textContent = 'Échec : ' + (relayCodes[r.code] || r.error || r.code) +
                (r.args && r.args.s ? ' (HTTP ' + r.args.s + ')' : '') + (r.detail ? ' : ' + r.detail : '') + '.';
            }
          } catch (e) {
            result.textContent = 'Échec : ' + (relayCodes[e.code] || e.message);
          }
        } }, 'Tester');
        const save = h('button', { class: 'button primary', type: 'button', onclick: async () => {
          try {
            await api.putJSON('/gulliver/v1/settings', { relay: body() });
            current = await api.getJSON('/gulliver/v1/settings');
            toast('Relais enregistré.');
            if (env.refreshStatus) env.refreshStatus();
            render();
            // Le rendu remonte en haut de l'onglet : la vue revient sur la carte du relais.
            const again = list.querySelector('.svc-card[data-key="relay"]');
            if (again) again.scrollIntoView({ block: 'start' });
          } catch (e) {
            toast(relayCodes[e.code] || e.message, true);
          }
        } }, 'Enregistrer');
        /*
            Depuis l'accès réseau (http, un autre appareil), les réglages du relais se lisent sans se changer : le
            service ne les accepte que du Mac lui-même (décision de l'humain du 2026-11-16).
        */
        const elsewhere = location.protocol === 'http:';
        if (elsewhere) {
          box.replaceChildren(stateLine(), h('p', { class: 'svc-where' },
            'Ces réglages se changent sur le Mac lui-même, dans cette page ouverte à son adresse locale ' +
            '(https://localhost) : ni le téléphone ni un autre appareil ne peuvent décider où partent les discussions ' +
            'ni quelle clé paie. Le cadenas des discussions reste utilisable ici.'));
          return;
        }
        box.replaceChildren(
          stateLine(),
          enabled,
          field('Fournisseur', choose),
          openai ? field('Adresse', textInput('url', 'http://192.168.1.20:8000/v1'),
            'La base de son interface ; sans /v1, le service l’ajoute si besoin.') : null,
          openai ? field('Authentification', authChoice) : null,
          openai && auth === 'basic' ? field('Utilisateur', textInput('user', '')) : null,
          auth !== 'none' ? field(auth === 'basic' ? 'Mot de passe' : 'Clé', secret,
            'Gardée par le service seul (relay.secret), jamais renvoyée à la page.') : null,
          field('Modèle', model), models,
          openai ? field('Contexte du modèle (tokens)', textInput('context', '131072', 'number')) : null,
          openai ? field('Réponse au plus (tokens)', textInput('max_output', '16384', 'number')) : null,
          openai ? check('images', 'Le modèle lit les images') : null,
          openai ? check('reasoning', 'Le modèle raisonne (reasoning_effort)') : null,
          check('pseudonymize', 'Pseudonymiser par Pseudalis avant l’envoi'),
          draft.pseudonymize ? pseudalisLine() : null,
          draft.pseudonymize ? check('keep_substitutes', 'Laisser les pseudonymes dans les réponses (contrôle)') : null,
          h('div', { class: 'svc-input', style: { marginTop: '8px', flexWrap: 'wrap' } }, test, save),
          result);
      }
      paint();
    })();
    return box;
  }

  // La place que prend la réserve, et son vidage.
  function reserveBox() {
    const box = h('div', { class: 'svc-where' }, 'Lecture de la réserve…');
    const gb = (b) => (b / 1073741824).toLocaleString('fr-FR', { maximumFractionDigits: 1 }) + ' Go';
    (async () => {
      let r;
      try {
        r = await api.getJSON('/gulliver/v1/reserve');
      } catch (e) {
        box.replaceChildren(e.message);
        return;
      }
      const files = r.files || [];
      const spare = files.filter((f) => !f.installed && !f.projector);
      const empty = h('button', { class: 'button', type: 'button', disabled: files.length === 0, onclick: async () => {
        if (!window.confirm('Vider la réserve ?\n\nLes ' + files.length + ' copies sont retirées ; un modèle supprimé ' +
          'devra se retélécharger.')) return;
        try {
          const res = await fetch('/gulliver/v1/reserve', { method: 'DELETE' });
          const o = await res.json();
          if (!res.ok) throw new Error(o.error || res.statusText);
          toast(o.removed + (o.removed === 1 ? ' copie retirée, ' : ' copies retirées, ') + gb(o.freed) + ' libérés.');
        } catch (e) {
          toast(e.message, true);
        }
        render();
      } }, 'Vider la réserve');
      box.replaceChildren(
        h('span', null, 'Utilisée : ', h('b', null, gb(r.used)), ' sur ' + gb(r.limit) +
          (spare.length ? ' (' + spare.map((f) => f.name || f.sha256.slice(0, 12)).join(', ') + ')' : '') +
          ' · clones des modèles installés : ' + gb(r.shared) + ', sans place prise. '),
        empty);
    })();
    return box;
  }

  function lanAddresses() {
    const addresses = current.lan_addresses || [];
    const port = valueOf('lan_port');
    if (!addresses.length) return h('p', { class: 'svc-where' }, 'Le Mac n’a pas d’adresse sur un réseau pour l’instant.');
    return h('p', { class: 'svc-where' }, current.lan ? 'Sur le téléphone : ' : 'Une fois ouvert, sur le téléphone : ',
      addresses.map((a, i) => [i ? ' ou ' : '', h('code', null, 'http://' + a + ':' + port + '/')]));
  }

  function render() {
    const open = new Set(Array.from(list.querySelectorAll('.svc-card.open')).map((c) => c.dataset.key));
    list.replaceChildren(
      tools(),
      ...groups.map((g) => h('section', { class: 'svc-group' }, h('h4', null, g.title), h('p', { class: 'help' }, g.note),
        g.items.map((k) => card(k, items[k])))));
    for (const k of open) {
      const c = list.querySelector('.svc-card[data-key="' + k + '"]');
      if (c) toggle(c, true);
    }
    renderBar();
  }

  async function load() {
    try {
      current = await api.getJSON('/gulliver/v1/settings');
      render();
      loadProfiles();
    } catch (e) {
      list.replaceChildren(h('p', { class: 'help' }, 'Les réglages ne se lisent pas : ' + e.message));
    }
  }
  load();
}
