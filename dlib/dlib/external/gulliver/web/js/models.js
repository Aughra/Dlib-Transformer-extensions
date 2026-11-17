// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet « Modèles » des paramètres : le modèle qui répond, choisi parmi ceux du catalogue du service, avec
    leurs spécialités, une mention « conseillé », « possible » ou « déconseillé » selon la mémoire du Mac et la vitesse
    attendue, le téléchargement depuis la page et le rafraîchissement de la liste.  Chaque modèle a sa fiche, à la
    manière de l'onglet « Service » : principe, avantages, inconvénients, schéma, puis l'explication normale, celle du
    hibou (très technique), de la marmotte (pour un ado) et de l'abeille (ce qui arrive si on se trompe).

    Le fork n'a plus de sélecteur de fonction : choisir un modèle l'active pour les trois fonctions du service
    (général, code, cybersécurité).  Le modèle de la recherche dans les documents (embedding) reste à part.
*/

import * as api from './api.js';
import { portraits, box, text, schema } from './service.js';

// Les mesures du portage sur MacBook Air M2 (journal du fork) : le modèle de 4B, 2,5 Go de poids, écrit 21,5
// tokens/s sur le GPU et 18,5 sur le processeur.  La vitesse d'écriture suit à peu près la taille des poids, que
// chaque token relit en entier.
const measured = { bytes: 2497281472, gpu: 21.5, cpu: 18.5 };

const specialties = {
  general: ['Général', 'questions, rédaction, analyse, traduction'],
  code: ['Code', 'programmation, revue, débogage, tests'],
  security: ['Cybersécurité', 'analyse de sécurité, audits, durcissement, incidents'],
  image: ['Vision', 'lit les images, les captures et les pages scannées'],
  reasoning: ['Raisonnement', 'réfléchit au brouillon avant de répondre'],
  tools: ['Outils', 'sait utiliser les outils du mode agent']
};

function specialtyKeys(m) {
  const out = (m.roles || []).filter((r) => specialties[r]);
  if ((m.inputs || []).includes('image')) out.push('image');
  if (m.reasoning) out.push('reasoning');
  if (m.tools) out.push('tools');
  return out;
}

// Les tailles comme macOS les affiche pour la mémoire : par paquets de 1 024.
function gb(n) {
  return (n / 1073741824).toLocaleString('fr-FR', { maximumFractionDigits: 1 }) + ' Go';
}

function decimal(n) {
  return n.toLocaleString('fr-FR', { maximumFractionDigits: 1 });
}

function billions(n) {
  return (n / 1e9).toLocaleString('fr-FR', { maximumFractionDigits: 1 }) + ' milliards';
}

function tokens(n) {
  return n >= 1024 ? Math.round(n / 1024).toLocaleString('fr-FR') + ' K tokens' : n + ' tokens';
}

// Un mélange d'experts (MoE).
function isMoe(m) {
  return /-a\d+b\b/i.test(m.id) || /moe/i.test(m.arch || '');
}

/*
    Les poids que prennent les noyaux Metal (cpu_runtime_metal.cpp) : Q4_K et Q6_K, et aussi Q5_K, Q8_0 et Q5_0 pour
    Qwen 3.5, LFM2 et les mélanges d'experts (depuis Gulliver 4.7.0 et 4.21.0 du fork) ; les experts de Gemma 4
    (26B-A4B) restent sur le processeur.
*/
function onGpu(m, moe) {
  if (moe && m.arch === 'gemma4') return false;
  const wide = moe || /^(qwen35|lfm2)/.test(m.arch || '');
  return (wide ? /^(Q4_K|Q5_K|Q6_K|Q5_0|Q8_0)/i : /^Q[46]_K/i).test(m.quantization || '');
}

/*
    Ce que le Mac peut attendre d'un modèle : le GPU (onGpu(), que le contrôle du chargement confirme), la vitesse
    d'écriture estimée d'après les mesures du M2 (pas pour un mélange d'experts, qui ne relit qu'une part de ses poids),
    et un avis.
*/
export function assess(m, status) {
  const ram = (status && status.host && status.host.memory && status.host.memory.total) || 0;
  const bytes = m.total_size || m.size || 0;
  const moe = isMoe(m);
  const gpu = onGpu(m, moe);
  const speed = moe || !m.size ? null : (gpu ? measured.gpu : measured.cpu) * measured.bytes / m.size;
  let level = 'conseille', reason = 'tient largement dans la mémoire et écrit vite sur ce Mac';
  if (!ram) {
    level = 'inconnu';
    reason = 'mémoire du Mac inconnue';
  } else if (bytes > ram * 0.5) {
    level = 'deconseille';
    reason = 'occupe plus de la moitié de la mémoire du Mac (' + gb(ram) + ')';
  } else if (moe && !gpu) {
    level = 'possible';
    reason = 'mélange d’experts dont les experts tournent sur le processeur seulement, sans le GPU';
  } else if (bytes > ram * 0.25 || (speed && speed < 10)) {
    level = 'possible';
    reason = speed && speed < 10 ? 'plus lent : environ ' + Math.round(speed) + ' tokens/s attendus'
      : 'prend une bonne part de la mémoire du Mac';
  }
  return { ram, bytes, moe, gpu, speed, level, reason };
}

// Les états d'un téléchargement en cours (models.cpp).
const running = ['queued', 'downloading', 'verifying'];

const modelErrors = {
  reserve_unavailable: 'La réserve est absente, incomplète ou illisible. Vérifiez aussi l’espace libre, ou importez les fichiers.',
  download_busy: 'Un téléchargement réseau de ce modèle se termine. Réessayez ensuite.',
  download_hash: 'Les fichiers ne correspondent pas aux empreintes du catalogue.',
  download_file: 'Un fichier de travail ne peut pas être créé ou lu. Consultez le journal du service.',
  download_install: 'Le modèle vérifié ne peut pas être installé. Consultez le journal du service.',
  download_failed: 'Le téléchargement a échoué. Consultez le journal du service.',
  disk_space: 'L’espace libre est insuffisant pour préparer ce modèle.',
  catalog_entry: 'L’entrée du catalogue est invalide pour ce modèle.',
  not_in_catalog: 'Ce modèle ne figure plus dans le catalogue.',
  builtin_installed: 'Ce modèle est déjà fourni avec Gulliver.',
  not_installed: 'Ce modèle n’est plus installé.',
  builtin_model: 'Ce modèle fourni avec Gulliver ne peut pas être supprimé.',
  delete_failed: 'La suppression a échoué. Vérifiez la réserve et les fichiers du modèle.',
  offline: 'Cette opération demande une connexion au réseau.',
  stopping: 'Le service est en cours d’arrêt.',
  library_insecure: 'D’autres comptes que les administrateurs peuvent modifier la bibliothèque du Mac : le service ne la lit pas.',
  library_unavailable: 'La bibliothèque du Mac ne peut pas être créée. Vérifiez son dossier dans l’onglet Service.',
  library_not_found: 'Ce fichier n’est plus dans la bibliothèque du Mac. Rafraîchissez la liste.'
};

function modelError(result) {
  return modelErrors[result.code] || 'L’opération a échoué' + (result.code ? ' (code ' + result.code + ')' : '') +
    '.';
}

const levelLabels = { conseille: 'Conseillé', possible: 'Possible', deconseille: 'Déconseillé', inconnu: 'À voir' };

// Le schéma de la mémoire : macOS et les autres programmes, le modèle, la place qui reste aux conversations.
function memorySchema(m, a) {
  if (!a.ram) return '';
  const w = 552, x0 = 24;
  const sys = Math.min(w * 0.3, w * 6e9 / a.ram);
  const mod = Math.min(w - sys - 20, w * a.bytes / a.ram);
  return schema('La mémoire du Mac (' + gb(a.ram) + ') avec ce modèle',
    `<rect x="${x0}" y="70" width="${w}" height="46" rx="10" class="s-box"/>` +
    `<rect x="${x0}" y="70" width="${sys}" height="46" rx="10" class="s-soft"/>` + text(x0 + sys / 2, 98, 'macOS') +
    `<rect x="${x0 + sys}" y="70" width="${mod}" height="46" class="${a.bytes > a.ram * 0.5 ? 's-gold' : 's-blue'}"/>` +
    text(x0 + sys + mod / 2, 98, mod > 70 ? 'modèle' : '') +
    text(x0 + sys + mod + (w - sys - mod) / 2, 98, 'conversations') +
    text(300, 150, 'le modèle : ' + gb(a.bytes) + (a.speed ? ' · environ ' + Math.round(a.speed) + ' tokens/s attendus' : ''), 's-f') +
    text(300, 172, a.gpu ? 'poids lus par le GPU, sans copie' : 'poids lus par le processeur', 's-f'), 190);
}

// La taille d'un modèle, en image pour la marmotte.
function sizeImage(params) {
  const b = params / 1e9;
  if (b < 2.5) return 'un collégien vif : il répond vite, mais connaît moins de choses et se trompe plus souvent';
  if (b < 6) return 'un bon lycéen : rapide, à l’aise dans la plupart des matières';
  if (b < 12) return 'un étudiant : il en sait plus et raisonne mieux, mais il lui faut plus de temps et un plus grand bureau';
  return 'un professeur : très savant, mais il prend toute la salle et parle lentement';
}

/*
    La fiche d'un modèle, faite de ce que le catalogue en dit et de ce que la mesure du Mac permet d'attendre.
    Les chiffres viennent du catalogue (taille, paramètres, contexte, quantification) et des mesures du M2.
*/
export function modelSheet(m, status) {
  const a = assess(m, status);
  const keys = specialtyKeys(m);
  const names = keys.map((k) => specialties[k][0]);
  const vision = keys.includes('image'), reasoning = !!m.reasoning;
  const pros = [], cons = [];
  if (keys.includes('code') && keys.includes('security')) pros.push('Polyvalent : général, code et cybersécurité.');
  else if (keys.includes('code')) pros.push('Spécialisé en programmation.');
  else if (keys.includes('security')) pros.push('Spécialisé en cybersécurité.');
  if (vision) pros.push('Lit les images : photos, captures d’écran, schémas, pages scannées.');
  if (reasoning) pros.push('Réfléchit avant de répondre : meilleur sur les problèmes à plusieurs étapes.');
  else pros.push('Répond directement, sans phase de réflexion : plus rapide.');
  if (a.level === 'conseille') pros.push('Adapté à ce Mac : ' + a.reason + '.');
  if (m.context >= 131072) pros.push('Grand contexte : ' + tokens(m.context) + ', de longs documents.');
  if (!vision) cons.push('Ne lit pas les images.');
  if (reasoning) cons.push('Sa réflexion allonge le temps avant la réponse.');
  if (a.level !== 'conseille') cons.push(levelLabels[a.level] + ' sur ce Mac : ' + a.reason + '.');
  if (a.moe && !a.gpu) cons.push('Mélange d’experts sans accélération par le GPU : il tourne sur le processeur.');
  if (a.moe && a.gpu) cons.push('Mélange d’experts : tout le fichier reste en mémoire, même si chaque mot n’en lit qu’une part.');
  if (/preview/i.test(m.id)) cons.push('Version de prévisualisation : moins éprouvée.');
  if (!cons.length) cons.push('Aucun défaut marquant pour ce Mac.');

  const normal = (m.description || m.name) + ' Il a ' + billions(m.parameters || 0) + ' de paramètres, occupe ' +
    gb(a.bytes) + ' de mémoire' + (a.speed ? ' et devrait écrire environ ' + Math.round(a.speed) +
    ' tokens par seconde sur ce Mac' : '') + '. ' + (names.length ? 'Ses spécialités : ' + names.join(', ') + '.' : '');
  const owl = 'Architecture ' + (m.arch || '?') + ', ' + (m.parameters || 0).toLocaleString('fr-FR') + ' paramètres, ' +
    'quantification ' + (m.quantization || '?') + ' (poids sur 4 à 6 bits par groupe), fichier de ' + gb(m.size || 0) +
    (m.projector ? ' plus un projecteur d’images de ' + gb(m.projector.size || 0) + ' (' + (m.projector.quantization || '') + ')' : '') +
    ', contexte entraîné de ' + (m.context || 0).toLocaleString('fr-FR') + ' tokens. ' +
    (a.moe && !a.gpu ? 'Mélange d’experts que les noyaux Metal du fork ne couvrent pas (Gemma 4 26B-A4B, ou poids ' +
      'd’un autre format) : les passes restent sur le processeur. '
      : a.moe ? 'Mélange d’experts : les étapes passent par le GPU, les experts choisis lus par matvec ; les prompts ' +
        'de Qwen 3.6 aussi, par produits groupés, ceux des autres sur le processeur ; contrôle du chargement à 25 % ' +
        'près en clés 8 bits. '
        : a.gpu ? 'Poids pris par le GPU sans copie, si le contrôle du chargement (logits à 1 % près du processeur, ' +
          '5 % en clés 8 bits) le confirme. ' : 'Format hors des noyaux Metal : processeur. ') +
    (a.speed ? 'Vitesse estimée : ' + decimal(measured[a.gpu ? 'gpu' : 'cpu']) + ' tokens/s mesurés pour ' + gb(measured.bytes) +
      ' de poids sur le M2, à l’échelle de la taille des poids (chaque token les relit tous) : environ ' + decimal(a.speed) +
      ' tokens/s. ' : '') +
    'Licence ' + (m.license || '?') + ', éditeur ' + (m.publisher || '?') + '.';
  const marmot = 'Ce modèle, c’est ' + sizeImage(m.parameters || 0) + '.' +
    (keys.includes('code') && !keys.includes('general') ? ' Sa matière forte, c’est l’informatique.' : '') +
    (keys.includes('security') && !keys.includes('general') ? ' Sa matière forte, c’est la sécurité.' : '') +
    (vision ? ' Il sait aussi regarder des photos et lire ce qui est écrit dessus.' : '') +
    (reasoning ? ' Avant de répondre, il fait ses calculs au brouillon : plus juste, mais plus long.' : '');
  const bee = (a.level === 'deconseille' ? 'Sur ce Mac, il risque de saturer la mémoire : tout ralentit, y compris les ' +
    'autres applications, et le contexte des conversations est réduit. ' : a.level === 'possible' ? 'Il tourne, mais ' +
    a.reason + '. ' : '') + (vision ? '' : 'Une image jointe à une question sera refusée. ') +
    (reasoning ? 'Pour une question simple, sa réflexion fait attendre pour rien ; un modèle « Instruct » répond plus vite. ' : '') +
    'Le télécharger prend ' + gb(m.total_size || m.size || 0) + ' sur le disque.';
  return { title: m.name, effect: names.join(' · '), principle: m.description || m.name, pros, cons, normal, owl,
    marmot, bee, schema: memorySchema(m, a), assessment: a };
}

// La fiche des spécialités, la même pour tous les modèles.
const specialtiesSheet = {
  title: 'Les spécialités des modèles',
  principle: 'Un modèle de langue est entraîné sur certains types de textes ; ses spécialités disent où il est le plus à l’aise.',
  pros: ['Un modèle spécialisé (code, cybersécurité) répond mieux dans sa matière qu’un modèle général de même taille.',
    'Un modèle « Vision » lit les images ; un modèle « Raisonnement » réfléchit avant de répondre.'],
  cons: ['Un spécialiste est moins bon hors de sa matière.',
    'Lire les images et raisonner demandent plus de mémoire ou de temps.'],
  normal: 'Général : questions, rédaction, analyse, traduction. Code : écrire, relire et corriger des programmes. ' +
    'Cybersécurité : analyser des journaux, auditer, durcir un système, réagir à un incident. Vision : comprendre une ' +
    'image ou une page scannée. Raisonnement : le modèle écrit d’abord sa réflexion, puis sa réponse. Outils : il peut ' +
    'chercher sur le web ou lire une page dans le mode agent.',
  owl: 'Les spécialités viennent du catalogue du service (champs roles, inputs, reasoning, tools). Les modèles « Thinking » ' +
    'écrivent un bloc de raisonnement (<think>) que la page replie ; les modèles « VL » ont un projecteur d’images (mmproj) ' +
    'chargé avec eux. Choisir un modèle ici l’active pour les fonctions général, code et cybersécurité du service ; ' +
    'le modèle de recherche dans les documents (embedding) reste à part.',
  marmot: 'C’est comme choisir un camarade pour t’aider : l’un est fort partout, un autre est un as de l’informatique, un ' +
    'autre sait lire les cartes et les photos. Le plus fort n’est pas toujours le meilleur choix : s’il est trop gros pour ' +
    'ta chambre, il ne sert à rien.',
  bee: 'Un modèle trop gros pour la mémoire du Mac ralentit tout, et un modèle sans vision refuse les images. Regarde la ' +
    'mention « conseillé » avant de télécharger plusieurs gigaoctets.',
  schema: schema('Ce qu’un modèle sait faire',
    box(20, 60, 130, 50, 'Général', 's-blue') + box(165, 60, 130, 50, 'Code', 's-blue') +
    box(310, 60, 130, 50, 'Cybersécurité', 's-blue') + box(455, 60, 125, 50, 'Vision', 's-gold') +
    box(90, 130, 200, 50, 'Raisonnement', 's-gold') + box(310, 130, 200, 50, 'Outils (agent)', 's-gold'), 200)
};

// La fiche dépliable, faite comme celles de l'onglet « Service ».
function sheetElement(h, sheet) {
  const voice = (who, title, words) => h('section', { class: 'svc-voice ' + who },
    h('div', { class: 'svc-portrait', html: portraits[who] }),
    h('div', null, h('h5', null, title), h('p', null, words)));
  return h('div', { class: 'svc-detail' },
    sheet.schema ? h('figure', { class: 'svc-schema', html: sheet.schema }) : null,
    h('p', { class: 'svc-principle' }, h('b', null, 'Principe : '), sheet.principle),
    h('div', { class: 'svc-pcs' },
      h('div', { class: 'svc-pc pros' }, h('h5', null, 'Avantages'), h('ul', null, sheet.pros.map((p) => h('li', null, p)))),
      h('div', { class: 'svc-pc cons' }, h('h5', null, 'Inconvénients'), h('ul', null, sheet.cons.map((p) => h('li', null, p))))),
    h('section', { class: 'svc-normal' }, h('h5', null, 'Explication'), h('p', null, sheet.normal)),
    voice('owl', 'Le hibou, version très technique', sheet.owl),
    voice('marmot', 'La marmotte, pour un ado', sheet.marmot),
    voice('bee', 'L’abeille : ce qui arrive si on se trompe de modèle', sheet.bee));
}

function helpButton(h, card, label) {
  return h('button', { type: 'button', class: 'svc-help', 'aria-expanded': 'false', 'aria-label': 'Explication de « ' + label + ' »',
    onclick: () => {
      const d = card.querySelector(':scope > .svc-detail');
      const open = !d || d.hidden;
      if (d) d.hidden = !open;
      card.classList.toggle('open', open);
    } }, '?');
}

// Une carte avec son « ? » et sa fiche repliée.
function sheetCard(h, sheet, head, extra) {
  const card = h('div', { class: 'svc-card' });
  const detail = sheetElement(h, sheet);
  detail.hidden = true;
  card.append(h('div', { class: 'svc-head' }, h('div', { class: 'svc-title' }, h('span', null, sheet.title),
    helpButton(h, card, sheet.title), head || null), extra || null));
  if (sheet.effect) card.append(h('p', { class: 'svc-effect' }, sheet.effect));
  card.append(detail);
  return card;
}

// La fenêtre « ? » du modèle choisi, ouverte depuis le bas de la page.
export function modelSheetDialog(m, env) {
  const sheet = modelSheet(m, env.status());
  return env.dialog((box, close) => {
    box.append(h2(env.h, m.name), env.h('p', { class: 'svc-effect' }, sheet.effect + ' · ' +
      levelLabels[sheet.assessment.level] + ' sur ce Mac'), sheetElement(env.h, sheet),
    env.h('div', { class: 'actions' }, env.h('button', { class: 'button', type: 'button',
      onclick: () => { close(true); env.openModels(); } }, 'Changer de modèle'),
    env.h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, 'Fermer')));
  });
}

function h2(h, title) {
  return h('h2', null, title);
}

/*
    modelsTab(body, env) remplit body.  env : h, toast, formatSize, status() (l'état du service), refreshStatus().
*/
export function modelsTab(body, env) {
  const { h, toast } = env;
  const list = h('div', { class: 'svc-list' }, h('p', { class: 'help' }, 'Lecture du catalogue…'));
  body.append(h('div', { class: 'svc-intro' }, h('p', null, 'Le modèle qui répond à vos questions. Chaque modèle a ses ' +
    'spécialités et une mention selon ce Mac ; le ', h('b', null, '?'), ' ouvre sa fiche, avec le hibou, la marmotte et ' +
    'l’abeille.')), list);
  let data = null, timer = 0;

  async function load() {
    try {
      data = await api.models('fr');
    } catch (e) {
      list.replaceChildren(h('p', { class: 'help' }, 'Le catalogue ne se lit pas : ' + e.message));
      return;
    }
    render();
    const busy = (data.downloads || []).some((d) => running.includes(d.state));
    clearTimeout(timer);
    if (busy && body.isConnected) timer = setTimeout(load, 1500);
  }

  async function choose(m) {
    try {
      for (const role of ['general', 'code', 'security']) {
        await api.postJSON('/gulliver/v1/models/' + encodeURIComponent(m.id) + '/activate', { role });
      }
      toast(m.name + ' répond désormais ; il se charge.');
      if (env.refreshStatus) env.refreshStatus();
      load();
    } catch (e) {
      toast(e.message, true);
    }
  }

  async function act(method, url, ok) {
    try {
      const r = await fetch(url, method === 'POST' ? { method, headers: { 'Content-Type': 'application/json' }, body: '{}' }
        : { method });
      let j;
      try { j = await r.json(); } catch (e) {
        if (e instanceof SyntaxError) throw new Error('La réponse du service est illisible.');
        throw e;
      }
      if (!j || typeof j !== 'object') throw new Error('La réponse du service est illisible.');
      if (!r.ok || j.ok === false) throw new Error(modelError(j));
      if (j.ok !== true) throw new Error('La réponse du service est illisible.');
      if (ok) toast(ok);
    } catch (e) {
      toast(e instanceof TypeError ? 'Le service ne répond pas.' : e.message, true);
    }
    load();
  }

  function row(m, active) {
    const sheet = modelSheet(m, env.status());
    const a = sheet.assessment;
    const dl = (data.downloads || []).find((d) => d.id === m.id);
    const badge = h('span', { class: 'mdl-badge ' + a.level, title: a.reason }, levelLabels[a.level]);
    let actions;
    if (active) {
      actions = h('span', { class: 'svc-badge' }, 'utilisé');
    } else if (dl && running.includes(dl.state)) {
      const pct = dl.total ? Math.round(100 * dl.received / dl.total) : 0;
      actions = h('div', { class: 'mdl-progress' }, h('div', { class: 'bar' }, h('div', { style: { width: pct + '%' } })),
        h('span', null, dl.state === 'verifying' ? 'vérification' : dl.state === 'queued' ? 'en attente' : pct + ' %'), h('button', { class: 'button', type: 'button',
          onclick: () => act('DELETE', '/gulliver/v1/models/' + encodeURIComponent(m.id) + '/download', 'Téléchargement annulé.') },
        'Annuler'));
    } else if (m.installed) {
      actions = h('div', { class: 'svc-input' },
        h('button', { class: 'button primary', type: 'button', onclick: () => choose(m) }, 'Choisir'),
        h('button', { class: 'button', type: 'button', onclick: () => {
          if (window.confirm('Supprimer ' + m.name + ' du Mac ?')) {
            act('DELETE', '/gulliver/v1/models/' + encodeURIComponent(m.id), m.name + ' supprimé.');
          }
        } }, 'Supprimer'));
    } else {
      // Un modèle dont la réserve garde tous les fichiers revient du Mac, sans réseau ; hors ligne, les autres
      // ne peuvent pas venir.
      const local = !!m.in_reserve;
      actions = h('button', { class: 'button', type: 'button', disabled: !!data.offline && !local,
        title: local ? 'Copie locale depuis la réserve du Mac, vérifiée, sans réseau'
          : data.offline ? 'Ce modèle n’est pas dans la réserve du Mac' : null,
        onclick: () => act('POST', '/gulliver/v1/models/' + encodeURIComponent(m.id) + '/download',
          (local ? 'Restauration de ' + m.name + ' lancée.' : 'Téléchargement de ' + m.name + ' lancé.')) },
      local ? 'Restaurer depuis le Mac' : data.offline ? 'Pas dans la réserve'
        : 'Télécharger (' + gb(m.total_size || m.size || 0) + ')');
    }
    const card = sheetCard(h, sheet, badge, actions);
    card.classList.add('mdl-card');
    card.insertBefore(h('p', { class: 'svc-where' }, (m.publisher || '') + ' · ' + billions(m.parameters || 0) +
      ' de paramètres · ' + gb(m.total_size || m.size || 0) + (a.speed ? ' · environ ' + Math.round(a.speed) + ' tokens/s' : '') +
      (m.installed ? ' · installé' : m.in_reserve ? ' · dans la réserve du Mac' : '')), card.querySelector('.svc-detail'));
    if (dl && dl.state === 'error')
      card.insertBefore(h('p', { class: 'error-box', role: 'alert' }, modelError(dl)), card.querySelector('.svc-detail'));
    return card;
  }

  function render() {
    const open = new Set(Array.from(list.querySelectorAll('.mdl-card.open')).map((c) => c.dataset.id));
    const activeId = data.active && data.active.general;
    const models = (data.models || []).slice();
    const order = { conseille: 0, possible: 1, inconnu: 2, deconseille: 3 };
    models.sort((x, y) => (y.installed - x.installed) || (order[assess(x, env.status()).level] -
      order[assess(y, env.status()).level]) || ((x.total_size || x.size) - (y.total_size || y.size)));
    const current = models.find((m) => m.id === activeId);
    offlineNow = !!data.offline;
    // Deux sources : le Mac (la liste, la réserve et les réponses gardées, sans réseau), ou Internet (le catalogue
    // depuis GitHub, la dernière recherche depuis Hugging Face).
    const refresh = h('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap' } },
      h('button', { class: 'button', type: 'button',
        title: 'Relit sans réseau la liste, la réserve, la bibliothèque du Mac et la dernière recherche gardée',
        onclick: async () => {
          await load();
          await loadLibrary();
          if (lastQuery !== null) search(true);
          toast('Liste relue depuis le Mac.');
        } }, 'Rafraîchir depuis le Mac'),
      h('button', { class: 'button', type: 'button', disabled: offlineNow,
        title: offlineNow ? 'Le service est hors ligne' : 'Le catalogue depuis GitHub, la dernière recherche depuis Hugging Face',
        onclick: async () => {
          await act('POST', '/gulliver/v1/models/refresh', 'Catalogue demandé à GitHub.');
          if (lastQuery !== null) search();
        } }, 'Rafraîchir depuis Hugging Face'));
    const updated = data.catalog && data.catalog.updated ? new Date(data.catalog.updated).toLocaleString('fr-FR',
      { dateStyle: 'medium', timeStyle: 'short' }) : 'jamais';
    const cards = models.map((m) => {
      const c = row(m, m.id === activeId);
      c.dataset.id = m.id;
      if (open.has(m.id)) { c.classList.add('open'); c.querySelector('.svc-detail').hidden = false; }
      return c;
    });
    list.replaceChildren(
      h('div', { class: 'svc-card svc-tools' }, h('div', { class: 'svc-head' },
        h('div', { class: 'svc-title' }, h('span', null, 'Modèle utilisé : ' + (current ? current.name : 'aucun'))), refresh),
        h('p', { class: 'svc-effect' }, 'Catalogue mis à jour : ' + updated + ' · ' + (data.models || []).length +
          ' modèles · place libre : ' + gb(data.free || 0) + (data.offline ? ' · hors ligne : restauration locale seulement' : ''))),
      sheetCard(h, specialtiesSheet),
      h('section', { class: 'svc-group' }, h('h4', null, 'Les modèles'),
        h('p', { class: 'help' }, 'Installés d’abord, puis du plus conseillé au moins conseillé pour ce Mac. Ceux ajoutés ' +
          'depuis Hugging Face y figurent aussi.'), cards),
      library,
      hub);
  }

  /*
      La bibliothèque du Mac (fork pour macOS) : les fichiers GGUF d'un dossier du Mac, que le service lit sans réseau et
      sans y écrire ; « Charger dans Gulliver » en fait une copie (un clone, sans place prise), avec son projecteur.
  */
  const billionsOf = (n) => n ? billions(n) + ' de paramètres' : 'taille inconnue';
  const library = h('section', { class: 'svc-group' }, h('h4', null, 'Sur ce Mac'),
    h('p', { class: 'help' }, 'Lecture de la bibliothèque du Mac…'));
  const loading = new Set();
  async function loadLibrary() {
    let r;
    try {
      r = await api.getJSON('/gulliver/v1/library');
    } catch (e) {
      library.replaceChildren(h('h4', null, 'Sur ce Mac'), h('p', { class: 'error-box', role: 'alert' },
        modelErrors[e.code] || 'La bibliothèque du Mac ne se lit pas : ' + e.message));
      return;
    }
    const intro = h('p', { class: 'help' }, 'Les fichiers GGUF du dossier ', h('code', null, r.folder || ''), ', lus sans ' +
      'réseau. « Charger dans Gulliver » en fait une copie qui ne prend pas de place sur le disque, avec le projecteur ' +
      'd’images qui va avec le modèle. Le dossier se change dans l’onglet Service ; on y dépose les fichiers dans le ' +
      'Finder.');
    const models = r.models || [], lone = r.projectors || [];
    const card = (f, model) => {
      const busy = loading.has(f.file);
      const parts = [model ? (f.name && f.name !== f.file ? f.name + ' · ' : '') + (f.arch || '?') + ' · ' +
        (f.quantization || '?') + ' · ' + billionsOf(f.parameters) + ' · ' + gb(f.size || 0)
        : 'projecteur d’images · ' + (f.arch || '?') + ' · ' + gb(f.size || 0)];
      if (model && f.projector) parts.push('avec son projecteur ' + f.projector.file + ' (' + gb(f.projector.size) + ')');
      if (model && f.installed) parts.push('déjà dans Gulliver (' + f.installed + ')');
      if (model && !f.supported) parts.push('le moteur ne lit pas cette architecture');
      const action = model && !f.supported ? null : h('button', { class: 'button', type: 'button', disabled: busy,
        onclick: async () => {
          loading.add(f.file);
          loadLibrary();
          try {
            const done = await api.postJSON('/gulliver/v1/library/load', { file: f.file });
            const ids = (done.imported || []).concat(done.already || []);
            toast(done.imported && done.imported.length ? f.file + ' est chargé dans Gulliver.'
              : done.already && done.already.length ? f.file + ' était déjà dans Gulliver (' + ids.join(', ') + ').'
                : done.pending && done.pending.length ? 'Le projecteur attend son modèle.'
                  : (done.errors && done.errors[0]) || 'Rien n’a été chargé.', !ids.length && !(done.pending || []).length);
          } catch (e) {
            toast(modelErrors[e.code] || e.message, true);
          }
          loading.delete(f.file);
          load();
          loadLibrary();
        } }, busy ? 'Chargement…' : f.installed ? 'Charger de nouveau' : 'Charger dans Gulliver');
      return h('div', { class: 'svc-card hub-card' + (model && !f.supported ? ' incompatible' : '') },
        h('div', { class: 'svc-head' }, h('div', { class: 'svc-title' }, h('span', null, f.file)), action),
        h('p', { class: 'svc-where' }, parts.join(' · ')));
    };
    library.replaceChildren(h('h4', null, 'Sur ce Mac'), intro,
      ...(models.length || lone.length ? [] : [h('p', { class: 'help' }, 'Aucun fichier GGUF dans ce dossier.')]),
      ...models.map((f) => card(f, true)), ...lone.map((f) => card(f, false)),
      ...((r.skipped || []).length ? [h('p', { class: 'help' }, 'Écartés : ' + r.skipped.map((s) => s.file + ' (' +
        (s.reason === 'link' ? 'lien' : s.reason === 'several names' ? 'plusieurs noms' : 'illisible') + ')').join(', '))] : []));
  }
  loadLibrary();

  /*
      Hugging Face : tous les modèles GGUF publiés, au-delà du catalogue.  Les compatibles d'abord ; ceux que le moteur
      ne lit pas (architecture, format de poids, fichier découpé), en gris à la fin, avec la raison.
  */
  let offlineNow = false;
  let lastQuery = null;
  const hubResults = h('div', { class: 'hub-results' });
  // Les recherches gardées sur le Mac, à relancer d'un clic, même hors ligne.
  const hubRecent = h('p', { class: 'help' });
  async function loadRecent() {
    let r;
    try {
      r = await api.getJSON('/gulliver/v1/hub/recent');
    } catch (e) {
      return;
    }
    const list = (r.searches || []).slice(0, 8);
    hubRecent.replaceChildren(...(list.length ? ['Recherches gardées sur le Mac : ', ...list.map((s, i) => [i ? ' · ' : '',
      h('a', { href: '#', onclick: (e) => {
        e.preventDefault();
        hubInput.value = s.query;
        search();
      } }, s.query || '(les plus téléchargés)')]).flat()] : []));
  }
  const keptOn = (sec) => new Date(sec * 1000).toLocaleString('fr-FR', { dateStyle: 'medium', timeStyle: 'short' });
  const hubInput = h('input', { type: 'search', placeholder: 'Rechercher un modèle (qwen3, mistral, granite…)',
    'aria-label': 'Rechercher sur Hugging Face' });
  const hub = h('section', { class: 'svc-group' }, h('h4', null, 'Tous les modèles de Hugging Face'),
    h('p', { class: 'help' }, 'Les modèles GGUF publiés sur Hugging Face, les plus téléchargés d’abord. Ceux que le moteur ' +
      'ne sait pas lire sont en gris, à la fin.'),
    h('div', { class: 'svc-input hub-search' }, hubInput,
      h('button', { class: 'button primary', type: 'button', onclick: () => search() }, 'Rechercher')), hubRecent, hubResults);
  loadRecent();
  hubInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') search(); });

  const byCompat = (a, b) => (b.compatible === true) - (a.compatible === true);

  // local : la réponse gardée sur le Mac, sans réseau.  Hors ligne, le service la donne de lui-même.
  async function search(local) {
    lastQuery = hubInput.value.trim();
    hubResults.replaceChildren(h('p', { class: 'help' }, local || offlineNow ? 'Lecture de la recherche gardée sur le Mac…'
      : 'Recherche sur Hugging Face…'));
    let r;
    try {
      r = await api.getJSON('/gulliver/v1/hub/search?q=' + encodeURIComponent(lastQuery) + (local ? '&local=1' : ''));
    } catch (e) {
      hubResults.replaceChildren(h('p', { class: 'help' }, e.code === 'hub_not_cached' || /never kept/.test(e.message)
        ? 'Cette recherche n’a jamais été faite sur ce Mac : il faut Internet pour la lancer.' : e.message));
      return;
    }
    loadRecent();
    const kept = r.cached_at ? h('p', { class: 'help' }, 'Résultats gardés sur le Mac le ' + keptOn(r.cached_at) +
      (local || offlineNow ? '.' : ' : Hugging Face ne répond pas.')) : null;
    const list = (r.models || []).slice().sort(byCompat);
    if (!list.length) return hubResults.replaceChildren(...[kept, h('p', { class: 'help' }, 'Aucun modèle GGUF trouvé.')].filter(Boolean));
    hubResults.replaceChildren(...(kept ? [kept] : []), ...list.map((m) => {
      const files = h('div', { class: 'hub-files' });
      const card = h('div', { class: 'svc-card hub-card' + (m.compatible ? '' : ' incompatible') },
        h('div', { class: 'svc-head' }, h('div', { class: 'svc-title' }, h('span', null, m.repo),
          m.compatible ? h('span', { class: 'mdl-badge conseille' }, 'compatible')
            : h('span', { class: 'mdl-badge inconnu' }, 'incompatible')),
        m.compatible ? h('button', { class: 'button', type: 'button', onclick: () => showFiles(m.repo, files) }, 'Voir les fichiers')
          : null),
        h('p', { class: 'svc-where' }, (m.architecture || 'architecture inconnue') + ' · ' + billionsOf(m.parameters) +
          ' · ' + (m.downloads || 0).toLocaleString('fr-FR') + ' téléchargements' + (m.reason ? ' · ' + m.reason : '')),
        files);
      return card;
    }));
  }

  async function showFiles(repo, box) {
    box.replaceChildren(h('p', { class: 'help' }, 'Lecture des fichiers…'));
    let r;
    try {
      r = await api.getJSON('/gulliver/v1/hub/files?repo=' + encodeURIComponent(repo));
    } catch (e) {
      box.replaceChildren(h('p', { class: 'help' }, e.message));
      return;
    }
    // Le projecteur d'images proposé : le premier en Q8_0, sinon en F16, sinon le premier.
    const proj = r.projectors || [];
    const pick = proj.find((p) => p.quantization === 'Q8_0') || proj.find((p) => p.quantization === 'F16') || proj[0];
    const vision = pick ? h('input', { type: 'checkbox', checked: true }) : null;
    const ram = (env.status() && env.status().host && env.status().host.memory && env.status().host.memory.total) || 0;
    const files = (r.files || []).slice().sort((a, b) => byCompat(a, b) || (b.gpu - a.gpu) || (a.size - b.size));
    box.replaceChildren(...[
      h('p', { class: 'svc-where' }, (r.license ? 'Licence ' + r.license + ' · ' : '') + 'contexte ' +
        (r.context ? tokens(r.context) : 'inconnu') + (r.cached_at ? ' · liste gardée sur le Mac le ' + keptOn(r.cached_at) : '')),
      pick ? h('label', { class: 'inline hub-vision' }, vision, ' Avec la lecture des images (' + pick.file + ', ' +
        gb(pick.size || 0) + ')') : null,
      ...files.map((f) => {
        const heavy = ram && f.size > ram * 0.5;
        return h('div', { class: 'hub-file' + (f.compatible ? '' : ' incompatible') },
          h('span', { class: 'name' }, f.file),
          h('span', { class: 'meta' }, gb(f.size || 0) + (f.gpu ? ' · GPU possible' : f.compatible ? ' · processeur' : '') +
            (heavy ? ' · trop gros pour ce Mac' : '') + (f.reason ? ' · ' + f.reason : '')),
          f.compatible ? h('button', { class: 'button', type: 'button', onclick: async () => {
            try {
              const done = await api.postJSON('/gulliver/v1/hub/add', { repo, file: f.file,
                projector: vision && vision.checked ? pick.file : '' });
              toast(done.name + ' ajouté ; son téléchargement commence.');
              load();
            } catch (e) {
              toast(e.message, true);
            }
          } }, 'Ajouter') : null);
      })].filter(Boolean));
  }
  load();
}
