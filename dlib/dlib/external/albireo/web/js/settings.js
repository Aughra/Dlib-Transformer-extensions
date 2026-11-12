// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les paramètres de la page et leurs onglets (Général, Modèles, Génération, Mémoire, Données, Service, À propos), les
// notes de version et la page des composants et licences (fork pour macOS : sortis d'app.js).

import * as api from './api.js';
import * as images from './images.js';
import * as rag from './rag.js';
import * as store from './store.js';
import * as web from './web.js';
import { applyTheme, broadcast, deleted, go, namePending, refreshViews, roles, saveSetting, syncLists } from './app.js';
import { generations, stopAllGenerations, unread } from './composer.js';
import { forgetStoredDrafts, waitingImages } from './drafts.js';
import { reloadLists } from './lists.js';
import { backgroundContext, modelName, refreshStatus, searchProvider } from './status.js';
import { forgetMemory, memoryParagraph, memorySentences } from './memory.js';
import { cancelBackground, cancelIndex, describePending, indexAttachments, indexProjects } from './indexing.js';
import { memoryLimitWords } from './context.js';
import { confirmDialog, dialog, field } from './dialogs.js';
import { download, fill, formatNumber, formatSize, h, storageFailed, toast } from './dom.js';
import { escapeHtml, renderMarkdown } from './markdown.js';
import { icon } from './icons.js';
import { language, t, tn } from './i18n.js';
import { modelsTab } from './models.js';
import { serviceTab } from './service.js';
import { state, ui } from './state.js';

export function settingsDialog(initialTab) {
  dialog((box, close) => {
    const tabs = ['general', 'modelsTab', 'model', 'memoryTab', 'data', 'serviceTab', 'about'];
    const tabBar = h('div', { class: 'tabs' });
    const body = h('div');
    const show = (tab) => {
      for (const b of tabBar.children) b.classList.toggle('on', b.dataset.tab === tab);
      ui.memoryView = null;
      body.replaceChildren();
      if (tab === 'general') generalTab(body, close);
      else if (tab === 'model') modelTab(body);
      else if (tab === 'memoryTab') memoryTab(body);
      else if (tab === 'data') dataTab(body, close);
      else if (tab === 'serviceTab') serviceTab(body, { h, toast, status: () => state.status, refreshStatus });
      else if (tab === 'modelsTab') modelsTab(body, { h, toast, status: () => state.status, refreshStatus });
      else aboutTab(body, close);
    };
    for (const tab of tabs) tabBar.append(h('button', { type: 'button', 'data-tab': tab, onclick: () => show(tab) }, t(tab)));
    box.append(h('h2', { html: icon('gear') + escapeHtml(t('settings')) }), tabBar, body,
      h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('close'))));
    show(initialTab || 'general');
  }).then(() => { ui.memoryView = null; });
}

function generalTab(body, close) {
  const theme = h('select', null, ['auto', 'light', 'dark'].map((v) =>
    h('option', { value: v, selected: state.settings.theme === v }, t('theme' + v[0].toUpperCase() + v.slice(1)))));
  theme.addEventListener('change', async () => { await saveSetting('theme', theme.value); applyTheme(); });
  const name = h('input', { value: state.settings.userName || '' });
  name.addEventListener('change', async () => {
    await saveSetting('userName', name.value.trim());
    refreshViews();
  });
  const enter = h('input', { type: 'checkbox', checked: state.settings.sendOnEnter });
  enter.addEventListener('change', () => saveSetting('sendOnEnter', enter.checked));
  const sys = h('textarea', { rows: 4 });
  sys.value = state.settings.systemPrompt || '';
  sys.addEventListener('change', () => saveSetting('systemPrompt', sys.value));
  body.append(field(t('theme'), theme), field(t('yourName'), name, t('yourNameHelp')),
    h('div', { class: 'field' }, h('label', { class: 'inline' }, enter, ' ', t('sendOnEnter'))),
    field(t('systemPrompt'), sys, t('systemPromptHelp')));
}

function modelTab(body) {
  const slider = (key, min, max, stepValue, def) => {
    const value = state.settings[key];
    const input = h('input', { type: 'range', min, max, step: stepValue, value: value == null ? def : value });
    const out = h('span', { style: { minWidth: '64px', textAlign: 'right' } }, value == null ? t('maxTokensAuto') : String(value));
    input.addEventListener('input', () => { out.textContent = input.value; });
    input.addEventListener('change', () => saveSetting(key, parseFloat(input.value)));
    const reset = h('button', {
      class: 'button', type: 'button', onclick: async () => { await saveSetting(key, null); out.textContent = t('maxTokensAuto'); input.value = def; }
    }, t('maxTokensAuto'));
    return h('div', { class: 'inline' }, input, out, reset);
  };
  /*
      top-k : un curseur et sa valeur, qui peut aussi être tapée, de 0 (pas de limite) à 1024, les bornes du service ;
      vide, le service prend sa propre valeur.
  */
  const topK = () => {
    const value = state.settings.topK;
    const range = h('input', { type: 'range', min: 0, max: 1024, step: 1, value: value == null ? 40 : value,
      'aria-label': t('topK') });
    const number = h('input', { type: 'number', class: 'top-k', min: 0, max: 1024, step: 1, 'aria-label': t('topK'),
      value: value == null ? '' : String(value), placeholder: t('maxTokensAuto') });
    range.addEventListener('input', () => { number.value = range.value; });
    range.addEventListener('change', () => saveSetting('topK', parseInt(range.value, 10)));
    number.addEventListener('change', () => {
      const typed = parseInt(number.value, 10);
      const v = number.value.trim() === '' || Number.isNaN(typed) ? null : Math.max(0, Math.min(1024, typed));
      number.value = v == null ? '' : String(v);
      if (v != null) range.value = v;
      saveSetting('topK', v);
    });
    const reset = h('button', {
      class: 'button', type: 'button', onclick: async () => {
        await saveSetting('topK', null);
        number.value = '';
        range.value = 40;
      }
    }, t('maxTokensAuto'));
    return h('div', { class: 'inline' }, range, number, reset);
  };
  const max = h('input', { type: 'number', min: 0, max: 131072, step: 256, value: state.settings.maxTokens || 0 });
  max.addEventListener('change', () => saveSetting('maxTokens', Math.max(0, parseInt(max.value, 10) || 0)));
  body.append(field(t('temperature'), slider('temperature', 0, 2, 0.05, 0.7), t('temperatureHelp')),
    field(t('topP'), slider('topP', 0.05, 1, 0.05, 0.95), t('topPHelp')), field(t('topK'), topK(), t('topKHelp')),
    field(t('maxTokens'), max, '0 = ' + t('maxTokensAuto')));
}

// La mémoire des discussions : son usage, son contenu, que l'utilisateur peut modifier, et sa remise à zéro.
function memoryTab(body) {
  const words = memoryLimitWords(backgroundContext());
  const enabled = h('input', { type: 'checkbox', checked: !!state.settings.memoryEnabled });
  const sensitive = h('input', { type: 'checkbox', checked: !!state.settings.memorySensitive });
  const text = h('textarea', { rows: 8, placeholder: t('memoryEmpty') });
  text.value = state.settings.memory || '';
  const size = h('p', { class: 'help', style: { margin: 0, color: 'var(--text-faint)', fontSize: '12.5px' } });
  const showSize = () => {
    const n = text.value.split(/\s+/).filter(Boolean).length;
    size.textContent = tn('memorySize', n, { max: words })
      + (state.settings.memoryUpdated ? ' · ' + t('memoryUpdated', { date: new Date(state.settings.memoryUpdated).toLocaleString(language()) }) : '');
  };
  showSize();
  enabled.addEventListener('change', async () => {
    await saveSetting('memoryEnabled', enabled.checked);
    sensitive.disabled = text.disabled = !enabled.checked;
  });
  sensitive.addEventListener('change', () => saveSetting('memorySensitive', sensitive.checked));
  text.addEventListener('input', showSize);
  text.addEventListener('change', async () => {
    /*
        Ce que l'utilisateur écrit reste, sensible ou non, dans la limite de deux fois la taille de la mémoire, sous
        forme de phrases l'une après l'autre : une ligne devient une phrase.  Les phrases que l'utilisateur ajoute ou
        change restent quand les faits que le modèle apprend remplissent la mémoire ; celles que le modèle a apprises et
        que l'utilisateur a laissées telles quelles peuvent partir.
    */
    const before = new Set(memorySentences(state.settings.memory, true));
    const pinnedBefore = new Set(memorySentences(state.settings.memoryPinned, true));
    await saveSetting('memory', memoryParagraph(memorySentences(text.value, true), words * 2));
    // Les phrases que l'utilisateur a ajoutées ou changées, et celles qu'il a écrites auparavant, telles que la mémoire
    // enregistrée les découpe.
    const saved = memorySentences(state.settings.memory, true);
    await saveSetting('memoryPinned', saved.filter((x) => pinnedBefore.has(x) || !before.has(x)).join(' '));
    await saveSetting('memoryUpdated', Date.now());
    text.value = state.settings.memory;
    showSize();
  });
  sensitive.disabled = text.disabled = !enabled.checked;
  // La mémoire telle qu'une mise à jour ou un autre onglet la change, sauf si l'utilisateur est en train d'y écrire.
  ui.memoryView = () => {
    if (!text.isConnected) return;
    enabled.checked = !!state.settings.memoryEnabled;
    sensitive.checked = !!state.settings.memorySensitive;
    sensitive.disabled = text.disabled = !enabled.checked;
    if (document.activeElement !== text) text.value = state.settings.memory || '';
    showSize();
  };
  body.append(h('p', null, t('memoryIntro')),
    h('div', { class: 'field' }, h('label', { class: 'inline' }, enabled, ' ', t('memoryEnabled'))),
    h('div', { class: 'field' }, h('label', { class: 'inline' }, sensitive, ' ', t('memorySensitive')),
      h('p', { class: 'help', style: { margin: 0, color: 'var(--text-faint)', fontSize: '12.5px' } }, t('memorySensitiveHelp'))),
    field(t('memoryText'), text), size,
    h('div', { class: 'buttons', style: { marginTop: '10px' } }, h('button', {
      class: 'button danger', type: 'button', html: icon('trash') + '<span>' + escapeHtml(t('memoryReset')) + '</span>',
      onclick: async () => {
        if (!await confirmDialog(t('memoryReset'), t('memoryResetText'))) return;
        await saveSetting('memory', '');
        await saveSetting('memoryPinned', '');
        await saveSetting('memoryUpdated', 0);
        // Les discussions oublient la mémoire avec laquelle elles ont commencé, et ce qu'elles ont
        // déjà dit reste hors des mises à jour suivantes : les discussions enregistrées changent sur
        // place, relues depuis la base avec les messages que d'autres onglets ont enregistrés, et les
        // copies des onglets suivent.
        await store.updateConversations((x) => {
          const mark = Array.isArray(x.messages) ? x.messages.length : 0;
          if (x.memory === undefined && x.memoryMark === mark) return false;
          delete x.memory;
          x.memoryMark = mark;
          return true;
        }).catch(() => {});
        forgetMemory();
        broadcast({ type: 'memoryReset' });
        text.value = '';
        showSize();
      }
    })));
}

function dataTab(body, close) {
  const input = h('input', { type: 'file', accept: '.json,application/json', class: 'hidden' });
  input.addEventListener('change', async () => {
    try {
      const data = JSON.parse(await input.files[0].text());
      const r = await store.importAll(data);
      // Une discussion supprimée auparavant revient avec l'importation, et est enregistrée de nouveau.
      for (const id of r.ids) deleted.delete(id);
      broadcast({ type: 'lists' });
      await syncLists();
      toast(t('imported', {
        discussions: tn('discussionsCount', r.conversations), projects: tn('projectsCount', r.projects)
      }));
      // Les documents des projets et les documents cherchés des discussions sont indexés, puisqu'une exportation ne
      // porte pas de passages ; les images des projets sont décrites, et les discussions laissées en attente sont
      // nommées.
      indexProjects();
      for (const id of r.ids) {
        const c = state.conversations.find((x) => x.id === id);
        if (c) indexAttachments(c).catch(() => {});
      }
      describePending();
      namePending();
    } catch (e) {
      toast(t('importFailed'), true);
    }
    input.value = '';
  });
  const storage = h('div', { class: 'storage-state' });
  // Les discussions, les projets et les images enregistrés, avec la place que prennent les images.
  const stored = h('p', { class: 'storage-usage' });
  const showUsage = (u) => {
    stored.textContent = t('storageUsage', {
      discussions: tn('discussionsCount', state.conversations.length),
      projects: tn('projectsCount', state.projects.length),
      images: tn('imagesCount', u.count), size: formatSize(u.bytes)
    });
  };
  showUsage({ count: 0, bytes: 0 });
  images.usage().then(showUsage).catch(() => {});
  body.append(stored, storage,
    h('div', { class: 'buttons' },
      h('button', {
        class: 'button', type: 'button', html: icon('download') + '<span>' + escapeHtml(t('exportAll')) + '</span>',
        onclick: async () => {
          try {
            download('albireo-' + new Date().toISOString().slice(0, 10) + '.json', await store.exportAll(),
              'application/json');
          } catch (e) {
            // L'exportation ne lit que la base du navigateur : l'erreur est celle du navigateur, telle quelle.
            toast(t('exportFailed', { reason: (e && e.message) || String(e) }), true);
          }
        }
      }),
      h('button', { class: 'button', type: 'button', html: icon('upload') + '<span>' + escapeHtml(t('importAll')) + '</span>', onclick: () => input.click() }),
      input,
      h('button', {
        class: 'button danger', type: 'button', html: icon('trash') + '<span>' + escapeHtml(t('clearAll')) + '</span>',
        onclick: async () => {
          if (!await confirmDialog(t('clearAll'), t('clearAllText'))) return;
          // Les réponses en cours, affichées ou écrites en arrière-plan, et le travail du modèle
          // d'arrière-plan s'arrêtent, les discussions enregistrées ne sont pas réenregistrées, et
          // l'indexation en cours n'écrit aucun passage.  Une nouvelle discussion, pas encore
          // enregistrée, reste avec son brouillon.
          for (const c of state.conversations) deleted.add(c.id);
          if (state.current && state.current.messages.length) deleted.add(state.current.id);
          for (const g of generations.values()) deleted.add(g.c.id);
          stopAllGenerations();
          unread.clear();
          cancelIndex();
          cancelBackground();
          rag.removeAll();
          images.removeAll(waitingImages());
          forgetStoredDrafts();
          for (const id of deleted) web.forgetPages(id);
          await store.clearAll().catch(storageFailed);
          broadcast({ type: 'cleared' });
          await reloadLists().catch(storageFailed);
          close(true);
          go('#/');
        }
      })));
  showStorage(storage);
}

/*
    Demande au navigateur de garder la base de l'application quand le disque vient à manquer de
    place, une fois la base ouverte, quand elle n'est pas encore gardée.  La réponse, que le
    navigateur donne sans demander à l'utilisateur, reste dans les réglages (storagePersisted) et
    s'affiche dans l'onglet Data des réglages.
*/
export async function requestPersistence() {
  const s = navigator.storage;
  if (!s || typeof s.persisted !== 'function' || typeof s.persist !== 'function') return;
  try {
    let kept = await s.persisted();
    if (!kept) kept = await s.persist();
    if (state.settings.storagePersisted !== kept) await saveSetting('storagePersisted', kept);
  } catch (e) {
    // Le navigateur garde sa réponse pour lui ; l'onglet Data montre alors ce qu'il dit.
  }
}

/*
    L'état du stockage de ce navigateur, gardé ou non, avec l'espace utilisé et l'espace disponible,
    et le conseil d'exporter les données régulièrement quand le navigateur peut les supprimer.
*/
async function showStorage(el) {
  const s = navigator.storage;
  let kept = state.settings.storagePersisted;
  let estimate = null;
  try {
    if (s && typeof s.persisted === 'function') kept = await s.persisted();
  } catch (e) {
    // La dernière réponse reste.
  }
  try {
    if (s && typeof s.estimate === 'function') estimate = await s.estimate();
  } catch (e) {
    estimate = null;
  }
  if (typeof kept === 'boolean' && kept !== state.settings.storagePersisted) saveSetting('storagePersisted', kept);
  const parts = [t(kept === true ? 'storagePersistent' : kept === false ? 'storageNotPersistent' : 'storageUnknown')];
  if (estimate && estimate.quota) {
    parts.push(t('storageEstimate', { used: formatSize(estimate.usage || 0), quota: formatSize(estimate.quota) }));
  }
  el.dataset.persistent = String(kept);
  fill(el, h('p', null, h('b', null, t('storageState')), ' ', parts.join(' · ')),
    kept === true ? null : h('p', { class: 'help storage-hint' }, h('span', { html: icon('info') }), t('storageHint')));
}

/*
    Le GPU du Mac : son nom, puis ce que chaque modèle chargé en fait (ses passes sur le GPU avec les tampons qu'il y
    garde, sa vérification, ou le processeur, la raison du moteur dans l'infobulle).  Les poids et les clés et valeurs
    restent dans la mémoire que le GPU partage avec le processeur, et ne sont pas comptés.
*/
function gpuSummary(stats) {
  const g = stats && stats.gpu;
  if (!g) return '-';
  if (!g.name) return h('span', { title: g.reason || '' }, t(g.disabled ? 'gpuDisabled' : 'gpuMissing'));
  const uses = (stats.models || []).filter((m) => m.gpu).map((m) => {
    const u = m.gpu;
    const what = u.state === 'on' ? t('gpuOn', { size: formatSize(u.bytes || 0) })
      : u.state === 'checking' ? t('gpuChecking') : t('gpuProcessor');
    return h('span', { title: u.reason || '', style: { display: 'block' } }, modelName(m.id) + ': ' + what);
  });
  // Des span, pas des div : la règle des étiquettes de .kv grise ses div impairs, imbriqués compris.
  return h('span', null, h('span', { style: { display: 'block' } }, g.name), uses);
}

async function aboutTab(body, close) {
  const s = state.status || {};
  const art = h('div', { class: 'about-art' }, h('img', { src: 'img/about.svg', alt: '' }));
  const kv = h('div', { class: 'kv' });
  const row = (k, v) => kv.append(h('div', null, k), h('div', null, v));
  row(t('version'), [s.product || 'Albireo', s.edition, s.version].filter(Boolean).join(' ') + ' · ' + t('forkShort'));
  row(t('engine'), s.engine ? (s.engine.variant || '-') + (s.engine.device ? ' (' + s.engine.device + ')' : '') : '-');
  if (s.host) {
    row(t('processor'), s.host.cpu.name + ' · ' + s.host.cpu.cores + ' / ' + s.host.cpu.threads);
    row(t('memory'), formatSize(s.host.memory.total));
  }
  row(t('gpu'), gpuSummary(s.engine && s.engine.stats));
  // Les fonctions de chaque modèle, dans l'ordre du sélecteur, la recherche dans les documents en dernier.
  const byModel = new Map();
  for (const r of roles.concat(['embedding'])) {
    const id = (s.active || {})[r];
    if (id) byModel.set(id, (byModel.get(id) || []).concat([t('role_' + r)]));
  }
  const models = Array.from(byModel, ([id, names]) => names.join(', ') + ': ' + modelName(id)).join(' · ');
  row(t('models'), models || '-');
  // Le fournisseur de la recherche web, Wikipédia, tant que le service le propose.
  const provider = searchProvider();
  if (provider) row(t('tool_web_search'), provider);
  if (s.usage) row(t('requests'), formatNumber(s.usage.requests) + ' · ' + t('tokensIn') + ' ' + formatNumber(s.usage.prompt_tokens) +
    ' · ' + t('tokensOut') + ' ' + formatNumber(s.usage.completion_tokens));
  body.append(art, h('p', null, t('aboutText')), kv);
  try {
    const u = await api.usage();
    const byDay = new Map((u.days || []).map((d) => [d.day, d]));
    if (byDay.size) {
      /*
          Les trente derniers jours, y compris les jours sans usage, chacun une barre des tokens générés au-dessus des
          tokens lus, dont l'infobulle donne la date et les chiffres dans la langue de l'interface.  Les jours du
          service sont ceux de l'UTC.
      */
      const dateOf = (day) => new Intl.DateTimeFormat(language(), { dateStyle: 'medium', timeZone: 'UTC' })
        .format(new Date(day + 'T00:00:00Z'));
      const days = [];
      for (let k = 29; k >= 0; --k) {
        const day = new Date(Date.now() - k * 86400000).toISOString().slice(0, 10);
        const d = byDay.get(day) || {};
        days.push({ day, read: d.prompt_tokens || 0, generated: d.generated_tokens || 0 });
      }
      const max = Math.max(...days.map((d) => d.read + d.generated), 1);
      body.append(h('h4', null, t('usage')),
        h('div', { class: 'usage-bars', role: 'img', 'aria-label': t('usageChart') }, days.map((d) => h('div', {
          class: 'usage-day', style: { height: Math.max(2, 80 * (d.read + d.generated) / max) + 'px' },
          title: t('usageDay', {
            date: dateOf(d.day), generated: tn('tokensGenerated', d.generated, { n: formatNumber(d.generated) }),
            read: tn('tokensRead', d.read, { n: formatNumber(d.read) })
          })
        }, h('div', { class: 'generated', style: { flexGrow: String(d.generated) } }),
        h('div', { class: 'read', style: { flexGrow: String(d.read) } })))),
        h('div', { class: 'usage-axis' }, h('span', null, dateOf(days[0].day)), h('span', null, dateOf(days[29].day))),
        h('div', { class: 'legend' }, h('span', { style: { '--c': 'var(--blue)' } }, t('usageGenerated')),
          h('span', { style: { '--c': 'var(--gold)' } }, t('usageRead'))));
    }
  } catch (e) {
    // Les chiffres d'usage sont facultatifs.
  }
  body.append(h('p', { style: { marginTop: '14px' } }, t('createdBy', {
    author: (s.author || 'Aldric Pierrain') + ' (' + (s.email || 'cydraltechnology@gmail.com') + ')'
  }) + ' · ' + (s.copyright || 'Copyright (C) 2026 Cydral Technology')),
  // Le fork pour macOS, et son dépôt.
  h('p', { class: 'fork-line' }, t('forkedBy'), ' · ',
    h('a', { href: 'https://github.com/Aughra/Dlib-Transformer-extensions', target: '_blank', rel: 'noopener' },
      'Aughra/Dlib-Transformer-extensions')),
  h('p', null, h('a', { href: s.project || 'https://github.com/Cydral/Dlib-Transformer-extensions', target: '_blank', rel: 'noopener' }, 'Dlib-Transformer-extensions'),
    ' · ', h('a', { href: '/docs/', target: '_blank' }, t('apiDocs')), ' · ',
    h('a', { href: '#', onclick: (e) => { e.preventDefault(); changelogDialog(); } }, t('changelog')), ' · ',
    h('a', { href: '#/licenses', onclick: () => close(true) }, t('thirdParty'))));
}

// Les notes de version du fork pour macOS (web/changelog.md).
export function changelogDialog() {
  return dialog((box, close) => {
    const body = h('div', { class: 'md changelog' }, h('p', { class: 'help' }, '…'));
    box.append(body, h('div', { class: 'actions' },
      h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('close'))));
    fetch('/changelog.md', { cache: 'no-store' }).then((r) => {
      if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
      return r.text();
    }).then((text) => { body.innerHTML = renderMarkdown(text); })
      .catch((e) => body.replaceChildren(h('p', { class: 'help' }, e.message)));
  });
}

/*
    La page des composants et de leurs licences : ceux de l'application web, que liste
    vendor/components.json ; ceux des programmes et des moteurs, dont l'installateur copie les
    textes dans le dossier des licences des programmes ; et les modèles installés, avec la licence
    de leur entrée dans le catalogue.  Un texte s'ouvre dans la page, sous sa ligne.
*/
let licensesSequence = 0;

// Le rôle et la licence de chaque texte du dossier des licences, par son fichier.
const programTexts = {
  'albireo.txt': ['albireo', 'Boost Software License 1.0'], 'dlib.txt': ['dlib', 'Boost Software License 1.0'],
  'mbedtls.txt': ['mbedtls', 'Apache License 2.0 or GPL 2.0 or later'], 'cpp-httplib.txt': ['httplib', 'MIT License'],
  'zlib.txt': ['zlib', 'zlib License'], 'sqlite.txt': ['sqlite', null],
  'libpng.txt': ['libpng', 'PNG Reference Library License 2'], 'libjpeg.txt': ['libjpeg', 'IJG License']
};

async function licenseText(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.text();
}

function licenseHead(columns) {
  return h('div', { class: 'lic-row lic-head', 'aria-hidden': 'true' }, columns.map((c) => h('div', null, c)),
    h('div'));
}

/*
    Une ligne de la page des licences : ses cellules, puis un bouton qui ouvre ses textes en dessous
    quand load est donné, load renvoyant [[file, text]...], ou l'élément action.
*/
function licenseRow(cells, load, action) {
  const box = h('div', { class: 'lic-text hidden' });
  let loaded = false;
  let button = null;
  if (load) {
    button = h('button', {
      class: 'button small', type: 'button', 'aria-expanded': 'false',
      html: icon('chevron') + '<span>' + escapeHtml(t('licenseShow')) + '</span>'
    });
    button.addEventListener('click', async () => {
      const open = box.classList.toggle('hidden') === false;
      button.setAttribute('aria-expanded', String(open));
      button.classList.toggle('open', open);
      if (!open || loaded) return;
      loaded = true;
      box.replaceChildren(h('div', { class: 'empty-note' }, t('licenseLoading')));
      try {
        const parts = await load();
        box.replaceChildren(...parts.flatMap(([file, text]) => [parts.length > 1 ? h('div', { class: 'lic-file' }, file)
          : null, h('pre', null, text)]).filter(Boolean));
      } catch (e) {
        loaded = false;
        box.replaceChildren(h('div', { class: 'error-box' }, t('licenseReadFailed')));
      }
    });
  }
  return [h('div', { class: 'lic-row' }, cells.map(([cls, v]) => h('div', { class: cls }, v)),
    h('div', { class: 'lic-action' }, button || action)), box];
}

export async function renderLicenses() {
  const sequence = ++licensesSequence;
  const head = [h('h2', { html: icon('book') + escapeHtml(t('licensesTitle')) }),
    h('p', { class: 'sub' }, t('licensesIntro'))];
  ui.content.replaceChildren(h('div', { class: 'page licenses' }, head,
    h('div', { class: 'empty-note' }, t('licenseLoading'))));
  const [web, programs, models] = await Promise.all([
    api.getJSON('/vendor/components.json').catch(() => null),
    api.getJSON('/albireo/v1/licenses').catch(() => null),
    api.models(language()).catch(() => null)
  ]);
  if (sequence !== licensesSequence || state.view !== 'licenses') return;
  const columns = [t('licenseComponent'), t('licenseVersion'), t('licenseRole'), t('licenseName')];
  const card = (iconName, title, help, rows, empty) => h('div', { class: 'card' },
    h('h3', { html: icon(iconName) + escapeHtml(title) }), help ? h('p', { class: 'help' }, help) : null,
    rows.length ? h('div', { class: 'lic-table' }, rows) : h('div', { class: 'empty-note' }, empty));

  // L'application web, d'après la liste que gardent ses mises à jour.
  const webRows = [];
  for (const c of (web && Array.isArray(web.components)) ? web.components : []) {
    const key = 'component_' + c.id;
    webRows.push(licenseRow([['lic-name', c.name], ['lic-version', c.version || '-'],
      ['lic-role', t(key) === key ? c.role || '' : t(key)], ['lic-license', c.license_name || c.license || '-']],
    () => Promise.all((c.license_files || []).map(async (f) => [f, await licenseText('/' + f)]))));
  }
  if (webRows.length) webRows.unshift(licenseHead(columns));

  // Les programmes et les moteurs, d'après le dossier des licences de l'installation.
  const programRows = [];
  for (const p of Array.isArray(programs) ? programs : []) {
    const [id, license] = programTexts[p.file] || ['runtime', '-'];
    programRows.push(licenseRow([['lic-name', p.name || p.file], ['lic-version', p.version || '-'],
      ['lic-role', t('program_' + id)], ['lic-license', license === null ? t('licensePublicDomain') : license]],
    () => licenseText('/albireo/v1/licenses/' + encodeURIComponent(p.file)).then((text) => [[p.file, text]])));
  }
  if (programRows.length) programRows.unshift(licenseHead(columns));

  // Les modèles installés, les modèles de langage puis les modèles d'embedding, avec les fonctions qu'ils servent.
  const modelRows = [];
  const active = (models && models.active) || {};
  const installed = models
    ? (models.models || []).filter((m) => m.installed).concat(models.embedding_models || []) : [];
  for (const m of installed) {
    const functions = roles.concat(['embedding']).filter((r) => active[r] === m.id).map((r) => t('role_' + r));
    const source = /^https:\/\//.test(m.source || '') ? h('a', {
      class: 'button small', href: m.source, target: '_blank', rel: 'noopener',
      html: icon('external') + '<span>' + escapeHtml(t('licenseSource')) + '</span>'
    }) : null;
    modelRows.push(licenseRow([['lic-name', m.name || m.id], ['lic-version', m.publisher || '-'],
      ['lic-role', functions.join(', ') || '-'], ['lic-license', m.license || t('licenseNotStated')]], null, source));
  }
  if (modelRows.length) {
    modelRows.unshift(licenseHead([t('licenseModel'), t('licensePublisher'), t('licenseFunctions'), t('licenseName')]));
  }

  ui.content.replaceChildren(h('div', { class: 'page licenses' }, head,
    card('code', t('licensesWeb'), t('licensesWebHelp'), webRows, t('licenseReadFailed')),
    card('cpu', t('licensesPrograms'), programRows.length ? t('licensesProgramsHelp') : null, programRows,
      programs ? t('licensesProgramsNone') : t('serviceDown')),
    card('spark', t('licensesModels'), t('licensesModelsHelp'), modelRows,
      models ? t('licensesModelsNone') : t('serviceDown'))));
}
