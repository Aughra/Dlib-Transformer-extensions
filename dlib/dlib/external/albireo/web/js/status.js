// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// L'état du service et des modèles vu par la page (fork pour macOS : sorti d'app.js) : la lecture régulière de
// /albireo/v1/status, le modèle de chaque rôle et ce qu'il lit, le modèle d'arrière-plan et ses vitesses, les outils
// et l'accès au Web disponibles, et la ligne d'état du panneau latéral.

import * as api from './api.js';
import * as rag from './rag.js';
import * as tools from './tools.js';
import * as web from './web.js';
import { describePending, indexProjects,
  showIndexState } from './indexing.js';
import { dialog } from './dialogs.js';
import { documentAccept } from './documents.js';
import { expireBusy,
  hintText, languageChanged, namePending, refreshViews, serviceReady } from './app.js';
import { lockThread } from './thread.js';
import { fill, formatDecimal, h } from './dom.js';
import { imageTokenCache, readRate, workingContext, writeRate } from './context.js';
import { language, t } from './i18n.js';
import { modelSheetDialog } from './models.js';
import { rememberLater } from './memory.js';
import { settingsDialog } from './settings.js';
import { state, ui } from './state.js';

// État du service et modèles.
export function roleModel(role) {
  const s = state.status;
  if (!s || !s.active) return null;
  return s.active[role] || s.active.general || null;
}

function modelInfo(id) {
  if (!state.catalog || !id) return null;
  const same = (m) => m.id === id;
  return (state.catalog.models || []).find(same) || (state.catalog.embedding_models || []).find(same) || null;
}

export function modelName(id) {
  const m = modelInfo(id);
  return m && m.name ? m.name : id;
}

export function loadedModel(id) {
  const list = (state.status && state.status.loaded_models) || [];
  return list.find((m) => m.id === id) || null;
}

// Ce que lit un modèle : "text", et "image" ou "audio" pour les modèles qui les lisent, comme le
// disent le moteur ou le catalogue.
function modelInputs(id) {
  const loaded = loadedModel(id);
  if (loaded && Array.isArray(loaded.inputs) && loaded.inputs.length) return loaded.inputs;
  const m = modelInfo(id);
  return m && Array.isArray(m.inputs) && m.inputs.length ? m.inputs : ['text'];
}

// Ce que lit le modèle d'un rôle ; les pièces jointes suivent.
export function roleInputs(role) {
  return modelInputs(roleModel(role));
}

export function readsImages(role) {
  return roleInputs(role).includes('image');
}

export function inputsLabel(inputs) {
  return inputs.map((i) => t('input_' + i)).join(', ');
}

/*
    Le sélecteur de fichiers d'un rôle : les documents, lus comme du texte, et les médias que son
    modèle lit, les images tant que room, les places qui leur restent, n'est pas 0.
*/
export function acceptFor(role, room) {
  const inputs = roleInputs(role);
  let accept = documentAccept();
  if (inputs.includes('image') && room !== 0) accept += ',image/*';
  if (inputs.includes('audio')) accept += ',audio/*';
  return accept;
}

/*
    Les capacités du modèle d'une fonction : reasoning, reasoningAlways pour un modèle entraîné à raisonner avant chaque
    réponse (sans le raisonnement approfondi, le moteur termine aussitôt son raisonnement), tools, son contexte et son
    contexte de travail (workingContext), 0 tant qu'il n'est pas chargé, et ses vitesses de lecture et d'écriture
    (readRate, writeRate).
*/
export function roleCapabilities(role) {
  const id = roleModel(role);
  const loaded = loadedModel(id);
  const read = readRate(id);
  const write = writeRate(id);
  if (loaded) {
    const context = loaded.context || 0;
    return { reasoning: loaded.reasoning !== false, reasoningAlways: loaded.reasoning_always === true,
      tools: loaded.tools !== false, context, working: context ? workingContext(id, context) : 0, read, write };
  }
  const m = modelInfo(id);
  return { reasoning: m ? m.reasoning !== false : true, reasoningAlways: !!(m && m.reasoning_always === true),
    tools: m ? m.tools !== false : true, context: 0, working: 0, read, write };
}

/*
    Si les outils de l'agent et du web peuvent accompagner les questions à un modèle aux capacités caps, reasoning
    disant si le raisonnement approfondi de la discussion est activé : un modèle capable de raisonner ne les appelle
    qu'avec son raisonnement approfondi, pour qu'il raisonne avant chaque appel ; un modèle incapable de raisonner les
    appelle au fil de sa réponse.
*/
export function toolsAvailable(caps, reasoning) {
  return !!caps.tools && (!caps.reasoning || !!reasoning);
}

/*
    Pourquoi l'accès au web ne peut pas servir une discussion dont le modèle a les capacités caps, reasoning disant si
    son raisonnement approfondi est activé, '' quand il le peut : un modèle qui n'appelle aucun outil, un modèle capable
    de raisonner sans son raisonnement approfondi, un service mis hors ligne, un accès au web que l'administrateur a
    coupé (le membre "web" de l'état), ou un état qui ne le contient pas.
*/
export function webUnavailable(caps, reasoning) {
  if (!caps.tools) return t('webNoTools');
  if (!toolsAvailable(caps, reasoning)) return t('webNeedsReasoning');
  const s = state.status;
  if (!s || !s.web || typeof s.web !== 'object') return t('webUnavailable');
  if (s.web.available) return '';
  return s.offline ? t('webOffline') : t('webOff');
}

/*
    Le fournisseur de la recherche web du service, Wikipédia, dans la langue de l'interface ; '' tant que l'accès au web
    est indisponible ou ne donne pas de recherche (web.search "none").  L'aide du mode agent et la fenêtre « À propos »
    le nomment.
*/
export function searchProvider() {
  const w = state.status && state.status.web;
  if (!w || typeof w !== 'object' || !w.available || w.search !== 'wikipedia') return '';
  return t('provider_wikipedia');
}

/*
    Les outils des requêtes de c et les parties de son message système qui les accompagnent : ceux de l'agent tant qu'il
    est activé, puis ceux du web tant que Web est activé et que le service le propose, web_search quand le service
    propose la recherche dans Wikipédia.  La liste et son ordre restent les mêmes tant que les interrupteurs et le
    fournisseur ne changent pas, pour que le début du prompt reste le même d'une question à la suivante.
*/
export function toolSet(c, caps) {
  const reasoning = !!(c.reasoning && caps.reasoning);
  const agent = !!(c.agent && toolsAvailable(caps, reasoning));
  const webOn = !!(c.web && !webUnavailable(caps, reasoning));
  const search = webOn && state.status.web.search === 'wikipedia';
  const definitions = (agent ? tools.definitions : []).concat(webOn ? web.definitions(search) : []);
  return { agent, web: webOn, search, definitions: definitions.length ? definitions : null };
}

/*
    Le travail fait en arrière-plan (titres, résumés, mémoire) va au modèle livré avec Albireo,
    que le service garde chargé, sans raisonnement ; le service le confie au modèle général quand
    il n'y en a pas.  Un modèle qui raisonne toujours voit son raisonnement terminé aussitôt par le
    moteur, hors de la réponse et de son max_tokens.
*/
export const backgroundModel = 'albireo-background';

// Le contexte de travail du modèle d'arrière-plan, et ses vitesses de lecture et d'écriture.
export function backgroundContext() {
  const s = state.status;
  const loaded = s && s.background ? loadedModel(s.background) : null;
  if (loaded && loaded.context) return workingContext(loaded.id, loaded.context);
  return roleCapabilities('general').working || 8192;
}

export function backgroundRates() {
  const s = state.status;
  const id = s && s.background && loadedModel(s.background) ? s.background : roleModel('general');
  return { read: readRate(id), write: writeRate(id) };
}

// Vrai quand le modèle d'arrière-plan lit les images, ce dont les descriptions des images des projets ont besoin.
export function backgroundReadsImages() {
  const s = state.status;
  return modelInputs((s && s.background) || roleModel('general')).includes('image');
}

// Le texte que le modèle d'arrière-plan écrit pour une consigne et un message.
export function backgroundText(system, user, maxTokens, signal) {
  return backgroundChat([{ role: 'system', content: system }, { role: 'user', content: user }], maxTokens, signal);
}

/*
    Le temps que peut prendre une requête du modèle d'arrière-plan : au-delà, la requête se termine par une
    TimeoutError, ce qui libère le travail qui attend derrière elle (les descriptions des images, la question suivante
    après une compression).
*/
export const backgroundTimeout = 10 * 60 * 1000;

/*
    Le texte que le modèle d'arrière-plan écrit après une liste de messages ; signal, quand il est donné, l'arrête
    aussi.  Sans température, le moteur échantillonne comme le conseillent les auteurs du modèle, sans la pénalité de
    présence que certains d'entre eux donnent : les titres, les résumés et les citations de la mémoire reprennent les
    mots de la discussion.
*/
export async function backgroundChat(messages, maxTokens, signal, temperature) {
  const body = { model: backgroundModel, reasoning: false, max_tokens: maxTokens, messages, presence_penalty: 0 };
  if (temperature != null) body.temperature = temperature;
  let text = '';
  let finish = null;
  const limit = api.limitedSignal(signal, backgroundTimeout);
  try {
    for await (const ev of api.streamChat(body, limit.signal)) {
      const choice = ev.type === 'chunk' && ev.data.choices && ev.data.choices[0];
      if (!choice) continue;
      if (choice.delta && choice.delta.content) text += choice.delta.content;
      if (choice.finish_reason) finish = choice.finish_reason;
    }
  } finally {
    limit.done();
  }
  return { text: text.trim(), finish };
}

// La lecture de l'état en cours, que partagent les appels faits entre-temps.
let statusPoll = null;

export function refreshStatus() {
  if (!statusPoll) statusPoll = pollStatus().finally(() => { statusPoll = null; });
  return statusPoll;
}

// Si le service était prêt à la lecture précédente de son état (serviceReady).
let serviceWasReady = false;

/*
    Lit l'état du service, avec les modèles des fonctions, que la fenêtre de la zone de
    notification peut changer, et si le service répond.  Un changement redessine aussitôt les
    affichages qui le montrent.  Quand le service devient prêt, à l'ouverture pendant qu'il démarre,
    ou après qu'il n'a pas répondu, le travail qu'une visite précédente ou un onglet fermé a laissé
    reprend : les titres laissés en attente, la mémoire, et les descriptions des images.
*/
async function pollStatus() {
  let changed = false;
  let back = false;
  try {
    const s = await api.status();
    // L'accès au web et le mode hors ligne changent le bouton Web de la zone de saisie et les outils de la question
    // suivante.
    const key = (x) => JSON.stringify([x.active, x.loaded_models, x.state, x.background, x.web, x.offline]);
    changed = !state.status || key(state.status) !== key(s);
    back = state.statusError;
    state.status = s;
    state.statusError = false;
    if (changed || !state.catalog) state.catalog = await api.models(language()).catch(() => state.catalog);
    // Les tokens des images ont été mesurés pour les modèles des fonctions tels qu'ils étaient.
    if (changed) imageTokenCache.clear();
    // Une langue choisie entre-temps dans Albireo, que cette interface suit.
    await languageChanged();
    renderHint();
    // Le modèle d'embedding de la recherche dans les documents ; les documents qui n'ont pas encore
    // reçu de vecteurs sont indexés dès qu'il est là.
    const embedder = (s.loaded_models || []).find((m) => m.embedding && s.active && m.id === s.active.embedding);
    const previous = rag.embeddingModel();
    rag.setEmbeddingModel(embedder ? { id: embedder.id, dimensions: embedder.dimensions } : null);
    if (embedder && (!previous || previous.id !== embedder.id)) indexProjects();
    // Les documents affichés avant que le modèle d'embedding soit connu, ou avec un autre, réaffichent leur état.
    if ((previous ? previous.id : null) !== (embedder ? embedder.id : null)) {
      for (const el of document.querySelectorAll('[data-index-doc]')) showIndexState(el);
    }
  } catch (e) {
    changed = !state.statusError;
    state.statusError = true;
  }
  if ((changed || back || expireBusy()) && state.current) {
    refreshViews();
    lockThread();
  } else {
    renderStatusLine();
  }
  const ready = serviceReady();
  const became = ready && !serviceWasReady;
  serviceWasReady = ready;
  if (became) {
    namePending();
    rememberLater();
  }
  if (back || changed || became) describePending();
}

export function renderStatusLine() {
  if (!ui.statusLine) return;
  const s = state.status;
  let led = 'error', text = t('serviceDown');
  if (s && !state.statusError) {
    const stats = s.engine && s.engine.stats;
    const loading = Object.values(s.models_state || {}).some((v) => v === 'loading');
    const model = roleModel(state.current ? state.current.role : 'general');
    if (s.state === 'starting') {
      led = 'busy';
      text = t('serviceStarting');
    } else if (!model) {
      led = 'error';
      text = t('noModel');
    } else if (loading) {
      led = 'busy';
      text = t('modelLoading');
    } else {
      led = stats && stats.active > 0 ? 'busy' : 'ok';
      const tps = stats ? stats.generation_tokens_per_second : 0;
      text = modelName(model) + ' · ' + computeUnit(model)
        + (tps > 0.05 ? ' · ' + t('tokensPerSecond', { n: formatDecimal(tps, 1) }) : '');
    }
  }
  const model = s && roleModel('general');
  ui.statusLine.replaceChildren(h('span', { class: 'led ' + led }), h('span', { class: 'grow', title: text }, text),
    model && modelInfo(model) ? modelHelp(model) : null);
  renderHint();
}

// Où calcule le modèle : le GPU du Mac quand ses passes y vont, le processeur sinon (état du moteur).
function computeUnit(id) {
  const stats = state.status && state.status.engine && state.status.engine.stats;
  const m = stats && (stats.models || []).find((x) => x.id === id);
  return m && m.gpu && m.gpu.state === 'on' ? 'GPU' : 'processeur';
}

// Le « ? » du modèle choisi : sa fiche, comme celles des paramètres.
function modelHelp(id) {
  return h('button', { type: 'button', class: 'svc-help', title: 'Le modèle utilisé', 'aria-label': 'Le modèle utilisé',
    onclick: () => modelSheetDialog(modelInfo(id), { h, dialog, status: () => state.status,
      openModels: () => settingsDialog('modelsTab') }) }, '?');
}

// Sous la zone de saisie : le modèle qui répond, son « ? », et l'avertissement.
function renderHint() {
  if (!ui.hint) return;
  const model = state.status && !state.statusError ? roleModel('general') : null;
  fill(ui.hint, model ? [h('span', { class: 'hint-model' }, modelName(model)), ' ', modelInfo(model) ? modelHelp(model) : null,
    ' · '] : null, hintText());
}
