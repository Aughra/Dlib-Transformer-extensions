// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les projets (fork pour macOS : sortis d'app.js) : leur création et leur page (nom, description, instructions,
// connaissance faite de documents et d'images, discussions du projet).

import * as images from './images.js';
import * as rag from './rag.js';
import * as store from './store.js';
import { attachMenu, dropFiles, imageLimitText, imageRecord, itemLimitText,
  readFiles } from './files.js';
import { broadcast, go, keepOwnCopies, newChat, projectColors, refreshViews,
  renderSidebar } from './app.js';
import { generations } from './composer.js';
import { formatBadge, renderTopbar } from './thread.js';
import { reloadLists } from './lists.js';
import { roleCapabilities } from './status.js';
import { cancelDescription, forgetDocument, forgetOwner, knowledgeMode, projectImageRow, queueDescription, queueIndex,
  showIndexState } from './indexing.js';
import { confirmDialog, promptDialog } from './dialogs.js';
import { documentAccept } from './documents.js';
import { escapeHtml } from './markdown.js';
import { estimateTokens, maxProjectImages, maxProjectItems } from './context.js';
import { formatNumber, h, iconButton, storageFailed, toast } from './dom.js';
import { icon } from './icons.js';
import { language, t, tn } from './i18n.js';
import { state, ui } from './state.js';

// Projets.
export async function newProject(silent) {
  const name = await promptDialog(t('newProject'), '');
  if (name == null || !name.trim()) return null;
  const p = {
    id: store.uid(), name: name.trim(), description: '', instructions: '', files: [],
    color: projectColors[state.projects.length % projectColors.length], created: Date.now(), updated: Date.now()
  };
  try {
    await store.putProject(p);
  } catch (e) {
    storageFailed(e);
    return null;
  }
  state.projects = state.projects.concat([p]).sort((a, b) => a.name.localeCompare(b.name));
  broadcast({ type: 'projects' });
  refreshViews();
  if (!silent) go('#/p/' + p.id);
  return p;
}

/*
    Les modifications des projets dans cet onglet, l'une après l'autre pour chaque projet : task, une fonction async,
    démarre une fois la modification précédente du projet terminée, qu'elle ait réussi ou non.  Renvoie la promesse de
    task.
*/
const projectEdits = new Map();
function queueProjectEdit(id, task) {
  const run = (projectEdits.get(id) || Promise.resolve()).then(task);
  const tail = run.catch(() => {});
  projectEdits.set(id, tail);
  tail.then(() => {
    if (projectEdits.get(id) === tail) projectEdits.delete(id);
  });
  return run;
}

export function renderProject(p) {
  const main = ui.content;
  const convs = state.conversations.filter((c) => c.projectId === p.id);
  const name = h('input', { value: p.name });
  const description = h('input', { value: p.description || '' });
  const instructions = h('textarea', { rows: 8 });
  instructions.value = p.instructions || '';
  const heading = h('h2', null, h('span', { class: 'dot', style: { background: p.color } }), h('span', null, p.name));
  const sub = h('p', { class: 'sub' }, p.description || '');
  /*
      Enregistre une modification du projet, edit(project) changeant les champs qu'elle concerne.  La modification
      s'applique au projet tel qu'il est enregistré, relu, pour que ce qu'un autre onglet ou les descriptions des images
      ont écrit entre-temps reste ; les modifications de cet onglet s'exécutent l'une après l'autre
      (queueProjectEdit()), pour que chacune lise ce que la précédente a écrit, et qu'un projet supprimé entre-temps ne
      soit pas réécrit.  Le nom, la description et la couleur s'affichent aussitôt dans la page, la barre du haut, le
      panneau latéral et les autres onglets.
  */
  const save = (edit) => queueProjectEdit(p.id, async () => {
    let stored = p;
    try {
      stored = await store.getProject(p.id);
    } catch (e) {
      // La base est hors de portée : la copie de cet onglet prend la modification.
    }
    if (!stored) return;
    if (stored !== p) {
      for (const k of Object.keys(p)) if (!(k in stored)) delete p[k];
      Object.assign(p, stored);
    }
    edit(p);
    p.updated = Date.now();
    try {
      await store.putProject(p);
    } catch (e) {
      storageFailed(e);
    }
    const i = state.projects.findIndex((x) => x.id === p.id);
    if (i >= 0) state.projects[i] = p;
    state.projects.sort((a, b) => a.name.localeCompare(b.name));
    heading.lastChild.textContent = p.name;
    heading.firstChild.style.background = p.color;
    sub.textContent = p.description || '';
    broadcast({ type: 'projects' });
    renderSidebar();
    renderTopbar();
  });
  name.addEventListener('change', () => save((x) => { x.name = name.value.trim() || x.name; }));
  description.addEventListener('change', () => save((x) => { x.description = description.value; }));
  instructions.addEventListener('change', () => save((x) => { x.instructions = instructions.value; }));
  const colors = h('div', { class: 'colors' });
  for (const col of projectColors) {
    colors.append(h('button', {
      type: 'button', class: p.color === col ? 'on' : '', style: { background: col }, 'aria-label': col,
      onclick: async () => {
        await save((x) => { x.color = col; });
        renderProject(p);
      }
    }));
  }
  /*
    Les documents et les images, maxProjectItems au plus, dont maxProjectImages images : les documents sont indexés, les
    images décrites.  Un projet d'une version antérieure qui en contient davantage les garde, et n'en prend aucun autre
    tant que certains ne sont pas supprimés.
  */
  const items = () => (p.files || []).length + (p.images || []).length;
  const room = () => Math.max(0, maxProjectItems - items());
  const places = () => Math.max(0, maxProjectImages - (p.images || []).length);
  const fileInput = h('input', {
    type: 'file', multiple: true, class: 'hidden', accept: documentAccept() + (places() ? ',image/*' : '')
  });
  // Ajoute au projet les fichiers choisis avec le bouton ou déposés sur la carte de la connaissance.
  const addFiles = async (files) => {
    if (!room()) {
      toast(t('projectItemsFull', { max: maxProjectItems }), true, 9000);
      return;
    }
    const added = await readFiles(files, null, places(), room());
    // Les limites s'appliquent de nouveau au projet tel qu'il est enregistré, qu'un ajout précédent a pu remplir
    // entre-temps.
    const docs = [];
    const pictures = [];
    const late = [];
    const lateImages = [];
    await save((x) => {
      let free = Math.max(0, maxProjectItems - (x.files || []).length - (x.images || []).length);
      let imageFree = Math.max(0, maxProjectImages - (x.images || []).length);
      for (const a of added) {
        const image = a.kind === 'image';
        if (free <= 0) {
          late.push(a.name);
          continue;
        }
        if (image && imageFree <= 0) {
          lateImages.push(a.name);
          continue;
        }
        --free;
        if (!image) {
          docs.push(a);
          continue;
        }
        --imageFree;
        pictures.push(Object.assign(imageRecord(a), { state: 'pending', description: '' }));
      }
      x.files = (x.files || []).concat(docs);
      if (pictures.length) x.images = (x.images || []).concat(pictures);
    });
    if (lateImages.length) toast(imageLimitText(lateImages, places(), true), true, 10000);
    if (late.length) toast(itemLimitText(late, room()), true, 10000);
    // Les images sont enregistrées une fois que le projet qui les contient l'est (pruneImages).
    try {
      await images.keep(p.id, pictures, true);
    } catch (e) {
      storageFailed(e);
    }
    renderProject(p);
    for (const f of docs) queueIndex(p.id, f, true);
    for (const img of pictures) queueDescription(p.id, img.id);
  };
  fileInput.addEventListener('change', () => {
    const files = Array.from(fileInput.files);
    fileInput.value = '';
    return addFiles(files);
  });
  // Chaque document avec son format, l'estimation de ses tokens et l'état de son indexation sur une ligne.
  const fileList = h('div', { class: 'list' });
  for (const f of p.files || []) {
    const tokens = estimateTokens(f.text);
    const indexState = h('span', { class: 'faint index-state', 'data-index-doc': f.id, 'data-index-owner': p.id });
    fileList.append(h('div', { class: 'row' }, formatBadge(f), h('span', { class: 'label' }, f.name),
      h('span', { class: 'faint' }, f.format + ' · ' + tn('tokensEstimate', tokens, { n: formatNumber(tokens) }),
        indexState),
      iconButton('trash', t('delete'), async () => {
        const removed = forgetDocument(p.id, f.id);
        await save((x) => { x.files = (x.files || []).filter((y) => y.id !== f.id); });
        await removed;
        renderProject(p);
      })));
    showIndexState(indexState);
  }
  // Les images, chacune avec l'état de sa description.
  ui.projectPage = {
    id: p.id,
    removeImage: async (img) => {
      cancelDescription(img.id);
      const removed = forgetDocument(p.id, img.id);
      await save((x) => { x.images = (x.images || []).filter((y) => y.id !== img.id); });
      await removed;
      await images.remove([img.id]);
      renderProject(p);
    }
  };
  const pictures = p.images || [];
  const imagesBlock = h('div', { class: 'knowledge-images' },
    h('div', { class: 'images-head' }, h('span', { html: icon('image') }), h('span', null, t('projectImages')),
      h('span', { class: 'count', title: t('projectImagesCount', { n: pictures.length, max: maxProjectImages }) },
        pictures.length + ' / ' + maxProjectImages)),
    h('p', { class: 'help' }, t('knowledgeImagesHelp')),
    pictures.length ? h('div', { class: 'list image-list' }, pictures.map((img) => projectImageRow(p, img))) : null);
  // La part du contexte que prennent les documents, et la façon dont le modèle les lit.
  const general = roleCapabilities('general');
  const contextTokens = general.context || 0;
  const totalTokens = (p.files || []).reduce((n, f) => n + estimateTokens(f.text), 0);
  const capacity = h('p', { class: 'help' });
  if ((p.files || []).length) {
    const mode = knowledgeMode(p, general);
    const share = contextTokens ? Math.round(100 * totalTokens / contextTokens) : 0;
    capacity.textContent = t(mode === 'whole' ? 'knowledgeWhole' : 'knowledgeSearch',
      { p: share, tokens: tn('tokensCount', totalTokens, { n: formatNumber(totalTokens) }) })
      + (rag.embeddingModel() ? '' : ' ' + t('knowledgeNoVectors'));
  }
  const convList = h('div', { class: 'list' });
  for (const c of convs) {
    convList.append(h('div', { class: 'row click', onclick: () => go('#/c/' + c.id) },
      h('span', { html: icon('chat') }), h('span', { class: 'label' }, c.title || t('untitled')),
      h('span', { class: 'faint' }, new Date(c.updated).toLocaleDateString(language()))));
  }
  if (!convs.length) convList.append(h('div', { class: 'empty-note' }, t('noDiscussions')));
  // La carte de la connaissance, qui prend aussi les fichiers déposés sur elle, avec le nombre de ses éléments.
  const full = !room();
  const knowledge = h('div', { class: 'card' },
    h('h3', { html: icon('book') + escapeHtml(t('knowledge')) },
      h('span', { class: 'count', title: t('projectItemsCount', { n: items(), max: maxProjectItems }) },
        items() + ' / ' + maxProjectItems)),
    h('p', { class: 'help' }, t('knowledgeHelp')), capacity, fileList, imagesBlock,
    h('div', { class: 'buttons', style: { marginTop: '10px' } },
      h('button', {
        class: 'button', type: 'button', disabled: full, onclick: (e) => attachMenu(e.currentTarget, fileInput, addFiles),
        title: full ? t('projectItemsFull', { max: maxProjectItems }) : null,
        html: icon('upload') + '<span>' + escapeHtml(t('addFiles')) + '</span>'
      }),
      fileInput));
  dropFiles(knowledge, addFiles);

  main.replaceChildren(h('div', { class: 'page' }, heading, sub,
    h('div', { class: 'buttons', style: { marginBottom: '16px' } },
      h('button', { class: 'button primary', type: 'button', onclick: () => newChat(p.id), html: icon('plus') + '<span>' + escapeHtml(t('startInProject')) + '</span>' })),
    h('div', { class: 'card' },
      h('div', { class: 'field' }, h('label', null, t('projectName')), name),
      h('div', { class: 'field' }, h('label', null, t('description')), description),
      h('div', { class: 'field' }, h('label', null, t('color')), colors)),
    h('div', { class: 'card' }, h('h3', { html: icon('edit') + escapeHtml(t('instructions')) }),
      h('p', { class: 'help' }, t('instructionsHelp')), h('div', { class: 'field' }, instructions)),
    knowledge,
    h('div', { class: 'card' }, h('h3', { html: icon('chat') + escapeHtml(t('projectDiscussions')) }), convList),
    h('div', { class: 'buttons' },
      h('button', {
        class: 'button danger', type: 'button', html: icon('trash') + '<span>' + escapeHtml(t('deleteProject')) + '</span>',
        onclick: async () => {
          if (!await confirmDialog(t('deleteProject'), t('deleteProjectText', { name: p.name }))) return;
          // Les descriptions de ses images s'arrêtent, les documents quittent la recherche, en mémoire et dans
          // la base, et la discussion affichée quitte le projet comme le font les discussions enregistrées.
          for (const img of p.images || []) cancelDescription(img.id);
          await forgetOwner(p.id).catch(() => {});
          await store.deleteProject(p.id).catch(storageFailed);
          await images.removeOwner(p.id).catch(() => {});
          if (state.current && state.current.projectId === p.id) state.current.projectId = null;
          for (const g of generations.values()) if (g.c.projectId === p.id) g.c.projectId = null;
          await reloadLists().catch(storageFailed);
          keepOwnCopies();
          broadcast({ type: 'lists' });
          go('#/');
        }
      }))));
}
