// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// L'écriture d'une réponse (fork pour macOS : sortie d'app.js) : la compression des échanges anciens pour tenir dans
// le contexte de travail (fitContext), l'affichage de la réponse au fil de son écriture, et un tour complet avec le
// modèle, ses appels d'outils et ses étapes d'agent (runTurn).

import * as api from './api.js';
import * as store from './store.js';
import * as tools from './tools.js';
import * as web from './web.js';
import { cancelWarmup, compactLater, errorText, nameConversation, needsTitle, warmBeginning } from './background.js';
import { generationOf, generations, prepareQuestion, searchDocumentsOf, unread } from './composer.js';
import { answerReserve, apiMessages, compact, documentsOf, keptShare, measureImages, requestMedia, systemPrompt,
  tokenEstimate } from './context.js';
import { deleted, persist } from './conversations.js';
import { toast } from './dom.js';
import { contextImages } from './files.js';
import { language, t } from './i18n.js';
import { knowledgeMode, queueIndex } from './indexing.js';
import { projectById, searchConversations } from './lists.js';
import { rememberLater } from './memory.js';
import { maxAgentSteps, state, ui } from './state.js';
import { modelName, roleCapabilities, roleModel, toolSet } from './status.js';
import { announceBusy, refreshViews } from './tabs.js';
import { fillAssistant, lockThread, renderMain, scrollToEnd } from './thread.js';

// Donne une compression à la discussion c.
export function applyCompaction(c, r) {
  c.summary = r.summary;
  c.contextStart = r.to;
  indexLeftDocuments(c, r.to);
}

/*
    Indexe pour la recherche les documents lus en entier des questions d'avant le message to de c, qui ont quitté le
    contexte, pour que les extraits des questions suivantes puissent en venir.
*/
function indexLeftDocuments(c, to) {
  for (const m of c.messages.slice(0, to)) {
    if (!m || m.role !== 'user') continue;
    for (const a of m.attachments || []) {
      if (a.mode === 'search' || a.searchable || !a.text) continue;
      a.searchable = true;
      queueIndex(c.id, a, !c.incognito);
    }
  }
}

/*
    La discussion telle qu'elle est maintenant, pour le travail qui se termine en arrière-plan : celle à laquelle cet
    onglet répond, affichée ou non, sinon celle qui est affichée, sinon celle qui est enregistrée.  Null quand elle a
    été supprimée.
*/
export async function currentVersion(c) {
  if (deleted.has(c.id)) return null;
  const g = generations.get(c.id);
  if (g) return g.c;
  if (state.current && state.current.id === c.id) return state.current;
  if (c.incognito) return c;
  return (await store.getConversation(c.id)) || null;
}

// Donne une compression des échanges à la discussion c, quand les échanges qu'elle a compressés sont encore ceux de c.
export function settleCompaction(c, r) {
  const last = c.messages[r.to - 1];
  if (!last || last.id !== r.last || (c.contextStart || 0) > r.to) return false;
  applyCompaction(c, r);
  return true;
}

/*
    Les compressions des échanges en cours : discussion -> {done, abort, always}, done une promesse qui n'est jamais
    rejetée, abort l'AbortController de ses requêtes, always vrai pour un résumé réécrit après une suppression
    (rewriteSummary), que chaque question attend.
*/
export const compactions = new Map();

// Arrête la compression en cours des échanges d'une discussion, quand la discussion part.
export function cancelCompaction(id) {
  const job = compactions.get(id);
  if (job) job.abort.abort();
}

// Attend promise, qui n'est jamais rejetée, ou lance une AbortError dès que signal arrête la réponse.
function untilDone(promise, signal) {
  if (!signal) return promise;
  return new Promise((resolve, reject) => {
    const stop = () => reject(new DOMException('The answer was stopped.', 'AbortError'));
    if (signal.aborted) {
      stop();
      return;
    }
    signal.addEventListener('abort', stop, { once: true });
    promise.then((v) => {
      signal.removeEventListener('abort', stop);
      resolve(v);
    });
  });
}

/*
    Le début d'une discussion qui ramène la conversation construite par build à target tokens au plus, ses tokens donnés
    par estimate : la première question après le début actuel pour laquelle elle tient, la dernière question au plus.
    Renvoie -1 quand aucune question ne peut partir.
*/
export function nextStart(c, build, estimate, target) {
  const from = c.contextStart || 0;
  const users = [];
  for (let i = from + 1; i < c.messages.length; ++i) if (c.messages[i].role === 'user') users.push(i);
  // La question qui reçoit une réponse reste : la dernière des messages, ou celle d'avant la
  // réponse en cours.
  const lastUser = c.messages.map((m) => m.role).lastIndexOf('user');
  const candidates = users.filter((i) => i <= lastUser);
  if (!candidates.length) return -1;
  const saved = c.contextStart;
  let next = candidates[candidates.length - 1];
  for (const i of candidates) {
    c.contextStart = i;
    if (estimate(build()) <= target) {
      next = i;
      break;
    }
  }
  c.contextStart = saved;
  return next;
}

/*
    Les tokens qu'une conversation garde une fois ses échanges les plus anciens partis : le message système et les
    outils (fixes), et keptShare de la place qu'ils laissent aux échanges.
*/
export function keptTokens(fixed, room) {
  return fixed + keptShare * Math.max(0, room - fixed);
}

/*
    Ajuste la conversation au contexte du modèle.  Quand elle ne tient plus, les échanges les plus
    anciens partent d'un seul coup, jusqu'à ce qu'ils prennent keptShare de la place que leur
    laissent le message système et les outils, et le point où elle commence désormais reste avec la
    conversation : les questions suivantes partent de ce même point, pour que le moteur réutilise
    ce qu'il a déjà lu, et la coupe suivante ne vient qu'après plusieurs questions.  Les échanges
    qui partent sont compressés dans le résumé de la discussion, que le modèle reçoit avec la
    première question qui reste (compact) ; les échanges partent sans lui quand le modèle ne peut
    pas l'écrire.  Quand la dernière question seule ne tient pas, les textes les plus anciens
    perdent leur début, à la fin d'une phrase, puis les étapes les plus anciennes de la réponse en
    cours d'écriture partent (shortenOldest).  Le message système et la dernière question restent
    toujours.  onCompact signale le démarrage d'une compression, et model nomme le modèle pour
    lequel les tokens sont comptés.  Renvoie {messages, trimmed, compacted, context, tokens}.

    La réponse de l'agent s'ajuste à son contexte avant chacune de ses étapes, avec les coupes de
    ses étapes précédentes (kept, qu'une compression vide), pour que le début du prompt reste d'une
    étape à la suivante ; une fois ses étapes coupées, un quart de la place des échanges est libéré
    au-delà du nécessaire, pour que les étapes suivantes tiennent sans nouvelle coupe.

    Une compression que l'arrière-plan a lancée après une réponse précédente (compactLater) n'est
    attendue que quand la conversation ne tient pas sans elle, et une compression terminée pendant
    l'écriture de la réponse (generations) s'applique alors ; un résumé en cours de réécriture après
    une suppression (rewriteSummary) est attendu dans tous les cas.  signal met fin à ces attentes
    aussi.
*/
async function fitContext(c, build, toolDefs, reserve, signal, onCompact, model, kept) {
  let trimmed = false;
  let compacted = false;
  const waited = new Set();
  const wait = async (job) => {
    waited.add(job);
    if (onCompact) onCompact();
    await untilDone(job.done, signal);
  };
  const first = compactions.get(c.id);
  if (first && first.always) await wait(first);
  const cuts = kept || { summary: null, texts: new Map(), steps: 0 };
  // Une compression déplace le début de la conversation, et les textes sont de nouveau mesurés en entier.
  const restart = () => {
    cuts.summary = null;
    cuts.texts.clear();
    cuts.steps = 0;
  };
  // Les étapes de la réponse en cours d'écriture que contient la requête, le dernier message de c contenant celle qui
  // est demandée.
  const answer = c.messages[c.messages.length - 1];
  let spare = answer && answer.role === 'assistant' && (answer.steps || []).length > 1 ? 0.25 : 0;
  const counted = model || 'albireo-' + c.role;
  for (let attempt = 0; attempt < 12; ++attempt) {
    const list = build(cuts);
    // Les images que le service n'a pas encore mesurées pour ce modèle, une fois.
    if (!attempt) await measureImages(counted, list);
    let r;
    try {
      r = await api.count(counted, list, toolDefs);
    } catch (e) {
      return { messages: list, trimmed, compacted, context: 0 };
    }
    if (!r || typeof r.tokens !== 'number' || !r.context) return { messages: list, trimmed, compacted, context: 0 };
    const room = r.context - Math.min(reserve, Math.floor(r.context / 2));
    if (r.tokens <= room) return { messages: list, trimmed, compacted, context: r.context, tokens: r.tokens };
    // La conversation ne tient pas : la compression terminée ou en cours déplace son début, et elle est mesurée de
    // nouveau.
    const g = generations.get(c.id);
    if (g && g.compaction) {
      const done = g.compaction;
      g.compaction = null;
      if (settleCompaction(c, done)) {
        compacted = true;
        restart();
        continue;
      }
    }
    const job = compactions.get(c.id);
    if (job && !waited.has(job)) {
      await wait(job);
      continue;
    }
    // Les tokens d'une liste de messages : ses images par leurs tokens, ses textes d'après leurs caractères mis à
    // l'échelle de la mesure.  Le message système et les outils sont mesurés à part, puisqu'ils restent.
    const estimate = tokenEstimate(list, r.tokens, counted);
    const scale = estimate.scale;
    const base = await api.count(counted, list.slice(0, 1), toolDefs).catch(() => null);
    if (signal && signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
    const fixed = base && typeof base.tokens === 'number' ? base.tokens : estimate.of(list.slice(0, 1));
    const next = nextStart(c, () => build(cuts), estimate.of, keptTokens(fixed, room));
    if (next > (c.contextStart || 0)) {
      try {
        if (onCompact) onCompact();
        cancelWarmup(c.id);
        applyCompaction(c, await compact(c, c.contextStart || 0, next, room, scale, signal));
        compacted = true;
      } catch (e) {
        if (e.name === 'AbortError') throw e;
        c.contextStart = next;
        indexLeftDocuments(c, next);
        trimmed = true;
      }
      restart();
      continue;
    }
    // Plus rien ne peut partir en entier : les textes les plus anciens perdent leur début, avec la place de réserve une
    // fois.
    const excess = Math.ceil((r.tokens - room + spare * Math.max(0, room - fixed)) / scale) + 200;
    spare = 0;
    if (!shortenOldest(c, cuts, excess)) {
      return { messages: list, trimmed, compacted, context: r.context, tokens: r.tokens, overflow: true };
    }
    trimmed = true;
  }
  return { messages: build(cuts), trimmed, compacted, context: 0 };
}

/*
    Retire les caractères en trop des textes que reçoit le modèle, les plus anciens d'abord : le
    résumé, les documents et les textes des questions précédentes avec les résultats des outils de
    leurs réponses, puis le raisonnement et les résultats des outils des étapes de la réponse en
    cours d'écriture, le dernier message de c, sauf ceux de sa dernière étape, puis les documents
    et le texte de la question à laquelle elle répond, et les résultats de la dernière étape de la
    réponse en dernier.  cuts contient les caractères que chacun d'eux garde : un texte garde sa fin
    et un résultat son début, 200 caractères au moins, et le raisonnement d'une étape part en
    entier.  Quand les textes d'avant la question n'ont plus rien à donner, les étapes les plus
    anciennes de la réponse partent en entier, la dernière étape de la requête restant
    (cuts.steps).  Renvoie false quand il ne reste rien à retirer.
*/
function shortenOldest(c, cuts, excess) {
  const messages = c.messages.slice(c.contextStart || 0);
  const lastUser = messages.map((m) => m.role).lastIndexOf('user');
  const end = messages[messages.length - 1];
  const answer = end && end.role === 'assistant' && messages.length - 1 > lastUser ? end : null;
  // La requête contient les étapes de la réponse d'avant la dernière, qui est celle qui est demandée.
  const latest = answer && answer.steps.length > 1 ? answer.steps[answer.steps.length - 2] : null;
  const early = [];
  const reasoning = [];
  const results = [];
  const question = [];
  const last = [];
  if (c.summary && c.summary.text && (c.contextStart || 0) > 0) early.push({ key: 'summary', text: c.summary.text });
  messages.forEach((m, i) => {
    if (m.role === 'user') {
      const list = i === lastUser ? question : early;
      for (const a of m.attachments || []) if (a.mode !== 'search') list.push({ key: a.id, text: a.text || '' });
      list.push({ key: m.id, text: m.content || '' });
      return;
    }
    for (const s of m.steps || []) {
      if (m === answer && s.reasoning) reasoning.push({ key: s, text: s.reasoning, whole: true });
      for (const tc of s.toolCalls || []) {
        if (tc.result == null) continue;
        const list = m !== answer ? early : s === latest ? last : results;
        list.push({ key: tc, text: tc.result });
      }
    }
  });
  const keptOf = (key, text) => {
    const kept = key === 'summary' ? cuts.summary : cuts.texts.get(key);
    return kept == null ? text.length : kept;
  };
  const take = (list) => {
    for (const x of list) {
      if (excess <= 0) return;
      const length = keptOf(x.key, x.text);
      // Un texte garde 200 caractères au moins : les dernières phrases d'une question, le début d'un résultat.
      const n = x.whole ? length : Math.min(excess, length - Math.min(length, 200));
      if (n <= 0) continue;
      if (x.key === 'summary') cuts.summary = length - n;
      else cuts.texts.set(x.key, length - n);
      excess -= n;
    }
  };
  take(early);
  take(reasoning);
  take(results);
  if (excess > 0 && answer) {
    const steps = answer.steps.length - 1;
    while (excess > 0 && cuts.steps < steps - 1) {
      const s = answer.steps[cuts.steps];
      let n = 60 + (s.content || '').length + (s.reasoning ? keptOf(s, s.reasoning) : 0);
      for (const tc of s.toolCalls || []) {
        if (tc.result != null) n += 80 + (tc.arguments || '').length + keptOf(tc, tc.result);
      }
      excess -= n;
      cuts.steps += 1;
    }
  }
  take(question);
  take(last);
  return excess <= 0;
}

/*
    La réponse en cours d'écriture dans la discussion affichée est redessinée à l'image suivante, au
    plus une fois toutes les renderInterval millisecondes, et moins souvent quand un dessin prend du
    temps, pour qu'une longue réponse laisse la page réactive.  Une réponse écrite en arrière-plan
    est dessinée en entier quand sa discussion est de nouveau affichée.
*/
const renderInterval = 100;
let renderSpacing = renderInterval;
let renderTimer = 0;
let renderFrame = 0;
let renderedAt = 0;

function scheduleRender(c) {
  if (state.view !== 'chat' || state.current !== c || renderTimer || renderFrame) return;
  const draw = () => {
    renderFrame = 0;
    const g = generationOf(state.current);
    if (!g || !g.turn) return;
    const t0 = performance.now();
    renderedAt = t0;
    drawLive(g.c, g.turn);
    renderSpacing = Math.min(1000, Math.max(renderInterval, 4 * (performance.now() - t0)));
  };
  const wait = renderSpacing - (performance.now() - renderedAt);
  if (wait > 0) {
    renderTimer = setTimeout(() => {
      renderTimer = 0;
      renderFrame = requestAnimationFrame(draw);
    }, wait);
  } else {
    renderFrame = requestAnimationFrame(draw);
  }
}

/*
    Dessine la réponse m de c en cours d'écriture, tant que c est affichée.  Les positions des
    défilements sont lues avant que le dessin change la page et écrites après, pour que la page
    soit mise en page une seule fois.
*/
function drawLive(c, m) {
  const el = answerElement(c, m);
  if (!el) return;
  const bubble = el.querySelector('.bubble');
  const thought = bubble.querySelector('.thinking.live .thought');
  const follow = !thought || thought.scrollHeight - thought.scrollTop - thought.clientHeight < 40;
  fillAssistant(bubble, m, true);
  const now = bubble.querySelector('.thinking.live .thought');
  if (now && follow) now.scrollTop = now.scrollHeight;
  scrollToEnd(false);
}

// Abandonne le dessin que scheduleRender a prévu, une fois la réponse dessinée en entier.
function cancelRender() {
  clearTimeout(renderTimer);
  cancelAnimationFrame(renderFrame);
  renderTimer = 0;
  renderFrame = 0;
}

// L'élément du message m de la discussion c, tant que c est la discussion affichée.
function answerElement(c, m) {
  if (state.view !== 'chat' || state.current !== c || !ui.thread || !ui.thread.isConnected) return null;
  const index = c.messages.indexOf(m);
  const el = index >= 0 ? ui.thread.children[index] : null;
  return el && el.dataset.index === String(index) && el.dataset.id === String(m.id || '') ? el : null;
}

/*
    Répond à la dernière question de c ; abort, issu de beginTurn, arrête la réponse.  La réponse
    est dessinée tant que c est la discussion affichée, continue en arrière-plan pendant qu'une
    autre est affichée, et est enregistrée avec c à sa fin ; une réponse qui se termine pendant que
    c n'est pas affichée marque c dans le panneau latéral jusqu'à ce qu'elle soit ouverte.
*/
export async function runTurn(c, abort, turn) {
  const g = generationOf(c);
  // La fonction, le raisonnement et l'agent de la réponse restent ceux de son début.
  const role = c.role;
  const model = roleModel(role);
  turn.model = model || '';
  const signal = abort.signal;
  const caps = roleCapabilities(role);
  const reasoning = !!(c.reasoning && caps.reasoning);
  // Les outils de l'agent et du web.  Les appels d'une étape s'exécutent tant que l'un d'eux est activé, et un appel
  // d'un outil que la réponse ne propose pas est refusé.
  const set = toolSet(c, caps);
  const toolDefs = set.definitions;
  const useTools = !!toolDefs;
  const offered = new Set((toolDefs || []).map((d) => d.function.name));
  try {
    if (state.current === c && !answerElement(c, turn)) renderMain();
    else scheduleRender(c);
    if (signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
    if (!model) throw new Error(t('noModel'));
    // Les images du contexte sous forme d'URL de données pour un modèle qui les lit ; un modèle qui ne lit que du texte
    // reçoit leurs noms, ce que la discussion signale une fois.
    const media = await requestMedia(c, role);
    if (!media.read && contextImages(c) && !c.imagesNamed) {
      c.imagesNamed = true;
      turn.imagesNamed = true;
      toast(t('imagesNamed', { model: modelName(model) }), false, 9000);
    }
    // Les coupes des textes que partagent les étapes de la réponse (fitContext).
    const cuts = { summary: null, texts: new Map(), steps: 0 };
    for (let step = 0; step < maxAgentSteps; ++step) {
      const s = { reasoning: '', content: '', toolCalls: [] };
      turn.steps.push(s);
      const system = systemPrompt(c, caps, set);
      // La conversation, avec les étapes de cette réponse déjà faites (celles de l'agent).
      const build = (x) => apiMessages(Object.assign({}, c, {
        messages: c.messages.slice(0, -1).concat([Object.assign({}, turn, { steps: turn.steps.slice(0, -1) })])
      }), system, x, media);
      const fitted = await fitContext(c, build, toolDefs, answerReserve(caps, reasoning), signal, () => {
        turn.compacting = true;
        scheduleRender(c);
      }, 'albireo-' + role, cuts);
      turn.compacting = false;
      /*
          Une question qui ne tient pas à côté des documents du projet lus en entier les fait chercher, dans cette
          discussion désormais, ses extraits sont retrouvés, et l'étape recommence.
      */
      const project = c.projectId ? projectById(c.projectId) : null;
      if (fitted.overflow && !c.knowledgeSearch && knowledgeMode(project, caps, c) === 'whole') {
        c.knowledgeSearch = true;
        const asked = c.messages.filter((x) => x.role === 'user');
        if (asked.length) await prepareQuestion(c, asked[asked.length - 1], signal);
        // Les coupes de l'ajustement qui n'a pas tenu partent : les textes sont de nouveau mesurés en entier.
        cuts.summary = null;
        cuts.texts.clear();
        cuts.steps = 0;
        turn.steps.pop();
        --step;
        continue;
      }
      if (fitted.trimmed) turn.trimmed = true;
      if (fitted.compacted) turn.compacted = true;
      if (fitted.context) {
        turn.contextTokens = fitted.context;
        turn.promptTokens = fitted.tokens;
      }
      const body = { model: 'albireo-' + role, messages: fitted.messages, reasoning };
      if (toolDefs) body.tools = toolDefs;
      if (state.settings.temperature != null) body.temperature = state.settings.temperature;
      if (state.settings.topP != null) body.top_p = state.settings.topP;
      if (state.settings.topK != null) body.top_k = state.settings.topK;
      if (state.settings.maxTokens > 0) body.max_tokens = state.settings.maxTokens;
      let finish = null;
      // Une compression des échanges qui se termine désormais attend la fin de la réponse, dont les étapes gardent le
      // début de leur prompt.
      if (g) g.answering = true;
      for await (const ev of api.streamChat(body, signal)) {
        if (ev.type === 'progress') {
          turn.progress = ev.data;
          scheduleRender(c);
          continue;
        }
        const chunk = ev.data;
        if (chunk.usage) {
          turn.usage = turn.usage ? {
            prompt_tokens: chunk.usage.prompt_tokens, completion_tokens: (turn.usage.completion_tokens || 0) + chunk.usage.completion_tokens
          } : chunk.usage;
          turn.timings = chunk.timings || turn.timings;
        }
        // Une réponse que le moteur a arrêtée parce qu'elle se répétait.
        if (chunk.repetition) turn.repetition = true;
        const choice = chunk.choices && chunk.choices[0];
        if (!choice) continue;
        const d = choice.delta || {};
        if (d.reasoning_content) {
          if (!s.reasoningStart) s.reasoningStart = Date.now();
          s.reasoning += d.reasoning_content;
        }
        if (d.content) {
          if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
          s.content += d.content;
        }
        for (const tc of d.tool_calls || []) {
          if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
          const existing = tc.index != null ? s.toolCalls.find((x) => x.index === tc.index) : null;
          if (existing) existing.arguments += (tc.function && tc.function.arguments) || '';
          else s.toolCalls.push({
            index: tc.index, id: tc.id || ('call_' + store.uid()), name: tc.function ? tc.function.name : '',
            arguments: (tc.function && tc.function.arguments) || '', status: 'pending'
          });
        }
        if (choice.finish_reason) finish = choice.finish_reason;
        turn.progress = null;
        scheduleRender(c);
      }
      if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
      delete s.reasoningStart;
      if (!(useTools && s.toolCalls.length && (finish === 'tool_calls' || finish === 'stop'))) break;
      // Les outils de l'étape s'exécutent, chacun chronométré, puis le modèle continue avec leurs résultats.
      for (const tc of s.toolCalls) {
        if (signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
        tc.status = 'running';
        tc.started = Date.now();
        scheduleRender(c);
        let r;
        if (!offered.has(tc.name)) {
          r = { ok: false, text: 'The tool ' + tc.name + ' is not available in this answer.' };
        } else if (web.isTool(tc.name)) {
          // fetch_page lit les adresses de la discussion telle qu'elle est quand l'appel s'exécute, y compris les
          // résultats des appels de cette réponse.
          r = await web.run(tc.name, tc.arguments, {
            id: c.id, language: language(), signal, allowed: () => web.allowedAddresses(c)
          });
        } else {
          r = await tools.run(tc.name, tc.arguments, {
            documents: documentsOf(c),
            searchDocuments: (query, limit) => searchDocumentsOf(c, query, limit),
            searchDiscussions: async (q) => searchConversations(q, 6).filter((x) => x.conversation.id !== c.id)
              .map((x) => ({
                title: x.conversation.title, date: new Date(x.conversation.updated).toLocaleDateString(),
                excerpt: x.excerpt
              })),
            createFile: (name, content) => ({ name, content }),
            signal
          });
        }
        tc.ms = Date.now() - tc.started;
        delete tc.started;
        // Un appel que l'arrêt de la réponse a terminé garde ce qu'il a renvoyé, comme arrêté.
        tc.status = signal.aborted ? 'stopped' : 'done';
        tc.ok = r.ok;
        tc.result = r.text;
        if (r.file) tc.file = r.file;
        // Les adresses qu'un outil web ajoute à la discussion, que fetch_page peut lire dès lors.
        if (r.web) tc.web = r.web;
        delete tc.index;
        scheduleRender(c);
      }
      if (step === maxAgentSteps - 1) turn.agentLimit = true;
    }
  } catch (e) {
    if (e.name === 'AbortError') turn.cancelled = true;
    else turn.error = errorText(e);
  } finally {
    if (state.current === c) cancelRender();
    // Les appels qu'une réponse arrêtée ou en échec n'a pas exécutés restent arrêtés, sans résultat.
    for (const s of turn.steps) {
      delete s.reasoningStart;
      for (const tc of s.toolCalls || []) {
        if (tc.status === 'done') continue;
        tc.status = 'stopped';
        delete tc.started;
        delete tc.index;
      }
    }
    turn.progress = null;
    turn.compacting = false;
    turn.preparing = false;
    if (g && generations.get(c.id) === g) generations.delete(c.id);
    // Une compression des échanges terminée pendant l'écriture de la réponse s'applique maintenant.
    const settled = !!(g && g.compaction && settleCompaction(c, g.compaction));
    announceBusy(c, false);
    c.updated = Date.now();
    await persist(c);
    if (settled) warmBeginning(c);
    if (!(state.view === 'chat' && state.current && state.current.id === c.id) && !c.incognito && !deleted.has(c.id)) {
      unread.add(c.id);
    }
    if (state.current === c) renderMain();
    else lockThread();
    refreshViews();
  }
  if (deleted.has(c.id)) return;
  // Une discussion encore nommée d'après sa première question reçoit un titre du modèle, quelle
  // que soit la fin de sa réponse, puisque le titre suit la question.
  if (needsTitle(c)) nameConversation(c);
  if (!turn.error && !turn.cancelled) {
    compactLater(c);
    if (!c.incognito) rememberLater();
  }
}
