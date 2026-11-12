// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La zone principale et le fil d'une discussion (fork pour macOS : sortis d'app.js) : la barre du haut, l'accueil, les
// questions et les réponses avec leur raisonnement, leurs appels d'outils, leurs fichiers et leurs actions, le
// défilement du fil et l'affichage d'une réponse au fil de son écriture.

import * as images from './images.js';
import * as web from './web.js';
import { autoGrow, busyHere, liveMessage, regenerate, renderComposerState, sendMessage } from './composer.js';
import { maxContextImages } from './context.js';
import { attachmentIds, conversationMenu, deleteQuestion, exportAnswer, liveCopy, moveMenu,
  persist } from './conversations.js';
import { showMenu } from './dialogs.js';
import { extension } from './documents.js';
import { copyText, download, formatDecimal, formatDuration, formatNumber, formatShortDuration, formatSize, h,
  iconButton, mimeOf, toast } from './dom.js';
import { attachMenu, contextImages, imageIds, imageLimitText, imageRoom, imageThumb, pastedFiles, pendingImageCount,
  readFiles } from './files.js';
import { compactions } from './generation.js';
import { language, t, tn } from './i18n.js';
import { icon } from './icons.js';
import { forgetDocument } from './indexing.js';
import { projectById } from './lists.js';
import { attachCodeButtons, escapeHtml, renderMarkdown } from './markdown.js';
import { go } from './navigation.js';
import { saveSetting } from './preferences.js';
import { renderProject } from './projects.js';
import { changelogDialog, renderLicenses, settingsDialog } from './settings.js';
import { renderSidebar } from './sidebar.js';
import { maxAgentSteps, state, ui } from './state.js';
import { acceptFor, modelName, renderStatusLine, roleCapabilities, roleModel, toolsAvailable } from './status.js';
import { answeredElsewhere, refreshViews } from './tabs.js';

// Zone principale.
export function renderAll() {
  renderSidebar();
  renderTopbar();
  renderMain();
  renderComposerState();
  renderStatusLine();
  ui.incognitoButton.classList.toggle('on', state.incognito);
  ui.banner.classList.toggle('hidden', !(state.view === 'chat' && state.current && state.current.incognito));
}

// L'engrenage de la barre du haut : les réglages, les réglages du service, l'onglet « À propos » et les notes de
// version.
function gearButton() {
  return iconButton('gear', t('settings'), (e) => showMenu(e.currentTarget, [
    { icon: 'gear', label: t('settings'), action: () => settingsDialog('general') },
    { icon: 'spark', label: t('modelsTab'), action: () => settingsDialog('modelsTab') },
    { icon: 'cpu', label: t('serviceTab'), action: () => settingsDialog('serviceTab') },
    { icon: 'info', label: t('about'), action: () => settingsDialog('about') },
    { icon: 'file', label: t('changelog'), action: () => changelogDialog() }
  ]), 'icon-button topbar-gear');
}

export function renderTopbar() {
  const bar = ui.topbar;
  bar.replaceChildren();
  bar.append(iconButton('sidebar', t('collapse'), toggleSidebar));
  if (state.view === 'project') {
    const p = projectById(state.projectId);
    bar.append(h('div', { class: 'title' }, p ? p.name : ''), gearButton());
    return;
  }
  if (state.view === 'licenses') {
    bar.append(h('div', { class: 'title' }, t('licensesTitle')), gearButton());
    return;
  }
  const c = state.current;
  const title = h('div', { class: 'title', contenteditable: c && c.messages.length ? 'true' : 'false', spellcheck: 'false' },
    c && c.title ? c.title : t('untitled'));
  title.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); title.blur(); } });
  // Le texte que montre le titre quand l'utilisateur commence à le modifier : le laisser inchangé ne renomme rien, et
  // montre le titre que le modèle a pu écrire entre-temps.
  let shownText = '';
  title.addEventListener('focus', () => { shownText = title.textContent.trim(); });
  title.addEventListener('blur', async () => {
    if (!c || !c.messages.length) return;
    const live = liveCopy(c);
    const v = title.textContent.trim();
    if (v && v !== shownText && v !== live.title) {
      live.title = v;
      // Un titre de l'utilisateur reste : le modèle ne nomme plus la discussion.
      live.titleSource = 'user';
      delete live.titleQuestion;
      await persist(live);
      refreshViews();
    } else {
      title.textContent = live.title || t('untitled');
    }
  });
  bar.append(title);
  if (c && c.incognito) bar.append(h('span', { class: 'chip incognito', html: icon('incognito') + escapeHtml(t('incognito')) }));
  if (c && c.projectId) {
    const p = projectById(c.projectId);
    if (p) {
      bar.append(h('button', {
        class: 'chip', type: 'button', title: t('moveToProject'),
        onclick: (e) => moveMenu(c, e.currentTarget)
      }, h('span', { class: 'swatch', style: { background: p.color } }), p.name));
    }
  }
  // Le fork n'a plus de sélecteur de fonction : le modèle se choisit dans les paramètres (onglet Modèles).
  if (c && c.messages.length) bar.append(iconButton('more', '…', (e) => conversationMenu(c, e.currentTarget)));
  bar.append(gearButton());
}

function toggleSidebar() {
  const open = ui.sidebar.classList.toggle('collapsed') === false;
  saveSetting('sidebar', open);
}

/*
    Dessine la zone principale pour la vue.  Ensuite, les images qu'elle ne montre plus rendent leurs URL d'objet.
*/
export function renderMain() {
  drawMain();
  releaseHiddenImages();
}

// Révoque les URL d'objet des images qu'aucun élément de la page ne montre (data-image), visionneuse comprise.
function releaseHiddenImages() {
  images.releaseExcept(new Set(Array.from(document.querySelectorAll('[data-image]'), (el) => el.dataset.image)));
}

function drawMain() {
  stopClock();
  // La page d'accueil met la zone de saisie au milieu de l'écran (welcome-mode) ; les autres vues la laissent en bas.
  ui.main.classList.remove('welcome-mode');
  // Les boutons qui mènent aux extrémités du fil attendent le prochain défilement de l'utilisateur.
  if (ui.jump) ui.jump.classList.remove('on');
  if (state.view === 'project') {
    const p = projectById(state.projectId);
    ui.composerWrap.classList.add('hidden');
    ui.thread = null;
    if (p) renderProject(p);
    // Un projet supprimé entre-temps, dans cet onglet ou dans un autre, quitte sa page.
    else go('#/');
    return;
  }
  if (state.view === 'licenses') {
    ui.composerWrap.classList.add('hidden');
    ui.thread = null;
    renderLicenses();
    return;
  }
  // La zone de saisie de nouveau affichée prend la hauteur de son texte, qu'elle ne pouvait pas mesurer quand elle
  // était cachée.
  const hidden = ui.composerWrap.classList.contains('hidden');
  ui.composerWrap.classList.remove('hidden');
  if (hidden) autoGrow();
  const c = state.current;
  if (!c.messages.length) {
    ui.main.classList.add('welcome-mode');
    renderWelcome();
    return;
  }
  const thread = h('div', { class: 'thread' });
  c.messages.forEach((m, i) => thread.append(messageElement(c, m, i, i === c.messages.length - 1)));
  ui.content.replaceChildren(thread);
  ui.thread = thread;
  showElsewhere();
  startClock();
  scrollToEnd(true);
}

function renderWelcome() {
  const c = state.current;
  const name = state.settings.userName ? ', ' + state.settings.userName : '';
  const p = c.projectId ? projectById(c.projectId) : null;
  let notice = null;
  if (state.statusError) notice = h('div', { class: 'error-box' }, t('serviceDown'));
  else if (state.status && !roleModel(c.role)) notice = h('div', { class: 'error-box' }, t('noModel'));
  // Avec l'agent activé, ce qu'il fait et les outils dont il dispose.
  const caps = roleCapabilities(c.role);
  const agent = c.agent && toolsAvailable(caps, c.reasoning && caps.reasoning)
    ? h('div', { class: 'agent-note' }, h('span', { html: icon('tool') }), h('span', null, t('agentHelp'))) : null;
  ui.content.replaceChildren(h('div', { class: 'welcome' },
    h('img', { class: 'hero', src: 'img/albireo-mark.svg', alt: '' }),
    h('h1', { html: escapeHtml(t('welcome', { name })).replace('Albireo', '<span>Albireo</span>') }),
    h('p', null, p ? t('newProjectDiscussion', { name: p.name }) : t('welcomeSub')),
    notice, agent));
  ui.thread = null;
}

/*
    Le fil suit sa fin tant que l'utilisateur l'y laisse (ui.stick) : une réponse en cours d'écriture, et une zone de
    saisie qui grandit, gardent ses dernières lignes en vue.  La position est écrite sans être lue d'abord ; les
    défilements que fait l'application se distinguent de ceux de l'utilisateur par la position qu'ils laissent
    (ui.ownTop).
*/
export function scrollToEnd(force) {
  if (!force && !ui.stick) return;
  const s = ui.content;
  s.scrollTop = s.scrollHeight;
  ui.ownTop = s.scrollTop;
  ui.stick = true;
}

// Un défilement du fil : s'il reste à sa fin, et, pour un défilement de l'utilisateur, les boutons qui mènent à ses
// extrémités.
export function threadScrolled() {
  const s = ui.content;
  const own = Math.abs(s.scrollTop - ui.ownTop) < 2;
  if (!own) ui.stick = s.scrollHeight - s.scrollTop - s.clientHeight < 80;
  if (!own) showJump(true);
  else if (ui.jump.classList.contains('on')) showJump(false);
}

/*
    Les boutons qui mènent au début et à la fin du fil s'affichent pendant que l'utilisateur fait défiler un fil plus
    long que la vue, chacun quand on est loin de son extrémité, et partent un moment après le défilement sauf si le
    pointeur ou le focus est sur eux.  keep dit que l'utilisateur fait défiler, ce qui les affiche de nouveau.
*/
export const jumpDelay = 1600;

function showJump(keep) {
  const s = ui.content;
  const thread = state.view === 'chat' && ui.thread && ui.thread.isConnected;
  const fits = !thread || s.scrollHeight <= s.clientHeight + 4;
  const atStart = s.scrollTop < 8;
  const atEnd = s.scrollHeight - s.scrollTop - s.clientHeight < 8;
  ui.jumpStart.classList.toggle('hidden', fits || atStart);
  ui.jumpEnd.classList.toggle('hidden', fits || atEnd);
  const on = !fits && !(atStart && atEnd) && (keep || ui.jump.classList.contains('on'));
  ui.jump.classList.toggle('on', on);
  clearTimeout(ui.jumpTimer);
  if (on) ui.jumpTimer = setTimeout(hideJump, jumpDelay);
}

export function hideJump() {
  clearTimeout(ui.jumpTimer);
  if (ui.jump.matches(':hover') || ui.jump.contains(document.activeElement)) {
    ui.jumpTimer = setTimeout(hideJump, jumpDelay);
    return;
  }
  ui.jump.classList.remove('on');
}

// Va aussitôt au début ou à la fin du fil.
export function jumpTo(end) {
  const s = ui.content;
  if (end) {
    scrollToEnd(true);
  } else {
    s.scrollTop = 0;
    ui.ownTop = s.scrollTop;
    ui.stick = false;
  }
  showJump(true);
}

/*
    La zone de saisie repose sur le bas du fil : la hauteur qu'elle prend, quels que soient son texte, ses pièces
    jointes et les outils de l'agent, va dans --composer-room, que le bas du fil et les boutons au-dessus laissent
    libre.  Un fil à sa fin reste à sa fin.
*/
export function watchComposer() {
  if (ui.composerObserver) ui.composerObserver.disconnect();
  ui.composerObserver = null;
  if (typeof ResizeObserver !== 'function') return;
  ui.composerObserver = new ResizeObserver(() => {
    const height = ui.composerWrap.offsetHeight;
    if (height === ui.composerHeight) return;
    ui.composerHeight = height;
    ui.main.style.setProperty('--composer-room', height + 'px');
    if (ui.stick && ui.thread && ui.thread.isConnected) scrollToEnd(true);
  });
  ui.composerObserver.observe(ui.composerWrap);
}

function messageElement(c, m, index, last) {
  if (m.role === 'user') return userElement(c, m, index, last);
  return assistantElement(c, m, index, last);
}

// Le badge d'un document : son format sur la couleur de sa famille.
export function formatBadge(a) {
  const ext = extension(a.name || '') || String(a.format || '').toLowerCase();
  const family = /^(docx?|docm|dotx|odt|rtf)$/.test(ext) ? 'word' : /^(xlsx?|xlsm|xltx|ods|csv|tsv)$/.test(ext) ? 'sheet'
    : /^(pptx?|pptm|ppsx|potx|odp)$/.test(ext) ? 'slides' : ext === 'pdf' ? 'pdf'
    : /^(md|markdown|txt|rst)$/.test(ext) ? 'text' : 'code';
  const label = (ext || 'txt').slice(0, 4).toUpperCase();
  return h('span', { class: 'format-badge ' + family, 'aria-hidden': 'true' }, label);
}

export function attachmentChip(a, removable, onremove) {
  return h('span', { class: 'attachment', title: a.name },
    formatBadge(a), h('span', { class: 'name' }, a.name),
    h('span', { class: 'size' }, (a.format || '') + (a.size ? ' · ' + formatSize(a.size) : '')
      + (a.mode === 'search' ? ' · ' + t('searchedDocument') : '')),
    removable ? h('button', { type: 'button', title: t('removeAttachment'), 'aria-label': t('removeAttachment'),
      html: icon('close'), onclick: onremove }) : null);
}

function userElement(c, m, index, last) {
  const el = h('div', { class: 'message user' + (last ? ' last' : '') });
  el.dataset.index = index;
  el.dataset.id = m.id || '';
  // Les images d'une question qui a quitté le contexte s'affichent grisées : le modèle ne les reçoit plus.
  const pictures = m.images || [];
  if (pictures.length) {
    const outside = index < (c.contextStart || 0);
    el.append(h('div', { class: 'attachments image-thumbs' }, pictures.map((img) => imageThumb(img, { list: pictures, outside }))));
  }
  if (m.attachments && m.attachments.length) {
    el.append(h('div', { class: 'attachments' }, m.attachments.map((a) => attachmentChip(a))));
  }
  // Une question faite seulement d'images n'affiche pas de bulle vide.
  if (m.content || !pictures.length) el.append(h('div', { class: 'bubble' }, m.content));
  // L'heure de la question s'affiche avant ses boutons, qui apparaissent quand le pointeur est dessus.
  el.append(h('div', { class: 'meta' }, timeElement(m),
    iconButton('copy', t('copy'), () => copyText(m.content)),
    lockable(iconButton('history', t('replayQuestion'), () => replayQuestion(c, m)), 'replay', pictures.length),
    lockable(iconButton('edit', t('edit'), () => editMessage(c, m, index, el)), 'busy'),
    lockable(iconButton('trash', t('deleteQuestion'), () => deleteQuestion(c, index)), 'busy')));
  return el;
}

/*
    L'heure d'un message dans la langue de l'interface : relative dans la journée ("5 minutes
    ago", "just now" dans la minute), la date courte et l'heure au-delà, la date et l'heure
    complètes dans son infobulle.  Un message enregistré sans son heure n'en affiche aucune.
*/
function timeText(ts) {
  const age = Date.now() - ts;
  if (age < 60000) return t('justNow');
  if (age < 86400000) {
    const relative = new Intl.RelativeTimeFormat(language(), { numeric: 'auto' });
    return age < 3600000 ? relative.format(-Math.floor(age / 60000), 'minute')
      : relative.format(-Math.floor(age / 3600000), 'hour');
  }
  return new Intl.DateTimeFormat(language(), { dateStyle: 'short', timeStyle: 'short' }).format(new Date(ts));
}

function timeElement(m) {
  if (typeof m.created !== 'number' || !Number.isFinite(m.created)) return null;
  const full = new Intl.DateTimeFormat(language(), { dateStyle: 'full', timeStyle: 'medium' })
    .format(new Date(m.created));
  return h('time', { class: 'msg-time', datetime: new Date(m.created).toISOString(), title: full,
    'data-time': String(m.created) }, timeText(m.created));
}

// Les heures relatives du fil affiché, réécrites chaque minute tant qu'il reste affiché.
let clockTimer = 0;

function startClock() {
  clearInterval(clockTimer);
  clockTimer = setInterval(() => {
    if (!ui.thread || !ui.thread.isConnected) {
      stopClock();
      return;
    }
    for (const el of ui.thread.querySelectorAll('.msg-time[data-time]')) {
      el.textContent = timeText(Number(el.dataset.time));
    }
  }, 60000);
}

function stopClock() {
  clearInterval(clockTimer);
  clockTimer = 0;
}

/*
    Repose une question du fil : la même question, avec ses documents et ses images, rejoint la fin
    de la discussion comme nouvelle question, à laquelle le modèle répond là.  L'échange précédent
    reste tel quel, les documents gardent leurs passages et les images leurs Blob, que les deux
    questions partagent ; les images reprennent des places dans le contexte.
*/
async function replayQuestion(c, m) {
  const live = liveCopy(c);
  if (threadLocked() || state.current !== live) return;
  const pictures = m.images || [];
  if (pictures.length > imageRoom(live)) {
    toast(replayLimitText(pictures.length), true, 10000);
    return;
  }
  await sendMessage(m.content || '', (m.attachments || []).map((a) => Object.assign({}, a))
    .concat(pictures.map((img) => Object.assign({ kind: 'image' }, img))));
}

// Pourquoi une question avec n images ne peut pas être reposée : les places restantes pour les images dans le contexte.
function replayLimitText(n) {
  return t('replayImagesFull', { n, left: imageRoom(state.current), max: maxContextImages });
}

/*
    Les boutons qui changent une discussion attendent pendant qu'elle reçoit une réponse, ici ou
    dans un autre onglet (kind 'busy'), les boutons d'une réponse attendent qu'elle soit écrite
    (kind 'turn'), et le bouton qui repose une question attend aussi des places pour ses images
    dans le contexte (kind 'replay').  Ils restent désactivés entre-temps, et disent pourquoi.
*/
export function threadLocked() {
  return busyHere() || answeredElsewhere(state.current);
}

/*
    Marque un bouton de kind 'busy', de kind 'turn' pour une réponse dont live dit qu'elle est en
    cours d'écriture, ou de kind 'replay' pour une question avec live images.
*/
function lockable(button, kind, live) {
  button.dataset.lock = kind;
  button.dataset.title = button.title;
  if (kind === 'replay') button.dataset.images = String(live || 0);
  lockButton(button, kind === 'turn' ? !!live : threadLocked());
  return button;
}

function lockButton(button, busy) {
  if (button.dataset.lock === 'replay' && !busy) {
    const n = Number(button.dataset.images || 0);
    if (n > imageRoom(state.current)) {
      applyLock(button, true, replayLimitText(n));
      return;
    }
  }
  applyLock(button, busy);
}

function applyLock(button, locked, reason) {
  button.disabled = locked;
  button.title = locked ? reason || t('afterAnswer') : button.dataset.title;
}

// Donne aux boutons du fil affiché l'état de sa discussion.
export function lockThread() {
  if (!ui.content) return;
  const busy = threadLocked();
  for (const b of ui.content.querySelectorAll('button[data-lock]')) {
    lockButton(b, b.dataset.lock === 'turn' ? !!b.closest('.message.live') : busy);
  }
  showElsewhere();
}

// La note à la fin du fil pendant qu'un autre onglet répond dans sa discussion.
function showElsewhere() {
  if (!ui.thread || !ui.thread.isConnected) return;
  const note = ui.thread.querySelector(':scope > .elsewhere');
  const on = !busyHere() && answeredElsewhere(state.current);
  if (on && !note) {
    const spin = h('span', { class: 'spinner' });
    ui.thread.append(h('div', { class: 'progress-line elsewhere' }, spin, t('busyElsewhere')));
  } else if (!on && note) {
    note.remove();
  }
}

// Modifie une question : son texte, et ses documents et images joints, qui peuvent être retirés ou ajoutés.
function editMessage(c, m, index, el) {
  if (threadLocked()) return;
  let attachments = (m.attachments || []).concat((m.images || []).map((img) => Object.assign({ kind: 'image' }, img)));
  // Les images préparées pour cette modification, oubliées quand elle est annulée.
  const prepared = new Set();
  /*
      Les places pour les images de la question modifiée : les questions qui la suivent partent,
      les échanges qui la précèdent reviennent dans le contexte quand elle a été résumée, et les
      images en attente dans la zone de saisie gardent les leurs.
  */
  const places = () => maxContextImages - pendingImageCount()
    - (contextImages(c, (c.contextStart || 0) > index ? 0 : c.contextStart || 0) - contextImages(c, index));
  const room = () => Math.max(0, places() - attachments.filter((a) => a.kind === 'image').length);
  const area = h('textarea', { style: { width: '100%', minHeight: '90px', borderRadius: '12px', padding: '10px', border: '1px solid var(--border-strong)', background: 'var(--bg-input)' } });
  area.value = m.content;
  const chips = h('div', { class: 'attachments', style: { justifyContent: 'flex-start', marginBottom: '6px' } });
  const fileInput = h('input', { type: 'file', multiple: true, class: 'hidden' });
  const remove = (a) => {
    attachments = attachments.filter((x) => x !== a);
    if (prepared.has(a.id)) images.drop([a.id]);
    showChips();
  };
  const showChips = () => {
    const pictures = attachments.filter((a) => a.kind === 'image');
    chips.replaceChildren(...pictures.map((a) => imageThumb(a, { list: pictures, onremove: () => remove(a) })),
      ...attachments.filter((a) => a.kind !== 'image').map((a) => attachmentChip(a, true, () => remove(a))));
    fileInput.accept = acceptFor(c.role, room());
  };
  const add = async (files) => {
    const added = await readFiles(files, c.role, room());
    for (const a of added) if (a.kind === 'image') prepared.add(a.id);
    attachments = attachments.concat(added);
    showChips();
  };
  showChips();
  fileInput.addEventListener('change', () => {
    const files = Array.from(fileInput.files);
    fileInput.value = '';
    add(files);
  });
  area.addEventListener('paste', (e) => {
    const files = pastedFiles(e);
    if (files.length) {
      e.preventDefault();
      add(files);
    }
  });
  const box = h('div', { style: { width: '85%' } }, chips, area, h('div', { class: 'buttons', style: { justifyContent: 'flex-end', marginTop: '6px' } },
    h('button', { class: 'button', type: 'button', onclick: (e) => attachMenu(e.currentTarget, fileInput, add), html: icon('clip') + '<span>' + escapeHtml(t('attach')) + '</span>' }),
    fileInput,
    h('span', { class: 'grow' }),
    h('button', {
      class: 'button', type: 'button',
      onclick: () => {
        images.drop(Array.from(prepared));
        renderMain();
      }
    }, t('cancel')),
    h('button', {
      class: 'button primary', type: 'button',
      onclick: async () => {
        if (threadLocked()) return;
        const text = area.value.trim();
        if (!text && !attachments.length) return;
        // Une compression en cours déplace le début de la discussion : elle se termine d'abord.
        if (compactions.has(c.id)) await compactions.get(c.id).done;
        // Une question envoyée entre-temps reçoit une réponse : les messages restent tels quels.
        if (threadLocked() || state.current !== c) return;
        // Les images s'en tiennent aux places du contexte, qu'un autre onglet a pu prendre entre-temps.
        const pictures = attachments.filter((a) => a.kind === 'image');
        const left = places();
        if (pictures.length > left) {
          toast(imageLimitText(pictures.slice(Math.max(0, left)).map((a) => a.name), left), true, 10000);
          return;
        }
        /*
            Les documents qui ne sont plus joints, à cette question ou aux questions qui la
            suivent, et qui partent avec elle, quittent la recherche de la discussion, sauf si une
            question précédente les partage.  La mémoire relit la discussion à partir de la
            question modifiée.
        */
        const kept = attachmentIds(c.messages.slice(0, index));
        for (const a of attachments) kept.add(a.id);
        // Les images des questions qui partent s'en vont aussi, sauf si une question gardée ou la question modifiée les
        // contient.
        const keptImages = imageIds(c.messages.slice(0, index));
        for (const a of pictures) keptImages.add(a.id);
        const gone = [];
        for (const x of c.messages.slice(index)) {
          for (const a of x.attachments || []) {
            if (!kept.has(a.id) && (a.mode === 'search' || a.searchable)) forgetDocument(c.id, a.id);
          }
          for (const img of x.images || []) if (!keptImages.has(img.id)) gone.push(img.id);
        }
        images.remove(gone).catch(() => {});
        c.messages = c.messages.slice(0, index);
        if ((c.memoryMark || 0) > index) c.memoryMark = index;
        if ((c.contextStart || 0) > index) {
          // Les échanges reviennent dans le contexte, sans le résumé de ce qui suit.
          c.contextStart = 0;
          c.summary = null;
        }
        await sendMessage(text, attachments);
      }
    }, t('send'))));
  box.addEventListener('dragover', (e) => e.preventDefault());
  box.addEventListener('drop', (e) => {
    e.preventDefault();
    if (!e.dataTransfer || !e.dataTransfer.files.length) return;
    add(Array.from(e.dataTransfer.files));
  });
  el.replaceChildren(box);
  area.focus();
}

// Le texte d'une réponse, tel qu'il est quand on le copie.
function answerText(m) {
  return (m.steps || []).map((s) => s.content || '').join('\n\n').trim();
}

/*
    Une réponse, avec ses boutons.  Copier et exporter attendent que la réponse soit écrite, qu'elle
    soit affichée au fil de son écriture ou écrite en arrière-plan.
*/
function assistantElement(c, m, index, last) {
  const live = liveMessage(c, m);
  const el = h('div', { class: 'message assistant' + (last ? ' last' : '') + (live ? ' live' : '') });
  el.dataset.id = m.id || '';
  const bubble = h('div', { class: 'bubble' });
  el.append(bubble);
  fillAssistant(bubble, m, live);
  // Les boutons d'une réponse attendent qu'elle soit écrite ; regénérer attend aussi la discussion.
  const meta = h('div', { class: 'meta' });
  meta.append(lockable(iconButton('copy', t('copy'), () => copyText(answerText(m))), 'turn', live));
  if (last) meta.append(lockable(iconButton('refresh', t('regenerate'), () => regenerate(liveCopy(c))), 'busy'));
  const exporter = lockable(iconButton('download', t('exportAnswer'), () => showMenu(exporter, [
    { label: t('exportMarkdown'), icon: 'download', action: () => exportAnswer(liveCopy(c), index, 'md') },
    { label: t('exportJson'), icon: 'archive', action: () => exportAnswer(liveCopy(c), index, 'json') }
  ])), 'turn', live);
  meta.append(exporter);
  // L'heure de la réponse s'affiche après ses boutons.
  const when = timeElement(m);
  if (when) meta.append(when);
  const stats = statsText(m);
  if (stats) meta.append(h('span', { class: 'stats' }, stats));
  el.append(meta);
  el.dataset.index = index;
  return el;
}

function statsText(m) {
  const parts = [];
  if (m.model) parts.push(modelName(m.model));
  if (m.usage && m.usage.completion_tokens) {
    parts.push(tn('tokensCount', m.usage.completion_tokens, { n: formatNumber(m.usage.completion_tokens) }));
  }
  if (m.timings && m.timings.generation_per_second) {
    parts.push(t('tokensPerSecond', { n: formatDecimal(m.timings.generation_per_second, 1) }));
  }
  return parts.join(' · ');
}

// Le HTML des textes terminés des réponses, avec le texte dont il est tiré (WeakMap : step
// -> {text, html}), pour qu'un fil soit redessiné sans analyser de nouveau ses réponses.
const markdownCache = new WeakMap();

function stepHtml(s, live) {
  if (live) return renderMarkdown(s.content, true);
  const hit = markdownCache.get(s);
  if (hit && hit.text === s.content) return hit.html;
  const html = renderMarkdown(s.content, false);
  markdownCache.set(s, { text: s.content, html });
  return html;
}

// Ce que montre une étape, pour savoir si elle doit être redessinée.
function stepKey(s, active) {
  const calls = (s.toolCalls || []).map(toolKey);
  return JSON.stringify([active, (s.reasoning || '').length, (s.content || '').length, calls]);
}

// Ce que montre un appel d'outil, pour savoir s'il doit être redessiné.
function toolKey(tc) {
  return JSON.stringify([tc.status, tc.ok, (tc.arguments || '').length, tc.result == null ? -1 : tc.result.length,
    tc.ms || 0, !!tc.file]);
}

function stepElement(s, active, firstTool) {
  const el = h('div', { class: 'step' });
  if (s.reasoning) el.append(thinkingElement(s, active && !s.content && !(s.toolCalls || []).length));
  if (s.content) {
    const md = h('div', { class: 'md', html: stepHtml(s, active) });
    attachCodeButtons(md, t('copy'));
    el.append(md);
  }
  (s.toolCalls || []).forEach((tc, j) => el.append(toolElement(tc, firstTool + j + 1)));
  return el;
}

/*
    L'étape d'une réponse en cours d'écriture, dessinée sur place.  el est l'élément qu'a produit le dessin précédent,
    ou null.  Le raisonnement reçoit le texte arrivé depuis, le texte de la réponse ses nouveaux blocs (liveMarkdown),
    et un appel d'outil est redessiné quand son état change, pour qu'un dessin ne coûte que les nouveaux tokens, quelle
    que soit la longueur de la réponse.  Renvoie l'élément de l'étape.
*/
function liveStep(el, s, firstTool) {
  if (!el || !el.live) {
    el = h('div', { class: 'step' });
    el.live = { thinking: null, thinkingLive: false, label: null, text: null, shown: 0, md: null, cut: 0, tail: null,
      tailText: null, calls: [] };
  }
  const st = el.live;
  const calls = s.toolCalls || [];
  if (s.reasoning) {
    const thinking = !s.content && !calls.length;
    if (!st.thinking || st.thinkingLive !== thinking) {
      const d = thinkingElement(s, thinking);
      if (st.thinking) st.thinking.replaceWith(d);
      else el.prepend(d);
      st.thinking = d;
      st.thinkingLive = thinking;
      st.label = d.querySelector('summary > .label');
      st.text = d.querySelector('.thought').firstChild;
      st.shown = s.reasoning.length;
    } else {
      if (s.reasoning.length > st.shown) {
        st.text.appendData(s.reasoning.slice(st.shown));
        st.shown = s.reasoning.length;
      }
      if (thinking) st.label.textContent = thinkingLabel(s);
    }
  }
  if (s.content) {
    if (!st.md) {
      st.md = h('div', { class: 'md' });
      el.insertBefore(st.md, st.calls.length ? st.calls[0].el : null);
    }
    liveMarkdown(st, s.content);
  }
  calls.forEach((tc, j) => {
    const key = toolKey(tc);
    const old = st.calls[j];
    if (old && old.key === key) return;
    const wrap = toolElement(tc, firstTool + j + 1);
    if (old) old.el.replaceWith(wrap);
    else el.append(wrap);
    st.calls[j] = { key, el: wrap };
  });
  return el;
}

/*
    Le texte de l'étape en cours d'écriture, dessiné par blocs dans st.md : la partie que les tokens suivants ne
    changent plus (settledEnd) est dessinée une fois, bloc après bloc, et le reste, le bloc en cours d'écriture, est
    redessiné quand il change.  La réponse une fois écrite est redessinée en entier, à partir de son texte entier.
*/
function liveMarkdown(st, text) {
  const cut = settledEnd(text, st.cut);
  if (cut > st.cut) {
    const block = h('div', { class: 'md-block', html: renderMarkdown(text.slice(st.cut, cut), true) });
    attachCodeButtons(block, t('copy'));
    st.md.insertBefore(block, st.tail);
    st.cut = cut;
  }
  const rest = text.slice(st.cut);
  if (rest === st.tailText) return;
  if (!st.tail) {
    st.tail = h('div', { class: 'md-block' });
    st.md.append(st.tail);
  }
  st.tail.innerHTML = renderMarkdown(rest, true);
  attachCodeButtons(st.tail, t('copy'));
  st.tailText = rest;
}

/*
    La fin de la partie d'un texte en cours d'écriture que les tokens suivants ne changent plus : le début de la
    dernière ligne, après from, qui suit une ligne vide et commence à la marge, hors d'un bloc de code et hors d'une
    formule entre $$ ou \[ \].  Une ligne qui commence par une espace peut continuer un élément de liste, et reste avec
    ce qui suit.  Renvoie from quand il n'y a pas de telle ligne.  Le texte d'avant from se termine hors d'un bloc de
    code et d'une formule.
*/
function settledEnd(text, from) {
  let end = from;
  let fence = '';
  let math = false;
  let blank = false;
  for (let at = from; at < text.length;) {
    const nl = text.indexOf('\n', at);
    const line = text.slice(at, nl < 0 ? text.length : nl);
    if (!fence && !math && blank && /^\S/.test(line)) end = at;
    // La dernière ligne, pas encore terminée, dit seulement où commence un bloc.
    if (nl < 0) break;
    if (fence) {
      const close = /^\s*(`{3,}|~{3,})\s*$/.exec(line);
      if (close && close[1][0] === fence[0] && close[1].length >= fence.length) fence = '';
      blank = false;
    } else {
      const open = /^\s*(`{3,}|~{3,})/.exec(line);
      if (open) {
        fence = open[1];
      } else if (((line.match(/\$\$/g) || []).length + (line.match(/\\\[|\\\]/g) || []).length) % 2) {
        math = !math;
      }
      blank = !open && /^\s*$/.test(line);
    }
    at = nl + 1;
  }
  return end;
}

/*
    Remplit le corps d'une réponse : par étape, son raisonnement, son texte et ses appels d'outils,
    puis les notes sur son état.  Pendant l'écriture de la réponse, les étapes déjà dessinées qui
    n'ont pas changé restent telles quelles, l'étape en cours d'écriture est dessinée sur place
    (liveStep), et les notes ne changent que quand ce qu'elles disent change.  Une étape terminée ne
    change plus pendant l'écriture de la réponse : une fois dessinée en entier, elle est laissée
    telle quelle, pour qu'un dessin coûte la même chose quel que soit le nombre d'étapes.
*/
export function fillAssistant(bubble, m, live) {
  const steps = m.steps || [];
  const drawn = Array.from(bubble.children).filter((x) => x.classList.contains('step'));
  // Les étapes passent avant les notes.
  const place = (el) => bubble.insertBefore(el, bubble.querySelector(':scope > :not(.step)'));
  let tools = 0;
  steps.forEach((s, k) => {
    const active = live && k === steps.length - 1;
    const first = tools;
    tools += (s.toolCalls || []).length;
    const old = drawn[k] || null;
    if (active) {
      const el = liveStep(old, s, first);
      if (el !== old) {
        if (old) old.replaceWith(el);
        else place(el);
      }
      return;
    }
    if (live && old && !old.live && old.dataset.key) return;
    const key = stepKey(s, false);
    if (old && !old.live && old.dataset.key === key) return;
    const el = stepElement(s, false, first);
    el.dataset.key = key;
    if (old) old.replaceWith(el);
    else place(el);
  });
  for (let k = steps.length; k < drawn.length; ++k) drawn[k].remove();
  const notes = noteElements(m, live, steps);
  const key = notes.map((x) => x.outerHTML).join('');
  if (bubble.notesKey === key) return;
  for (const x of Array.from(bubble.children)) if (!x.classList.contains('step')) x.remove();
  bubble.append(...notes);
  bubble.notesKey = key;
}

// Les notes sur l'état d'une réponse, sous ses étapes.
function noteElements(m, live, steps) {
  const out = [];
  const note = (...children) => out.push(h('div', { class: 'progress-line' }, ...children));
  if (live && m.progress && m.progress.prompt_total > 256 && m.progress.prompt_done < m.progress.prompt_total) {
    const pct = Math.floor(100 * m.progress.prompt_done / m.progress.prompt_total);
    note(h('span', { class: 'spinner' }), t('readingContext', { p: pct }),
      h('div', { class: 'bar' }, h('div', { style: { width: pct + '%' } })));
  } else if (live && m.compacting) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('compacting')));
  } else if (live && m.preparing) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('preparingContext')));
  } else if (live && !steps.some((s) => s.content || s.reasoning || (s.toolCalls || []).length)) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('thinking') + '…'));
  }
  if (m.compacted) note(h('span', { html: icon('info') }), t('contextCompacted'));
  else if (m.trimmed) note(h('span', { html: icon('info') }), t('contextTrimmed'));
  if (m.imagesNamed) note(h('span', { html: icon('image') }), t('imagesNamed', { model: modelName(m.model) }));
  if (m.error) out.push(h('div', { class: 'error-box' }, t('errorPrefix', { message: m.error })));
  if (m.cancelled) note(t('cancelled'));
  if (m.repetition && !m.cancelled) note(h('span', { html: icon('info') }), t('answerRepeated'));
  if (!live && !m.error && !m.cancelled && !steps.some((s) => s.content || (s.toolCalls || []).length)) {
    note(t('emptyAnswer'));
  }
  if (m.agentLimit) note(t('agentLimit', { n: maxAgentSteps }));
  return out;
}

// L'étiquette d'un raisonnement en cours d'écriture, avec le temps qu'il a pris.
function thinkingLabel(s) {
  return t('thinking') + '…' + (s.reasoningStart ? ' ' + formatDuration(Date.now() - s.reasoningStart) : '');
}

function thinkingElement(s, live) {
  const d = h('details', { class: 'thinking' + (live ? ' live' : '') });
  // Fermé, pendant que le modèle réfléchit et après : l'utilisateur l'ouvre, et il reste ensuite
  // comme l'utilisateur l'a laissé.
  if (s.expanded) d.open = true;
  d.addEventListener('toggle', () => { s.expanded = d.open; });
  let label;
  if (live) {
    label = h('span', { class: 'label shimmer' }, thinkingLabel(s));
  } else {
    label = h('span', { class: 'label' },
      s.reasoningMs ? t('thoughtFor', { s: formatDuration(s.reasoningMs) }) : t('thoughtBriefly'));
  }
  d.append(h('summary', null, h('span', { html: icon('spark', 'spark') }), label, h('span', { html: icon('chevron', 'chev') })),
    h('div', { class: 'thought' }, s.reasoning));
  return d;
}

export const toolIcons = {
  calculator: 'calc', run_javascript: 'terminal', search_documents: 'search', document_outline: 'archive',
  read_document: 'book', search_discussions: 'history', current_datetime: 'clock', create_file: 'file',
  web_search: 'globe', fetch_page: 'link'
};

// Le nom d'un outil dans la langue de l'interface, son propre nom pour un outil qui n'en a pas.
export function toolLabel(name) {
  const key = 'tool_' + name;
  const label = t(key);
  return label === key ? String(name || '') : label;
}

// Les mots qui résument les arguments d'un appel d'outil, pour la ligne de l'appel.
function toolBrief(tc) {
  let a = null;
  try {
    a = JSON.parse(tc.arguments || '{}');
  } catch (e) {
    return String(tc.arguments || '').replace(/\s+/g, ' ').slice(0, 160);
  }
  if (!a || typeof a !== 'object') return '';
  const text = (v) => (typeof v === 'string' ? v : v == null ? '' : JSON.stringify(v));
  const quoted = (v) => (v ? '"' + text(v) + '"' : '');
  switch (tc.name) {
    case 'calculator': return text(a.expression);
    case 'run_javascript': {
      const lines = text(a.code).trim().split('\n');
      return lines[0] + (lines.length > 1 ? ' …' : '');
    }
    case 'search_documents': case 'search_discussions': case 'web_search': return quoted(a.query);
    case 'read_document': return [a.name, a.part].filter(Boolean).map(text).join(' · ');
    case 'fetch_page': return [a.url, a.start].filter(Boolean).map(text).join(' · ');
    case 'document_outline': case 'create_file': return text(a.name);
    default: return Object.values(a).map(text).join(', ').slice(0, 160);
  }
}

// L'état d'un appel d'outil : en cours d'écriture par le modèle, en cours d'exécution, terminé, en échec ou arrêté.
function toolState(tc) {
  if (tc.status === 'pending') return { cls: 'pending', label: t('toolPending'), spin: true };
  if (tc.status === 'running') return { cls: 'running', label: t('toolRunning'), spin: true };
  const time = tc.ms != null ? ' · ' + formatShortDuration(tc.ms) : '';
  if (tc.status === 'stopped') return { cls: 'stopped', label: t('toolStopped') + time };
  return tc.ok === false ? { cls: 'error', label: t('toolError') + time } : { cls: 'ok', label: t('toolDone') + time };
}

/*
    Les mots d'un appel de fetch_page qui a lu sa page (tc.web), ce que la règle des adresses a permis : l'adresse sous
    forme de lien qui s'ouvre dans un nouvel onglet sans la page d'Albireo comme opener ni referrer, puis le début de la
    partie lue.  null pour les autres appels, dont les mots sont du texte.
*/
function pageBrief(tc) {
  const url = tc.name === 'fetch_page' && tc.web ? web.addressKey(tc.web.url) : '';
  if (!url) return null;
  let start = 0;
  try {
    start = parseInt(JSON.parse(tc.arguments || '{}').start, 10) || 0;
  } catch (e) {
    start = 0;
  }
  return [h('a', { href: url, target: '_blank', rel: 'noopener noreferrer', title: url }, url),
    start > 0 ? ' · ' + start : null];
}

/*
    Un appel d'outil de l'agent ou du web, avec son numéro dans la réponse, le nom de l'outil dans
    la langue de l'interface, les mots de ses arguments, un court résultat sur la même ligne, son
    état et sa durée.  Déplié, il montre ses arguments et son résultat entier.
*/
function toolElement(tc, number) {
  const st = toolState(tc);
  const d = h('details', { class: 'tool-call ' + st.cls });
  const open = !!tc.open;
  if (open) d.open = true;
  d.addEventListener('toggle', () => { if (d.open !== open) tc.open = d.open; });
  const brief = pageBrief(tc) || toolBrief(tc);
  // La valeur d'un résultat de l'interpréteur s'affiche sans sa flèche.
  const result = tc.result == null ? '' : String(tc.result).trim().replace(/^=> /, '');
  // Un résultat d'une seule ligne courte s'affiche sur la ligne de l'appel.
  const inline = st.cls === 'ok' && result && result.length <= 60 && !result.includes('\n') ? result : '';
  let args = tc.arguments || '';
  try { args = JSON.stringify(JSON.parse(args), null, 2); } catch (e) { /* gardés tels qu'envoyés */ }
  d.append(h('summary', { title: tc.name },
    number ? h('span', { class: 'tool-number' }, String(number)) : null,
    h('span', { html: icon(toolIcons[tc.name] || 'tool', 'tool-icon') }),
    h('span', { class: 'tool-name' }, toolLabel(tc.name)),
    h('span', { class: 'tool-brief' }, brief, inline ? h('span', { class: 'tool-result' }, ' → ' + inline) : null),
    h('span', { class: 'state ' + st.cls }, st.spin ? h('span', { class: 'spinner small' }) : null, st.label),
    h('span', { html: icon('chevron', 'chev') })),
  h('div', { class: 'io' }, h('h6', null, t('toolInput') + ' · ' + tc.name), h('pre', null, h('code', null, args)),
    tc.result != null ? [h('h6', null, t('toolOutput')), h('pre', null, h('code', null, tc.result))] : null));
  const wrap = h('div', null, d);
  if (tc.file) {
    wrap.append(h('div', { class: 'attachments', style: { justifyContent: 'flex-start' } },
      h('span', {
        class: 'attachment file-out', title: t('download'),
        onclick: () => download(tc.file.name, tc.file.content, mimeOf(tc.file.name))
      }, h('span', { html: icon('download') }), h('span', { class: 'name' }, tc.file.name),
      h('span', { class: 'size' }, t('generatedFile')))));
  }
  return wrap;
}
