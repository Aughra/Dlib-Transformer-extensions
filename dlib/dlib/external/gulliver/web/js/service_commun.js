// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Ce que chaque module de groupe (service_moteur, service_documents, …) partage :
    les trois portraits du glossaire (le hibou, la marmotte, l'abeille), les primitives de dessin
    des schémas (box, arrow, text, schema), et les petits helpers de formatage des entiers
    (tokens, méga-octets, listes de contextes et de tailles de clés/valeurs).
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

// ---------------------------------------------------------------------------------------------------
// Formatage et listes partagées.  Les entiers manipulés ici sont des jetons et des méga-octets.

export const contexts = [0, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144];
export const kvSizes = [0, 512, 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384];

export function formatTokens(n) {
  return n >= 1024 ? (n / 1024) + ' K tokens' : n + ' tokens';
}

export function formatMb(n) {
  return n >= 1024 ? (n / 1024).toLocaleString('fr-FR') + ' Go' : n + ' Mo';
}
