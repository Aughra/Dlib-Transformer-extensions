// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'application web d'Albireo : les discussions et les projets gardés dans ce navigateur, les discussions privées
    gardées en mémoire seulement, les trois fonctions du service (général, code, cybersécurité), le raisonnement
    approfondi affiché au fil de son écriture, et un mode agent dont les outils s'exécutent dans la page.

    LE CONTEXTE D'UNE QUESTION
        Le moteur garde les clés et valeurs de chaque conversation et des prompts qu'elle partage avec d'autres, et ne
        recalcule que les tokens qui diffèrent de ce qu'il a déjà lu.  Le prompt est donc construit pour que son début
        ne change jamais d'une question à la suivante : le message système ne contient que ce qui reste (la date, les
        instructions, les documents d'un projet quand ils tiennent en entier), chaque message garde, une fois envoyé,
        les extraits de documents choisis pour lui, et les échanges les plus anciens partent d'un seul coup, bien en
        dessous de la limite, plutôt qu'un par un à chaque question.  Chaque question ne coûte alors que ses propres
        tokens, quelle que soit la longueur de la conversation, et les conversations d'un même projet partagent la
        lecture de ses documents.

        Les documents d'un projet qui ne tiennent pas en entier, et les grands documents joints à un message, sont
        l'objet d'une recherche (rag.js) : les extraits les plus proches de la question s'y joignent.

    LES IMAGES
        Une question peut porter des images, pour un modèle qui les lit (images.js).  Elles passent avant son texte tant
        que la question reste dans le contexte, maxContextImages au plus pour la discussion : les images des échanges
        qu'une compression a résumés quittent le contexte avec eux et libèrent leurs places.  Les images d'un projet
        sont décrites par le modèle d'arrière-plan, et leurs descriptions sont cherchées avec ses documents ; une
        question qui en trouve une reçoit l'image avec elle.  Un modèle qui ne lit que du texte reçoit les noms des
        images.

    LES MODULES (fork pour macOS : app.js, qui faisait plus de 7 000 lignes, a été découpé en novembre 2026 ; il ne garde
    que le démarrage, init)
        state.js          l'état partagé (state, ui), les réglages par défaut et les constantes des rôles
        dom.js            la création des éléments, les formats, les messages passagers, le presse-papiers
        dialogs.js        les boîtes de dialogue, les menus et les panneaux d'explications
        preferences.js    le thème, la langue et l'enregistrement d'un réglage
        tabs.js           les messages entre onglets et la relecture de la base qu'ils demandent
        status.js         l'état du service et des modèles, le modèle de chaque rôle et ce qu'il lit
        lists.js          les listes du panneau latéral et la recherche
        sidebar.js        le panneau latéral
        conversations.js  l'enregistrement d'une discussion et les actions de son menu
        navigation.js     les adresses de la page et le passage d'une discussion à une autre
        drafts.js         les brouillons de la zone de saisie
        files.js          les fichiers et les images joints, la visionneuse, le glisser-déposer
        projects.js       les projets et leur page
        indexing.js       l'indexation des documents et les descriptions des images des projets
        context.js        le contexte que reçoit le modèle et ses parts
        thread.js         la zone principale et le fil d'une discussion
        composer.js       la zone de saisie et l'envoi d'une question
        generation.js     l'écriture d'une réponse, la compression des échanges, les étapes de l'agent
        background.js     les titres, la compression en arrière-plan, la lecture d'avance
        memory.js         la mémoire des discussions
        settings.js       les paramètres, les notes de version et les licences
        layout.js         la mise en page
    Les modules de l'original restent : api.js, store.js, i18n.js, icons.js, markdown.js, documents.js, rag.js,
    tools.js, sandbox.js, web.js, images.js, models.js et service.js (ces deux derniers propres au fork).
    Les modules s'importent les uns les autres, parfois en cercle ; aucun n'appelle un autre pendant son propre
    chargement, seulement dans ses fonctions, ce qui rend ces cercles sans danger pour les modules ES.
*/

import * as api from './api.js';
import * as rag from './rag.js';
import * as store from './store.js';
import { generations } from './composer.js';
import { memoryWords } from './context.js';
import { storageFailed, toast } from './dom.js';
import { pruneDrafts, saveDraft } from './drafts.js';
import { guardFileDrops } from './files.js';
import { t } from './i18n.js';
import { indexProjects, pruneImages, pruneIndex } from './indexing.js';
import { buildLayout } from './layout.js';
import { reloadLists } from './lists.js';
import { listMemory, memoryParagraph, memoryRunning, memorySentences, releaseMemoryLease, tabId } from './memory.js';
import { route } from './navigation.js';
import { applyLanguage, applyTheme } from './preferences.js';
import { requestPersistence } from './settings.js';
import { defaults, state } from './state.js';
import { refreshStatus } from './status.js';
import { broadcast } from './tabs.js';

async function init() {
  // Un échec que le code ne traite pas est journalisé, et un refus de la base est signalé.
  window.addEventListener('unhandledrejection', (e) => {
    const r = e.reason;
    if (r && r.name === 'AbortError') {
      e.preventDefault();
      return;
    }
    console.warn('Albireo: an operation failed', r);
    if (r && (r.name === 'QuotaExceededError' || r.name === 'UnknownError' || r.name === 'InvalidStateError')) {
      storageFailed(r);
    }
  });
  let blocked = false;
  try {
    await store.open();
    state.settings = Object.assign({}, defaults, await store.getSettings());
    // Les notes de la mémoire écrites sous forme de liste par une version antérieure deviennent des phrases l'une après
    // l'autre.
    if (listMemory(state.settings.memory)) {
      state.settings.memory = memoryParagraph(memorySentences(state.settings.memory, true), memoryWords * 2);
      await store.setSetting('memory', state.settings.memory);
    }
    // Le navigateur est invité à garder la base, sans attendre sa réponse.
    requestPersistence();
  } catch (e) {
    // Sans IndexedDB (une fenêtre de navigation privée de certains navigateurs), ou tant qu'un onglet
    // d'une version antérieure la garde ouverte, l'application garde ses données en mémoire, comme
    // dans une discussion privée.
    console.warn('Albireo: the database of the browser is unavailable', e);
    blocked = !!e && e.name === 'BlockedError';
    state.incognito = true;
  }
  // La langue d'Albireo, quand cette interface la suit, avant les premiers textes.
  state.status = await api.status().catch(() => null);
  applyLanguage();
  applyTheme();
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', applyTheme);
  // Les documents qui ont des passages, listés avant la lecture des listes (pruneIndex), qui ne
  // les élague qu'une fois les listes lues en entier.
  const indexed = state.incognito ? [] : await store.chunkDocuments().catch(() => []);
  const pictures = state.incognito ? [] : await store.imageKeys().catch(() => []);
  if (await reloadLists().then(() => true, () => false)) {
    pruneIndex(indexed).catch(() => {});
    pruneImages(pictures).catch(() => {});
    if (!state.incognito) pruneDrafts();
  }
  buildLayout();
  guardFileDrops();
  if (blocked) toast(t('dbBlocked'), true, 30000);
  window.addEventListener('hashchange', route);
  // Le texte en cours de saisie reste quand la page est quittée, rechargée ou cachée.
  window.addEventListener('pagehide', () => saveDraft(true));
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') saveDraft(true);
  });
  await route();
  /*
      La première lecture de l'état, une fois le service prêt maintenant ou plus tard (pollStatus),
      planifie les questions qu'une visite précédente a laissées à la mémoire, les titres qu'elle a
      laissés à écrire et les descriptions des images.
  */
  await refreshStatus();
  setInterval(refreshStatus, 4000);
  // Les documents des projets sans passages sont indexés, par leurs mots sans modèle d'embedding ;
  // avec un, l'état a déjà lancé leur indexation.
  if (!rag.embeddingModel()) indexProjects();
  // Un onglet qui se ferme rend le bail de la mémoire et dit qu'il ne répond plus.
  window.addEventListener('pagehide', () => {
    if (memoryRunning) releaseMemoryLease().catch(() => {});
    for (const g of generations.values()) {
      if (!g.c.incognito) broadcast({ type: 'busy', id: g.c.id, tab: tabId, busy: false });
    }
  });
}

init();
