// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Le panneau latéral (fork pour macOS : sorti d'app.js) : les projets et les discussions, épinglées puis par date,
// avec leur état (réponse en cours, non lue) et leur menu.

import * as tools from './tools.js';
import * as web from './web.js';
import { generations, unread } from './composer.js';
import { conversationMenu } from './conversations.js';
import { showPopover } from './dialogs.js';
import { h } from './dom.js';
import { t } from './i18n.js';
import { icon } from './icons.js';
import { dateGroup, excerptHtml, markTerms, projectActivity, projectById, queryTerms, searchConversations, searchDialog,
  sideLimit } from './lists.js';
import { escapeHtml } from './markdown.js';
import { go } from './navigation.js';
import { newProject } from './projects.js';
import { projectColors, roleIcons, state, ui } from './state.js';
import { searchProvider } from './status.js';
import { answeredElsewhere } from './tabs.js';
import { toolIcons, toolLabel } from './thread.js';

// Panneau latéral.
export function renderSidebar() {
  const scroll = ui.sideScroll;
  scroll.replaceChildren();
  if (state.search.trim()) {
    const terms = queryTerms(state.search);
    const hits = searchConversations(state.search);
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t('searchIn')));
    if (!hits.length) section.append(h('div', { class: 'empty-note' }, t('noResults')));
    for (const hit of hits) {
      const item = conversationItem(hit.conversation);
      item.title = hit.where === 'title' ? hit.text : t('found_' + hit.where) + ' : ' + hit.excerpt;
      if (hit.where !== 'title') {
        item.querySelector('.label').innerHTML = escapeHtml(hit.conversation.title || t('untitled'))
          + '<span class="side-excerpt">' + excerptHtml(hit.text, hit.at, terms, 60) + '</span>';
      } else {
        item.querySelector('.label').innerHTML = markTerms(hit.text, terms);
      }
      section.append(item);
    }
    scroll.append(section);
    return;
  }

  const projects = h('div', { class: 'side-section' },
    h('div', { class: 'side-title' }, h('span', null, t('projects')),
      h('span', { class: 'side-tools' },
        h('button', { type: 'button', title: t('searchProjects'), onclick: () => searchDialog('projects'), html: icon('search') }),
        h('button', { type: 'button', title: t('newProject'), onclick: () => newProject(false), html: icon('plus') }))));
  // Un projet se crée d'ici aussi bien que depuis une discussion.
  projects.append(h('div', { class: 'item new-item', onclick: () => newProject(false) },
    h('span', { html: icon('plus') }), h('span', { class: 'label' }, t('newProject'))));
  const recent = state.projects.slice().sort((a, b) => projectActivity(b) - projectActivity(a));
  for (const p of recent.slice(0, sideLimit)) {
    const count = state.conversations.filter((c) => c.projectId === p.id).length;
    projects.append(h('div', {
      class: 'item' + (state.view === 'project' && state.projectId === p.id ? ' active' : ''),
      onclick: () => go('#/p/' + p.id)
    }, h('span', { class: 'dot', style: { background: p.color || projectColors[0] } }),
    h('span', { class: 'label' }, p.name), h('span', { class: 'faint', style: { fontSize: '12px', color: 'var(--text-faint)' } },
      count ? String(count) : '')));
  }
  if (recent.length > sideLimit) {
    projects.append(h('div', { class: 'item more-item', onclick: () => searchDialog('projects') },
      h('span', { html: icon('search') }), h('span', { class: 'label' }, t('searchMoreProjects', { n: recent.length }))));
  }
  scroll.append(projects);

  // Les discussions : les épinglées, puis par date, sideLimit d'entre elles au plus.
  scroll.append(h('div', { class: 'side-section side-head' }, h('div', { class: 'side-title' }, h('span', null, t('discussions')),
    h('span', { class: 'side-tools' },
      h('button', { type: 'button', title: t('searchDiscussions'), onclick: () => searchDialog('discussions'), html: icon('search') })))));
  const pinned = state.conversations.filter((c) => c.pinned);
  const shown = pinned.slice(0, sideLimit);
  if (shown.length) {
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t('pinned')));
    for (const c of shown) section.append(conversationItem(c));
    scroll.append(section);
  }
  let left = sideLimit - shown.length;
  const groups = { today: [], yesterday: [], week: [], month: [], older: [] };
  for (const c of state.conversations) if (!c.pinned) groups[dateGroup(c.updated)].push(c);
  for (const g of Object.keys(groups)) {
    if (!groups[g].length || left <= 0) continue;
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t(g)));
    for (const c of groups[g].slice(0, left)) section.append(conversationItem(c));
    left -= Math.min(left, groups[g].length);
    scroll.append(section);
  }
  if (!state.conversations.length) scroll.append(h('div', { class: 'empty-note' }, t('noDiscussions')));
  if (state.conversations.length > sideLimit) {
    scroll.append(h('div', { class: 'side-section' }, h('div', { class: 'item more-item', onclick: () => searchDialog('discussions') },
      h('span', { html: icon('search') }), h('span', { class: 'label' },
        t('searchMoreDiscussions', { n: state.conversations.length })))));
  }
}

// Vrai quand la discussion c reçoit une réponse, dans cet onglet ou dans un autre.
export function answering(c) {
  return !!c && (generations.has(c.id) || answeredElsewhere(c));
}

/*
    Une discussion du panneau latéral.  À sa gauche, un point clignote pendant qu'elle reçoit une réponse, dans cet
    onglet ou dans un autre, et reste bleu une fois sa réponse terminée pendant qu'une autre discussion était affichée,
    jusqu'à ce qu'elle soit ouverte.
*/
function conversationItem(c) {
  const p = c.projectId ? projectById(c.projectId) : null;
  const active = state.view === 'chat' && state.current && state.current.id === c.id;
  const mark = answering(c) ? 'live' : unread.has(c.id) ? 'unread' : '';
  const markText = mark ? t(mark === 'live' ? 'answering' : 'unreadAnswer') : '';
  return h('div', { class: 'item' + (active ? ' active' : ''), onclick: () => go('#/c/' + c.id) },
    mark ? h('span', { class: 'gen-dot ' + mark, title: markText, role: 'img', 'aria-label': markText }) : null,
    p ? h('span', { class: 'dot', title: p.name, style: { background: p.color || projectColors[0] } })
      : h('span', { html: icon(roleIcons[c.role] || 'chat') }),
    h('span', { class: 'label' }, c.title || t('untitled')),
    c.pinned ? h('span', { class: 'pin', html: icon('pin') }) : null,
    h('button', {
      class: 'more', type: 'button', title: '…', html: icon('more'),
      onclick: (e) => { e.stopPropagation(); conversationMenu(c, e.currentTarget); }
    }));
}

/*
    Ce que font le mode agent et l'accès au web, et les outils que chacun d'eux donne au modèle, avec leur usage ; le
    fournisseur de la recherche web en dernier, tant que le service en propose un.
*/
export function agentHelp(anchor) {
  const row = (name) => h('div', { class: 'popover-tool' },
    h('span', { html: icon(toolIcons[name] || 'tool') }),
    h('span', null, h('b', null, toolLabel(name)), h('span', { class: 'desc' }, t('toolHint_' + name))));
  const provider = searchProvider();
  showPopover(anchor, [
    h('div', { class: 'head' }, t('agentHelpTitle')),
    h('p', null, t('agentHelp')),
    h('div', { class: 'popover-tools' }, tools.definitions.map((d) => row(d.function.name))),
    h('p', null, t('webHelp')),
    h('div', { class: 'popover-tools' }, web.definitions(true).map((d) => row(d.function.name))),
    provider ? h('p', { class: 'search-provider' }, t('webSearchBy', { provider })) : null
  ]);
}
