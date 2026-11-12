// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les listes du panneau latéral et la recherche (fork pour macOS : sorties d'app.js) : le regroupement des
// discussions par date, l'activité des projets, la recherche dans les discussions et les projets, avec ses extraits
// et ses termes marqués.

import * as store from './store.js';
import { dialog } from './dialogs.js';
import { h } from './dom.js';
import { language, t, tn } from './i18n.js';
import { icon } from './icons.js';
import { escapeHtml } from './markdown.js';
import { go } from './navigation.js';
import { projectColors, roleIcons, state } from './state.js';

// Listes.
export async function reloadLists() {
  state.conversations = await store.allConversations();
  state.projects = await store.allProjects();
}

export function projectById(id) {
  return state.projects.find((p) => p.id === id) || null;
}

export function dateGroup(ts) {
  const d = new Date(ts);
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const day = 86400000;
  if (d >= today) return 'today';
  if (d >= today - day) return 'yesterday';
  if (d >= today - 7 * day) return 'week';
  if (d >= today - 30 * day) return 'month';
  return 'older';
}

// Les listes du panneau latéral montrent au plus ce nombre de projets et de discussions ; les
// autres se trouvent par la recherche.
export const sideLimit = 20;
// Les recherches se lancent une fois que l'utilisateur a cessé de taper pendant ce nombre de millisecondes.
export const searchDelay = 200;

// Le texte tel que la recherche le compare : en minuscules, sans accents.
export function fold(text) {
  return String(text || '').normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
}

export function queryTerms(query) {
  return fold(query).split(/\s+/).filter(Boolean);
}

// La langue de l'interface, nommée en anglais pour les consignes du modèle : le français, la seule de ce fork.
export const languageNames = { fr: 'French' };

// La langue d'un texte : le français, la seule de ce fork.
export function textLanguage() {
  return 'fr';
}

/*
    Les textes repliés de la recherche, gardés avec le texte dont ils sont tirés (WeakMap : message
    ou discussion -> {text, folded}), pour qu'une recherche ne replie que ce qui a changé depuis la
    dernière.
*/
const messageFolds = new WeakMap();
const titleFolds = new WeakMap();

function foldedOf(cache, owner, text) {
  const hit = cache.get(owner);
  if (hit && hit.text === text) return hit.folded;
  const folded = fold(text);
  cache.set(owner, { text, folded });
  return folded;
}

// Le texte des messages d'une discussion, et sa forme repliée faite des messages repliés.
function conversationFolded(c) {
  const messages = c.messages || [];
  const texts = messages.map(messageText);
  return { text: texts.join('\n'), folded: messages.map((m, i) => foldedOf(messageFolds, m, texts[i])).join('\n') };
}

// La première position d'un terme de terms dans text, ou -1 quand l'un d'eux manque.  Un terme
// correspond au début, au milieu ou à la fin d'un mot.
function findTerms(text, terms) {
  return findFolded(fold(text), terms);
}

// findTerms pour un texte déjà replié.
function findFolded(f, terms) {
  let first = -1;
  for (const term of terms) {
    const i = f.indexOf(term);
    if (i < 0) return -1;
    if (first < 0 || i < first) first = i;
  }
  return first;
}

// Une partie du texte autour d'une position, avec les termes marqués.
export function excerptHtml(text, at, terms, width) {
  text = String(text || '').replace(/\s+/g, ' ');
  const w = width || 160;
  const start = Math.max(0, at - Math.floor(w / 3));
  const piece = text.slice(start, start + w);
  return (start > 0 ? '…' : '') + markTerms(piece, terms) + (start + w < text.length ? '…' : '');
}

// text en HTML, les termes de la recherche dans <mark>.
export function markTerms(text, terms) {
  text = String(text || '');
  const f = fold(text);
  // Le repliage garde la longueur de la plupart des textes ; les marques sont omises pour les autres.
  if (f.length !== text.length || !terms.length) return escapeHtml(text);
  const marked = new Array(text.length).fill(false);
  for (const term of terms) {
    for (let i = f.indexOf(term); i >= 0; i = f.indexOf(term, i + 1)) marked.fill(true, i, i + term.length);
  }
  let out = '';
  let open = false;
  for (let i = 0; i < text.length; ++i) {
    if (marked[i] !== open) {
      out += open ? '</mark>' : '<mark>';
      open = marked[i];
    }
    out += escapeHtml(text[i]);
  }
  return out + (open ? '</mark>' : '');
}

function messageText(m) {
  if (m.role === 'user') return m.content || '';
  return (m.steps || []).map((s) => s.content || '').join('\n');
}

/*
    Les discussions qui contiennent tous les termes d'une requête, quelles que soient leur casse et
    leurs accents, un terme correspondant au début, au milieu ou à la fin d'un mot : celles dont le
    titre les contient d'abord, puis celles dont les messages les contiennent, chaque groupe de la
    plus récente à la plus ancienne.  Chaque résultat dit où se trouvent les termes : {conversation,
    where: 'title', 'question', 'answer' ou 'messages' (plusieurs d'entre eux), at, text, excerpt}.
*/
export function searchConversations(query, limit) {
  const terms = queryTerms(query);
  if (!terms.length) return [];
  const titles = [];
  const contents = [];
  for (const c of state.conversations) {
    const title = c.title || '';
    const at = findFolded(foldedOf(titleFolds, c, title), terms);
    if (at >= 0) {
      titles.push({ conversation: c, where: 'title', at, text: title });
      continue;
    }
    // Le premier message qui contient tous les termes, sinon la discussion dans son ensemble.
    let hit = null;
    for (const m of c.messages || []) {
      const text = messageText(m);
      const i = findFolded(foldedOf(messageFolds, m, text), terms);
      if (i >= 0) {
        hit = { conversation: c, where: m.role === 'user' ? 'question' : 'answer', at: i, text, created: m.created };
        break;
      }
    }
    if (!hit) {
      const all = conversationFolded(c);
      if (findFolded(all.folded, terms) >= 0) {
        hit = { conversation: c, where: 'messages', at: all.folded.indexOf(terms[0]), text: all.text };
      }
    }
    if (hit) contents.push(hit);
  }
  const out = titles.concat(contents).slice(0, limit || 100);
  for (const x of out) x.excerpt = String(x.text).replace(/\s+/g, ' ').slice(Math.max(0, x.at - 60), x.at + 120);
  return out;
}

// L'heure de la dernière activité d'un projet : ses propres changements et ceux de ses discussions.
export function projectActivity(p) {
  let t0 = p.updated || p.created || 0;
  for (const c of state.conversations) if (c.projectId === p.id && c.updated > t0) t0 = c.updated;
  return t0;
}

/*
    Les projets qui contiennent tous les termes d'une requête, comme searchConversations : ceux
    dont le nom les contient d'abord, puis ceux dont la description, les instructions, les
    documents, les images ou les discussions les contiennent.  where : 'name', 'description',
    'instructions', 'document', 'image' ou 'discussion', avec le nom du document ou de l'image ou le
    titre de la discussion dans label.
*/
function searchProjects(query, limit) {
  const terms = queryTerms(query);
  if (!terms.length) return [];
  const names = [];
  const contents = [];
  for (const p of state.projects) {
    const at = findTerms(p.name, terms);
    if (at >= 0) {
      names.push({ project: p, where: 'name', at, text: p.name });
      continue;
    }
    // Chaque champ avec son texte replié, les longs repliés une seule fois (foldedOf).
    const fields = [['description', p.description || '', ''], ['instructions', p.instructions || '', '']]
      .map(([where, text, label]) => [where, text, label, fold(text)]);
    for (const f of p.files || []) {
      const text = f.name + '\n' + (f.text || '');
      fields.push(['document', text, f.name, foldedOf(messageFolds, f, text)]);
    }
    for (const img of p.images || []) {
      const text = img.name + '\n' + (img.description || '');
      fields.push(['image', text, img.name, foldedOf(messageFolds, img, text)]);
    }
    for (const c of state.conversations) {
      if (c.projectId !== p.id) continue;
      const all = conversationFolded(c);
      fields.push(['discussion', (c.title || '') + '\n' + all.text, c.title || t('untitled'),
        fold(c.title || '') + '\n' + all.folded]);
    }
    for (const [where, text, label, folded] of fields) {
      const i = findFolded(folded, terms);
      if (i >= 0) {
        contents.push({ project: p, where, at: i, text, label });
        break;
      }
    }
  }
  const byActivity = (a, b) => projectActivity(b.project) - projectActivity(a.project);
  return names.sort(byActivity).concat(contents.sort(byActivity)).slice(0, limit || 100);
}

/*
    La fenêtre de la recherche des projets (kind 'projects') ou des discussions : chaque résultat
    montre son titre, l'endroit où les mots ont été trouvés, et le passage qui les contient.
*/
export function searchDialog(kind) {
  const projects = kind === 'projects';
  return dialog((box, close) => {
    const input = h('input', { type: 'search', placeholder: t('searchPlaceholder'), 'aria-label': t('searchPlaceholder') });
    const count = h('div', { class: 'help' });
    const list = h('div', { class: 'search-results', role: 'listbox' });
    const open = (hit) => {
      close(true);
      go(projects ? '#/p/' + hit.project.id : '#/c/' + hit.conversation.id);
    };
    const show = () => {
      const terms = queryTerms(input.value);
      const hits = projects ? searchProjects(input.value) : searchConversations(input.value);
      list.replaceChildren();
      if (!terms.length) {
        count.textContent = t('searchHint');
        return;
      }
      count.textContent = tn('resultsCount', hits.length);
      for (const hit of hits) {
        const title = projects ? hit.project.name : (hit.conversation.title || t('untitled'));
        const date = new Date(projects ? projectActivity(hit.project) : hit.conversation.updated);
        const p = projects ? hit.project : (hit.conversation.projectId ? projectById(hit.conversation.projectId) : null);
        const inTitle = hit.where === 'title' || hit.where === 'name';
        const where = t('found_' + hit.where, { name: hit.label || '' });
        list.append(h('div', { class: 'search-result', role: 'option', tabindex: '0', onclick: () => open(hit),
          onkeydown: (e) => { if (e.key === 'Enter') open(hit); } },
          h('div', { class: 'line' },
            p ? h('span', { class: 'dot', style: { background: p.color || projectColors[0] } })
              : h('span', { html: icon(roleIcons[hit.conversation.role] || 'chat') }),
            h('span', { class: 'title', html: inTitle ? markTerms(title, terms) : escapeHtml(title) }),
            h('span', { class: 'date' }, date.toLocaleDateString(language()))),
          h('div', { class: 'where' }, h('span', { class: 'badge' }, where),
            inTitle ? null : h('span', { class: 'excerpt', html: excerptHtml(hit.text, hit.at, terms) }))));
      }
      if (!hits.length) list.append(h('div', { class: 'empty-note' }, t('noResults')));
    };
    let timer = 0;
    input.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(show, searchDelay); });
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        const first = list.querySelector('.search-result');
        if (first) first.click();
      }
    });
    box.classList.add('search-dialog');
    box.append(h('h2', null, projects ? t('searchProjects') : t('searchDiscussions')), input, count, list,
      h('div', { class: 'actions' }, h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('close'))));
    show();
    setTimeout(() => input.focus(), 0);
  });
}
