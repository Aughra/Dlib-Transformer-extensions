// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les fichiers joints à une discussion ou à un projet.  L'extraction du texte des documents
    (PDF, Word, Excel, PowerPoint, OpenDocument, RTF, HTML, texte brut et code) est désormais
    faite par le service C++, comme le dessin des pages des PDF numérisés en images pour un modèle
    de vision (route /gulliver/v1/extract/pages, fork, 2026-11-25) ; leur code dans le navigateur
    a été retiré, pdf.js compris.  Ne restent ici que :

    - l'aide à la recherche documentaire sur les documents lus en entier (terms, passages,
      bm25Scores, searchPassages) et à leur lecture partie par partie (outline, partText), le
      texte gardant la structure de son document dans des lignes qui commencent par "## " (une
      par page, diapositive, feuille ou section) ;
    - les utilitaires des fichiers (fileKind, extension, documentAccept, maxFileBytes).
*/

// La taille maximale d'un document, vérifiée avant la lecture du fichier.
export const maxFileBytes = 100 * 1024 * 1024;

const textExtensions = new Set(('txt md markdown rst csv tsv json jsonl ndjson xml yaml yml toml ini cfg conf log env ' +
  'c h cc cpp cxx hpp hh hxx inl cu cuh py pyw pyi js mjs cjs ts tsx jsx java kt kts scala go rs swift m mm cs fs vb ' +
  'php rb pl pm lua r jl sh bash zsh fish ps1 psm1 bat cmd sql graphql proto cmake mk make gradle properties tex bib ' +
  'css scss less vue svelte dart hs ml clj ex exs erl asm s v sv vhd tf hcl dockerfile gitignore editorconfig nix srt ' +
  'vtt diff patch').split(' '));

// Les formats que lit ce module, pour les sélecteurs de fichiers : ses documents et les fichiers texte.
export function documentAccept() {
  const office = ['pdf', 'docx', 'docm', 'dotx', 'pptx', 'pptm', 'ppsx', 'potx', 'xlsx', 'xlsm', 'xltx', 'odt', 'ods',
    'odp', 'rtf', 'html', 'htm', 'xhtml'];
  return office.concat(Array.from(textExtensions)).map((e) => '.' + e).join(',') + ',text/*';
}

/*
    Ce qu'un fichier apporte à un modèle : "image", "audio" ou "video" selon son type ou son
    extension, "document" sinon, dont le service lit le texte.
*/
export function fileKind(file) {
  const type = (file.type || '').toLowerCase();
  const ext = extension(file.name);
  if (type.startsWith('image/') && type !== 'image/svg+xml') return 'image';
  if (type.startsWith('audio/')) return 'audio';
  if (type.startsWith('video/')) return 'video';
  if (['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'tif', 'tiff', 'heic', 'avif'].includes(ext)) return 'image';
  if (['mp3', 'wav', 'ogg', 'oga', 'flac', 'm4a', 'aac', 'opus', 'wma'].includes(ext)) return 'audio';
  if (['mp4', 'mov', 'avi', 'mkv', 'webm', 'wmv'].includes(ext)) return 'video';
  return 'document';
}

export function extension(name) {
  const base = String(name).toLowerCase();
  if (base === 'dockerfile' || base === 'makefile' || base === 'cmakelists.txt') return 'txt';
  const i = base.lastIndexOf('.');
  return i < 0 ? '' : base.slice(i + 1);
}

/*
    Les parties d'un texte : ses lignes "## " (pages, diapositives, feuilles, sections), chacune
    avec les caractères qu'elle couvre.  Un texte sans de telles lignes forme une seule partie.
*/
export function outline(text) {
  const parts = [];
  const re = /^## (.+)$/gm;
  let m;
  while ((m = re.exec(text)) !== null) parts.push({ title: m[1].trim(), start: m.index });
  if (!parts.length || parts[0].start > 0 && text.slice(0, parts[0].start).trim()) {
    parts.unshift({ title: '', start: 0 });
  }
  for (let i = 0; i < parts.length; ++i) parts[i].end = i + 1 < parts.length ? parts[i + 1].start : text.length;
  return parts;
}

/*
    Le texte d'une partie nommée comme outline() la nomme : "Slide 3", "Page 12", "Sheet: Budget",
    ou le début d'un titre de section ; null quand aucune partie ne correspond.
*/
export function partText(text, name) {
  const wanted = String(name || '').trim().toLowerCase().replace(/\s+/g, ' ');
  if (!wanted) return null;
  const parts = outline(text).filter((p) => p.title);
  const exact = parts.find((p) => p.title.toLowerCase() === wanted
    || p.title.toLowerCase().split(':')[0].replace(' (hidden)', '') === wanted);
  const found = exact || parts.find((p) => p.title.toLowerCase().startsWith(wanted))
    || parts.find((p) => p.title.toLowerCase().includes(wanted));
  return found ? text.slice(found.start, found.end).trim() : null;
}

const stopWords = new Set(('the a an and or of to in on for with is are was were be by as at it this that from not ' +
  'le la les un une des et ou de du en dans pour par sur avec est sont au aux ce cette qui que ' +
  'der die das und oder ein eine zu im mit ist sind von den dem il lo gli e di da per con che ' +
  'el los las y o del para por con es son que').split(' '));

export function terms(s) {
  return String(s).toLowerCase().normalize('NFKD').replace(/[̀-ͯ]/g, '').split(/[^\p{L}\p{N}_]+/u)
    .filter((w) => w.length > 1 && !stopWords.has(w));
}

/*
    Scores BM25 d'une requête sur des passages [{text}] : un tableau de scores, dans leur ordre.
    Les statistiques des termes sont calculées sur les passages donnés.
*/
export function bm25Scores(passages, query) {
  const q = Array.from(new Set(terms(query)));
  const scores = new Float64Array(passages.length);
  if (!q.length || !passages.length) return scores;
  const df = new Map();
  const tfs = [];
  const lengths = [];
  for (const p of passages) {
    const tf = new Map();
    const words = terms(p.text);
    for (const w of words) tf.set(w, (tf.get(w) || 0) + 1);
    for (const w of q) if (tf.has(w)) df.set(w, (df.get(w) || 0) + 1);
    tfs.push(tf);
    lengths.push(words.length);
  }
  const avg = lengths.reduce((s, x) => s + x, 0) / lengths.length || 1;
  const k1 = 1.2, b = 0.75, n = passages.length;
  for (let i = 0; i < passages.length; ++i) {
    let score = 0;
    for (const w of q) {
      const f = tfs[i].get(w) || 0;
      if (!f) continue;
      const d = df.get(w) || 0;
      const idf = Math.log(1 + (n - d + 0.5) / (d + 0.5));
      score += idf * (f * (k1 + 1)) / (f + k1 * (1 - b + b * lengths[i] / avg));
    }
    scores[i] = score;
  }
  return scores;
}

// Découpe un texte en passages qui se chevauchent pour la recherche documentaire.
export function passages(text, size = 1200, overlap = 200) {
  const out = [];
  for (let i = 0; i < text.length; i += size - overlap) {
    out.push({ start: i, text: text.slice(i, i + size) });
    if (i + size >= text.length) break;
  }
  return out;
}

/*
    Les passages d'un ensemble de documents les plus proches d'une requête, par le score BM25
    de leurs termes : [{name, start, text, score}], le meilleur d'abord.  La recherche
    documentaire sans modèle d'embedding.
*/
export function searchPassages(documents, query, limit = 5) {
  const all = [];
  for (const d of documents) for (const p of passages(d.text || '')) all.push({ name: d.name, start: p.start, text: p.text });
  const scores = bm25Scores(all, query);
  all.forEach((p, i) => { p.score = scores[i]; });
  return all.filter((p) => p.score > 0).sort((a, b) => b.score - a.score).slice(0, limit)
    .map(({ name, start, text, score }) => ({ name, start, text, score }));
}
