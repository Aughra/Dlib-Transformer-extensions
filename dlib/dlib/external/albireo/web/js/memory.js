// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La mémoire des discussions (fork pour macOS : sortie d'app.js) : les faits sur l'utilisateur que le modèle
// d'arrière-plan tire des questions, leur réécriture dans les limites de sa taille et en français, le bail qui laisse
// un seul onglet la mettre à jour, et l'identifiant de cet onglet (tabId).

import * as store from './store.js';
import { generations } from './composer.js';
import { keepStart, memoryLimitWords } from './context.js';
import { deleted } from './conversations.js';
import { toast } from './dom.js';
import { language, t, tn } from './i18n.js';
import { fold, languageNames, textLanguage } from './lists.js';
import { saveSetting } from './preferences.js';
import { defaults, state } from './state.js';
import { backgroundContext, backgroundRates, backgroundText } from './status.js';
import { broadcast } from './tabs.js';

/*
    La mémoire lit les questions des discussions qu'elle n'a pas encore lues, chaque discussion à
    partir de sa marque, en un seul passage une fois qu'elles sont assez nombreuses
    (memoryBatchQuestions questions ou memoryBatchChars caractères) et que l'utilisateur est
    inactif depuis memoryIdle : le modèle n'écrit que les faits nouveaux, qui rejoignent la mémoire
    sans réécriture, et la mémoire n'est réécrite en entier que quand elle dépasse sa taille.  Un
    seul onglet la met à jour à la fois, grâce à un bail qui expire : un onglet fermé au milieu
    d'une mise à jour ne retient rien, et les questions qu'il lisait sont relues par la mise à jour
    suivante, puisque les marques ne bougent qu'une fois la mémoire enregistrée.  La mémoire est
    écrite dans la langue de l'interface, quelles que soient les langues des discussions, et une
    mise à jour y réécrit d'abord les faits écrits dans une autre langue, après un changement de
    langue entre autres.
*/
const memoryBatchQuestions = 3;
const memoryBatchChars = 1200;
const memoryIdle = 20000;
const memoryLeaseMs = 180000;
// Le nombre maximal de questions que lit une mise à jour : ce que le modèle d'arrière-plan lit en memoryReadSeconds, la
// moitié de son contexte de travail au plus ; les autres attendent la mise à jour suivante.
const memoryReadSeconds = 30;
export const tabId = store.uid();
let memoryTimer = null;
export let memoryRunning = false;
export let memoryAbort = null;   // la mise à jour en cours, qu'une nouvelle question arrête

export function rememberLater() {
  if (!state.settings.memoryEnabled) return;
  clearTimeout(memoryTimer);
  memoryTimer = setTimeout(() => {
    memoryTimer = null;
    updateMemory().catch(() => {});
  }, memoryIdle);
}

function sensitiveRule(sensitive) {
  return sensitive ? 'Information the user shares about themselves may be kept, sensitive or not. '
    : 'Leave out sensitive information: health, finances, passwords, keys and identifiers, political, religious '
      + 'or philosophical opinions, sexual orientation, private matters, and information about other people. ';
}

/*
    La langue dans laquelle la mémoire est écrite, nommée en anglais pour le modèle.  C'est celle de
    l'interface, quelles que soient les langues des discussions dont la mémoire apprend.
*/
export function memoryLanguage() {
  return languageNames[language()] || 'English';
}

/*
    Les deux faits de l'exemple du delta dans chaque langue de l'interface, pour un message en
    français (en anglais pour une interface en français), pour que l'exemple montre un fait dans la
    langue de l'interface à côté des mots de l'utilisateur dans sa propre langue.  Un fait est une
    phrase courte dont le sujet, l'utilisateur, est omis.
*/
const memoryExamples = {
  fr: ['Travaille comme infirmière à Lyon.', 'Préfère les réponses courtes.']
};

/*
    Les faits nouveaux qu'apprennent les questions, qui rejoignent la mémoire, chacun suivi des mots
    de l'utilisateur qui le disent, ce que la mise à jour vérifie dans les questions qu'elle a lues
    (groundedFacts).  La mémoire arrive numérotée, et un fait qui en change un donne son numéro,
    pour qu'il le remplace.  Les exemples montrent un message avec deux faits, et une demande, qui
    n'en apprend aucun.
*/
function memoryDeltaInstructions(words, sensitive) {
  const code = 'fr';
  const [a, b] = memoryExamples[code];
  const english = code === 'fr';
  const message = english ? 'I am a nurse in Lyon and I prefer short answers. What does triage mean?'
    : 'Je suis infirmière à Lyon et je préfère les réponses courtes. Que veut dire triage ?';
  const [qa, qb] = english ? ['I am a nurse in Lyon', 'I prefer short answers']
    : ['Je suis infirmière à Lyon', 'je préfère les réponses courtes'];
  const request = english ? 'Write a Python program that computes the integral of x squared between 0 and 1.'
    : 'Crée un programme Python pour calculer l\'intégrale de x au carré entre 0 et 1.';
  return 'You keep a short memory about the user for the future discussions of an assistant. From the new messages '
    + 'of the user, write the lasting facts about the user that the memory does not hold yet, those that will help '
    + 'in other discussions: the role, the profession and the field of the user, the organization they work for, '
    + 'their projects and the goals of these projects, the languages, tools, frameworks, systems and computers they '
    + 'use, their level of expertise, their habits of work, and the way they want to be answered (language, length, '
    + 'format, style), with their standing instructions ("always...", "never..."). A fact says who the user is, what '
    + 'the user does, uses or wants over time, as the user says it about themselves, even inside a question ("As a '
    + 'teacher, how do I..."). A request of the user (write, create, explain, translate or compute something) and the '
    + 'topic of a question are not facts about the user: leave them out, whatever their words. Leave out what serves '
    + 'one discussion only, the content of documents, and what the memory already holds, even in other words. '
    + sensitiveRule(sensitive)
    + 'Write each new fact on a line of its own: "- ", the fact as a short sentence in ' + memoryLanguage()
    + ' about the user, without naming the user, " | ", then the statement of the user that says it, copied '
    + 'exactly as the user wrote it, in its language, between double quotes; a request of the user states no fact. '
    + 'The facts of the memory are numbered: a new fact that changes one of them, a new role or another preference, '
    + 'starts with its number between brackets, "- [3] ", and replaces it. A fact the user did not state in so many '
    + 'words is left out. The facts take at most ' + words + ' words in all. For example, for the message "'
    + message + '":\n'
    + '- ' + a + ' | "' + qa + '"\n'
    + '- ' + b + ' | "' + qb + '"\n'
    + 'For the message "' + request + '": NONE\n'
    + 'When there is no new fact, answer NONE.';
}

/*
    Les mots d'un texte tels que la mémoire compare une citation de l'utilisateur avec les
    questions : en minuscules, une seule sorte d'apostrophe, sans guillemets doubles, les espaces
    réduites à une seule.
*/
function evidenceText(text) {
  return String(text || '').toLowerCase().replace(/[\u2018\u2019\u201a\u201b\u2032`\u00b4]/g, '\'')
    .replace(/["\u201c\u201d\u201e\u201f\u00ab\u00bb\u2033]/g, '').replace(/\s+/g, ' ').trim();
}

/*
    Les premiers mots des demandes, en français, repliés (fold) : les verbes des demandes fréquentes à l'impératif, et
    les débuts d'une demande polie.  Une affirmation de l'utilisateur qui commence par l'un d'eux demande une tâche, ce
    qui ne dit rien de durable sur l'utilisateur.
*/
const requestOpeners = new Set(('ecris ecrivez cree creez fais faites genere generez calcule calculez explique '
  + 'expliquez decris decrivez donne donnez montre montrez liste listez resume resumez traduis traduisez convertis '
  + 'convertissez corrige corrigez reecris reecrivez redige redigez dessine dessinez trouve trouvez cherche cherchez '
  + 'dis dites aide aidez implemente implementez ajoute ajoutez supprime supprimez verifie verifiez resous resolvez '
  + 'analysez comparez preparez concois concevez developpe developpez optimisez proposez suggere suggerez completez '
  + 'simplifie simplifiez ameliore ameliorez peux pourrais pouvez pourriez').split(' '));

// Les pronoms qui suivent un impératif après un trait d'union en français ("écris-moi", "donne-le-moi").
const requestPronouns = new Set(['moi', 'toi', 'nous', 'vous', 'le', 'la', 'les', 'lui', 'leur', 'en', 'y']);

/*
    Vrai quand une proposition demande une tâche : son premier mot, après « s'il te plaît » ou « s'il vous plaît », est
    un début de demande, seul ou avant les pronoms d'un impératif français.
*/
function asksForTask(clause) {
  const words = fold(clause).replace(/[^\p{L}\s'-]+/gu, ' ').trim().split(/\s+/);
  let k = 0;
  if (words[0] === 's\'il' && (words[1] === 'te' || words[1] === 'vous')) k = 3;
  const w = words[k] || '';
  if (requestOpeners.has(w)) return true;
  const parts = w.split('-');
  return parts.length > 1 && requestOpeners.has(parts[0]) && parts.slice(1).every((x) => requestPronouns.has(x));
}

/*
    Les mots d'une consigne durable de l'utilisateur, repliés, dans les langues de l'interface : une demande qui en
    contient un ("Please always answer in French") dit comment l'utilisateur veut qu'on lui réponde désormais.
*/
const standingWords = new RegExp('\\b(always|never|from now on|toujours|jamais|desormais|dorenavant|a l\'avenir|immer|nie'
  + '|niemals|ab jetzt|kunftig|sempre|mai|d\'ora in poi|siempre|nunca|a partir de ahora|de ahora en adelante)\\b');

/*
    Les débuts d'une proposition dans laquelle l'utilisateur dit ce qu'il est ou fait, repliés : un rôle ("as a
    teacher") ou un sujet à la première personne avec un verbe d'état ou de travail ("I work", "je suis", "ich arbeite",
    "sono", "trabajo").  Une proposition qui commence par l'un d'eux affirme un fait sur l'utilisateur même à
    l'intérieur d'une question ("As a teacher, how do I...?") ; les autres parties d'une question n'en appuient aucun.
*/
const statementOpeners = new RegExp('^(as an? |en tant qu|in qualita di |als |i am |i\'m |i work |i use |i develop '
  + '|i teach |i study |i manage |i lead |je suis |je travaille |j\'utilise |je developpe |j\'enseigne |j\'etudie '
  + '|je dirige |ich bin |ich arbeite |ich nutze |ich verwende |ich entwickle |ich unterrichte |ich studiere |sono '
  + '|lavoro |insegno |studio |soy |trabajo |utilizo |desarrollo |enseno |estudio )');

/*
    Si les mots d'une citation, normalisés et repliés, apparaissent dans les questions lues,
    normalisées et repliées elles aussi, avec 3 mots ou 12 caractères au moins dans une partie de
    phrase qui ne demande rien.  Les parties se terminent aux signes qui terminent une phrase, aux
    deux-points et aux points-virgules, et une partie qui se termine par un point d'interrogation
    n'appuie aucun fait, comme dans "One line: what is RAII?", sauf si la citation commence par dire
    ce que l'utilisateur est ou fait (statementOpeners : "As a C++ developer, how do I...?").  Une
    citation dont la proposition commence par une demande ("Write a program that...") n'appuie
    aucun fait non plus (asksForTask), sauf si elle contient une consigne durable
    ("Always answer in French").
*/
function statedIn(quote, where) {
  const at = quote ? where.indexOf(quote) : -1;
  if (at < 0) return false;
  let clause = at;
  while (clause > 0 && !/[.!?:;,\u2026]/.test(where[clause - 1])) --clause;
  const said = where.slice(clause, at + quote.length);
  if (asksForTask(said) && !standingWords.test(said)) return false;
  const personal = statementOpeners.test(quote);
  const end = at + quote.length;
  for (const s of where.matchAll(/[^.!?:;\u2026]+[.!?:;\u2026]*/g)) {
    const from = Math.max(at, s.index);
    const to = Math.min(end, s.index + s[0].length);
    if (from >= to || (/\?\s*$/.test(s[0]) && !personal)) continue;
    const part = where.slice(from, to).replace(/[.!?;:,\u2026]+$/, '').trim();
    if (part.split(' ').filter(Boolean).length >= 3 || part.length >= 12) return true;
  }
  return false;
}

// Les morceaux d'une citation que le modèle a raccourcie par des points de suspension, chacun dans les questions, dans
// leur ordre.
function quotePieces(quote, where) {
  const pieces = quote.split(/\s*(?:\u2026|\.\.\.)\s*/).map((x) => x.trim()).filter(Boolean);
  let from = 0;
  for (const piece of pieces) {
    const at = where.indexOf(piece, from);
    if (at < 0) return null;
    from = at + piece.length;
  }
  return pieces;
}

/*
    Les faits nouveaux d'un delta qu'appuient les questions lues par la mise à jour.  Chaque ligne
    contient un fait, " | " et les mots de l'utilisateur qui le disent, qui doivent se trouver dans
    une affirmation des questions lues (statedIn), les morceaux d'une citation raccourcie par des
    points de suspension dans leur ordre.  Les mots de l'utilisateur appuient un seul fait, et un
    fait suivant qui les cite de nouveau est omis.  Un fait peut commencer par le numéro du fait de
    la mémoire qu'il remplace, "[3]".  Les faits ne gardent aucune citation, et deviennent des
    phrases.  Renvoie {facts: [{text, replaces}], proposed, discarded}, replaces étant le numéro du
    fait remplacé, 0 pour aucun.
*/
function groundedFacts(text, read, sensitive) {
  const where = fold(evidenceText(read));
  const cited = new Set();
  const facts = [];
  let proposed = 0;
  let discarded = 0;
  for (const raw of String(text || '').split('\n')) {
    const line = raw.trim();
    if (!/^[-*\u2022]\s*\S/.test(line)) continue;
    ++proposed;
    const m = /^[-*\u2022]\s*(?:\[(\d+)\]\s*)?(.+?)\s*\|\s*(.+)$/.exec(line);
    const quote = m ? fold(evidenceText(m[3])).replace(/[.!?;:,\u2026]+$/, '').trim() : '';
    const pieces = quote ? quotePieces(quote, where) : null;
    const longest = (pieces || []).reduce((x, y) => (y.length > x.length ? y : x), '');
    if (!m || !m[2].trim() || !pieces || cited.has(quote) || !statedIn(longest, where)) {
      ++discarded;
      continue;
    }
    cited.add(quote);
    const replaces = m[1] ? Number(m[1]) : 0;
    for (const x of memorySentences(sentenceOf(m[2]), sensitive)) facts.push({ text: x, replaces });
  }
  return { facts, proposed, discarded };
}

// La mémoire réécrite dans les limites de sa taille, quand les faits nouveaux la lui ont fait dépasser.
function memoryCompactInstructions(words, sensitive) {
  return 'You keep a short memory about the user for the future discussions of an assistant. Rewrite it in at most '
    + words + ' words, as short sentences one after the other in a single paragraph, without a list, a bullet or a '
    + 'line break: merge the sentences that say the same thing, keep the most lasting facts about the user, those '
    + 'that help in any discussion first (role, field, projects, tools, standing preferences), the newest last, and '
    + 'leave out the requests and the tasks of past discussions. ' + sensitiveRule(sensitive)
    + 'Write every sentence in ' + memoryLanguage() + ', the sentences written in another language included. '
    + 'Answer with the paragraph alone.';
}

// La mémoire réécrite dans la langue de l'interface, phrase par phrase, chacune avec son numéro.
function memoryTranslateInstructions() {
  return 'You keep a short memory about the user for the future discussions of an assistant. Its sentences are '
    + 'numbered. Rewrite each of them in ' + memoryLanguage() + ', on a line of its own that starts with its number '
    + 'between brackets, as "[1] ": translate the sentences written in another language, and keep the sentences '
    + 'already written in ' + memoryLanguage() + ' as they are. Answer with the numbered lines alone.';
}

/*
    Les phrases des lignes de la mémoire une fois traduites : la ligne de chaque numéro dans la réponse du modèle, la
    phrase telle qu'elle était pour un numéro qui manque à la réponse.  Renvoie null quand la réponse n'en a traduit
    aucune.
*/
function translatedSentences(lines, answer) {
  const out = lines.slice();
  let found = 0;
  for (const raw of String(answer || '').split('\n')) {
    const m = /^\s*\[(\d+)\]\s*(.+)$/.exec(raw);
    const k = m ? Number(m[1]) - 1 : -1;
    if (k < 0 || k >= lines.length) continue;
    const sentence = memorySentences(m[2], true).join(' ');
    if (!sentence) continue;
    out[k] = sentence;
    ++found;
  }
  return found ? out : null;
}

// Vrai quand une phrase d'une mémoire est écrite dans une autre langue que celle de l'interface.
function memoryForeign(text) {
  const target = language();
  return memorySentences(text, true).some((x) => {
    const found = textLanguage(x, true);
    return !!found && found !== target;
  });
}

// Les identifiants de connexion, les clés et les identifiants de paiement, que la mémoire ne garde que quand les
// réglages gardent les informations sensibles, quoi que le modèle ait écrit.
const secretPatterns = [
  /\b(mots? de passe|passwords?|passw[o\u00f6]rt|kennwort|contrase[n\u00f1]as?|parol[ae] d'ordine|passphrase|passcode)\b/i,
  /\b(pin|cvv|cvc|iban|bic|swift)\b/i,
  /\b(api[ _-]?keys?|cl[e\u00e9]s? d'api|access[ _-]?tokens?|bearer|secret[ _-]?keys?|private[ _-]?keys?|cl[e\u00e9]s? priv[e\u00e9]es?)\b/i,
  /\b(?:\d[ -]?){13,19}\b/,
  /[A-Za-z0-9+\/=_-]{32,}/
];

// Un fait sous forme de phrase de la mémoire : sans les marques d'une liste ni guillemets autour, sa première lettre en
// majuscule, avec son point final.
function sentenceOf(text) {
  let x = String(text || '').replace(/\s+/g, ' ').trim().replace(/^[-*\u2022]\s*/, '')
    .replace(/^["'\u00ab\u201c\u201e]+|["'\u00bb\u201d]+$/g, '').trim();
  if (!x) return '';
  x = x.charAt(0).toLocaleUpperCase() + x.slice(1);
  return /[.!?\u2026]$/.test(x) ? x : x + '.';
}

/*
    Les phrases d'une mémoire : les lignes des notes écrites sous forme de liste par une version antérieure, chacune
    devenant une phrase, ou les phrases d'un paragraphe, sans les secrets sauf si les réglages gardent les informations
    sensibles.
*/
export function memorySentences(text, sensitive) {
  const out = [];
  for (const line of String(text || '').split('\n').map((l) => l.trim()).filter(Boolean)) {
    if (/^[-*\u2022]\s*\S/.test(line)) out.push(sentenceOf(line));
    else for (const x of line.split(/(?<=[.!?\u2026])\s+(?=\S)/)) out.push(sentenceOf(x));
  }
  return out.filter((x) => x && (sensitive || !secretPatterns.some((re) => re.test(x))));
}

const wordCount = (lines) => lines.join(' ').split(/\s+/).filter(Boolean).length;

// Les mots que peuvent prendre les faits que le modèle apprend à côté des phrases que l'utilisateur a écrites : la
// moitié de la mémoire au moins.
function learnedRoom(words, pinned) {
  return Math.max(words - wordCount(pinned), Math.floor(words / 2));
}

/*
    La mémoire telle qu'elle est gardée : ses phrases l'une après l'autre, dans la limite de words mots, les plus
    anciennes partant d'abord, sauf les phrases que l'utilisateur a écrites (pinned, un Set).
*/
export function memoryParagraph(sentences, words, pinned) {
  const list = sentences.slice();
  while (list.length > 1 && wordCount(list) > words) {
    const k = list.findIndex((x) => !(pinned && pinned.has(x)));
    if (k < 0) break;
    list.splice(k, 1);
  }
  return list.join(' ');
}

// Vrai quand une mémoire contient des notes écrites sous forme de liste, qui deviennent des phrases (memorySentences).
export function listMemory(text) {
  return /^\s*[-*\u2022]\s*\S/m.test(String(text || ''));
}

// Les mots d'une phrase, pour la comparaison de deux faits : leurs cinq premières lettres, qui
// rapprochent les formes d'un mot ("courtes", "court").
function factWords(line) {
  return new Set(line.toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^\p{L}\p{N}]+/gu, ' ')
    .split(' ').filter((w) => w.length > 2).map((w) => w.slice(0, 5)));
}

// Vrai quand deux phrases disent presque la même chose : la plupart de leurs mots sont communs.
function sameFact(a, b) {
  const x = factWords(a);
  const y = factWords(b);
  if (!x.size || !y.size) return false;
  let shared = 0;
  for (const w of x) if (y.has(w)) ++shared;
  return shared / (x.size + y.size - shared) >= 0.7;
}

/*
    Les questions que la mémoire n'a pas encore lues, chaque discussion à partir de sa marque, les discussions changées
    le moins récemment d'abord : [{id, to, asked: [{text, to}]}], to étant la marque après la discussion ou après la
    question.
*/
function pendingMemory() {
  const items = [];
  let questions = 0;
  let chars = 0;
  const all = state.conversations.slice();
  if (state.current && !state.current.incognito && !all.includes(state.current)) all.push(state.current);
  all.sort((a, b) => (a.updated || 0) - (b.updated || 0));
  for (const c of all) {
    if (c.incognito || !Array.isArray(c.messages) || c.messages.length <= (c.memoryMark || 0)) continue;
    const asked = [];
    for (let i = c.memoryMark || 0; i < c.messages.length; ++i) {
      const m = c.messages[i];
      if (m.role === 'user' && (m.content || '').trim()) asked.push({ text: m.content, to: i + 1 });
    }
    items.push({ id: c.id, to: c.messages.length, asked });
    questions += asked.length;
    chars += asked.reduce((n, a) => n + a.text.length, 0);
  }
  return { items, questions, chars };
}

/*
    Les questions que lit une mise à jour, les plus anciennes d'abord, dans la limite de chars caractères : le texte que
    lit le modèle, et les éléments à marquer comme lus, le dernier d'entre eux jusqu'à la dernière question qu'il a
    prise.
*/
function memoryReading(items, chars) {
  const parts = [];
  const marks = [];
  let used = 0;
  for (const item of items) {
    let taken = 0;
    for (const a of item.asked) {
      const part = 'User: ' + keepStart(a.text, 1500);
      if (parts.length && used + part.length + 2 > chars) break;
      parts.push(part);
      used += part.length + 2;
      ++taken;
    }
    // Une discussion lue en entier est marquée à sa fin, une discussion lue en partie après sa dernière question lue.
    if (taken === item.asked.length) {
      marks.push({ id: item.id, to: item.to });
      continue;
    }
    if (taken) marks.push({ id: item.id, to: item.asked[taken - 1].to });
    break;
  }
  return { read: parts.join('\n\n'), marks };
}

// Le bail de cet onglet sur la mémoire : false quand un autre onglet le détient et qu'il n'a pas expiré.
function takeMemoryLease() {
  return store.claimLease('memoryLease', tabId, Date.now() + memoryLeaseMs);
}

export function releaseMemoryLease() {
  return store.releaseLease('memoryLease', tabId);
}

// Les copies d'une discussion dans cet onglet : celle qui est affichée et celle des listes.
function copiesOf(id) {
  const out = state.conversations.filter((x) => x.id === id);
  if (state.current && state.current.id === id && !out.includes(state.current)) out.push(state.current);
  return out;
}

/*
    Les discussions lues jusqu'aux questions que la mémoire a prises.  La marque change sur la
    discussion enregistrée, relue depuis la base, pour que les messages qu'un autre onglet a
    enregistrés restent, et sur les copies de cet onglet, pour que leur prochain enregistrement la
    garde.
*/
async function markRead(items) {
  for (const item of items) {
    if (deleted.has(item.id)) continue;
    const mark = (x) => {
      const to = Math.min(item.to, Array.isArray(x.messages) ? x.messages.length : 0);
      if ((x.memoryMark || 0) >= to) return false;
      x.memoryMark = to;
      return true;
    };
    await store.updateConversations(mark, item.id).catch(() => {});
    copiesOf(item.id).forEach(mark);
    broadcast({ type: 'conversation', id: item.id });
  }
}

// La mémoire et ses réglages tels qu'ils sont enregistrés, qu'un autre onglet a pu changer.
async function storedMemory() {
  const s = Object.assign({}, defaults, await store.getSettings());
  return {
    enabled: !!s.memoryEnabled, sensitive: !!s.memorySensitive, memory: s.memory || '', language: s.memoryLanguage,
    pinned: s.memoryPinned || ''
  };
}

// Les copies des discussions dans cet onglet, après une remise à zéro de la mémoire dans cet onglet ou dans un autre.
export function forgetMemory() {
  const all = state.conversations.slice();
  if (state.current && !all.includes(state.current)) all.push(state.current);
  for (const c of all) {
    delete c.memory;
    c.memoryMark = c.messages.length;
  }
  clearTimeout(memoryTimer);
  memoryTimer = null;
}

/*
    Met à jour la mémoire avec les questions qu'elle n'a pas encore lues, quand elles sont assez
    nombreuses ou que force est vrai : le modèle d'arrière-plan écrit les faits nouveaux, qui
    rejoignent la mémoire sauf si elle les contient déjà, puis la mémoire est réécrite dans les
    limites de sa taille quand elle l'a dépassée, ou perd ses phrases les plus anciennes quand la
    réécriture échoue.  La mémoire est gardée sous forme de courtes phrases l'une après l'autre.
    Une notification dit combien de faits l'ont rejointe.
*/
async function updateMemory(force) {
  if (memoryRunning || !state.settings.memoryEnabled || state.incognito) return;
  const pending = pendingMemory();
  const enough = pending.items.length > 0
    && (force || pending.questions >= memoryBatchQuestions || pending.chars >= memoryBatchChars);
  // Une mémoire écrite dans une autre langue que celle de l'interface y est réécrite, avec ou sans
  // nouvelles questions à lire.
  const memory = state.settings.memory || '';
  const foreign = !!memory && (memoryForeign(memory)
    || (!!state.settings.memoryLanguage && state.settings.memoryLanguage !== language()));
  if (!enough && !foreign) return;
  // Les réponses en cours passent d'abord.
  if (generations.size) {
    rememberLater();
    return;
  }
  memoryRunning = true;
  memoryAbort = new AbortController();
  const signal = memoryAbort.signal;
  let leased = false;
  try {
    leased = await takeMemoryLease();
    if (!leased) return;
    // La mémoire telle qu'elle est enregistrée : un autre onglet a pu la remettre à zéro ou la modifier.
    const stored = await storedMemory();
    if (!stored.enabled) return;
    const sensitive = stored.sensitive;
    const context = backgroundContext();
    const words = memoryLimitWords(context);
    const current = stored.memory;
    const target = language();
    // Les phrases que l'utilisateur a écrites dans la mémoire, qui restent ; le bail, renouvelé avant chaque requête au
    // modèle.
    let pinned = memorySentences(stored.pinned, true);
    const renew = async () => {
      if (!(await takeMemoryLease())) throw new DOMException('The memory is updated by another tab.', 'AbortError');
    };
    // Vrai quand la mémoire a été remise à zéro, modifiée ou désactivée entre-temps, dans cet onglet
    // ou dans un autre, telle qu'elle est enregistrée maintenant : la mise à jour est alors abandonnée.
    const changed = async () => {
      const now = await storedMemory();
      return !now.enabled || now.memory !== current;
    };
    // Les notes écrites sous forme de liste par une version antérieure deviennent des phrases, et sont enregistrées
    // comme telles.
    let lines = memorySentences(current, true);
    let rewritten = false;
    /*
        Les faits écrits dans une autre langue sont d'abord réécrits dans la langue de
        l'interface, pour que les faits nouveaux soient comparés avec eux dans une seule langue.
        Une réécriture qui a perdu des faits est écartée, et la mise à jour suivante réessaie.
    */
    if (lines.length && (memoryForeign(current) || (!!stored.language && stored.language !== target))) {
      await renew();
      const r = await backgroundText(memoryTranslateInstructions(),
        '<memory>\n' + lines.map((x, k) => '[' + (k + 1) + '] ' + x).join('\n') + '\n</memory>\n\nRewrite every '
          + 'sentence in ' + memoryLanguage() + '.', Math.ceil(wordCount(lines) * 2.5) + 8 * lines.length + 32,
        signal).catch((e) => {
        if (e.name === 'AbortError') throw e;
        return null;
      });
      if (await changed()) return;
      // Une traduction garde chaque phrase, les phrases que l'utilisateur a écrites prenant leurs traductions.
      const translated = r ? translatedSentences(lines, r.text) : null;
      if (translated) {
        const kept = new Set(pinned);
        pinned = translated.filter((x, k) => kept.has(lines[k]));
        lines = translated;
        rewritten = true;
      }
    }
    let added = 0;
    let moved = 0;
    let marks = [];
    if (enough && pending.chars >= 40) {
      // Ce que l'utilisateur a écrit, les plus anciens d'abord, dans la limite de la lecture de la mise à jour : la
      // mémoire porte sur l'utilisateur, et les réponses la rempliraient de ce que l'assistant a dit.
      const rates = backgroundRates();
      const reading = memoryReading(pending.items,
        Math.floor(Math.min(0.5 * context, Math.max(2048, memoryReadSeconds * rates.read)) * 3.6));
      const read = reading.read;
      marks = reading.marks;
      await renew();
      const r = await backgroundText(memoryDeltaInstructions(words, sensitive),
        '<memory>\n' + (lines.map((x, k) => '[' + (k + 1) + '] ' + x).join('\n') || '(empty)')
          + '\n</memory>\n\n<messages>\n' + read
          + '\n</messages>\n\nWrite the new facts in ' + memoryLanguage() + ', each with the words of the user.',
        Math.ceil(words * 3.2) + 32, signal);
      if (await changed()) return;
      const delta = groundedFacts(r.text, read, sensitive);
      console.info('Albireo: memory: ' + delta.proposed + ' facts proposed, ' + delta.discarded
        + ' left out, unsupported by the words of the user');
      /*
          Un fait qui en remplace un de la mémoire prend sa place, un fait redit passe à la fin avec les nouveaux, pour
          que les faits que les questions n'apportent plus partent d'abord ; les phrases que l'utilisateur a écrites
          restent telles qu'il les a écrites, sauf si les mots de l'utilisateur changent l'une d'elles.
      */
      const kept = new Set(pinned);
      const recent = [];
      const slots = lines.slice();
      for (const fact of delta.facts) {
        const k = fact.replaces - 1;
        if (k >= 0 && k < slots.length && slots[k] != null) {
          if (slots[k] !== fact.text) ++added;
          kept.delete(slots[k]);
          slots[k] = null;
          recent.push(fact.text);
          continue;
        }
        const same = slots.findIndex((l) => l != null && sameFact(l, fact.text));
        if (same >= 0) {
          if (!kept.has(slots[same])) {
            recent.push(slots[same]);
            slots[same] = null;
            ++moved;
          }
          continue;
        }
        if (recent.some((l) => sameFact(l, fact.text))) continue;
        recent.push(fact.text);
        ++added;
      }
      if (added || moved) {
        lines = slots.filter((x) => x != null).concat(recent);
        pinned = pinned.filter((x) => kept.has(x));
      }
      if (added && wordCount(lines) > wordCount(pinned) + learnedRoom(words, pinned)) {
        // Les faits que le modèle a appris sont réécrits dans la place que leur laissent les phrases de l'utilisateur.
        const learned = lines.filter((x) => !pinned.includes(x));
        const room = learnedRoom(words, pinned);
        await renew();
        const compact = await backgroundText(memoryCompactInstructions(room, sensitive),
          '<memory>\n' + learned.join(' ') + '\n</memory>', Math.ceil(room * 2.2) + 16, signal).catch(() => null);
        if (signal.aborted) return;
        if (await changed()) return;
        const compacted = compact ? memorySentences(compact.text, sensitive) : [];
        if (compacted.length && wordCount(compacted) <= room * 1.2) lines = pinned.concat(compacted);
      }
    }
    /*
        Les faits que le modèle a appris prennent la place que leur laissent les phrases de l'utilisateur, la moitié de
        la mémoire au moins ; une mémoire que l'utilisateur a écrite, qui peut prendre deux fois sa taille, la garde
        tant qu'aucun fait ne la rejoint.
    */
    if (added || moved || rewritten || listMemory(current)) {
      const limit = wordCount(pinned) + learnedRoom(words, pinned);
      await saveSetting('memory', memoryParagraph(lines, added ? limit : Math.max(limit, words * 2), new Set(pinned)));
      await saveSetting('memoryPinned', pinned.join(' '));
      await saveSetting('memoryUpdated', Date.now());
      await saveSetting('memoryLanguage', target);
    }
    // Les questions trop courtes pour dire quoi que ce soit sont lues malgré tout.
    if (enough && pending.chars < 40) marks = pending.items;
    if (marks.length) await markRead(marks);
    if (added) toast(tn('memoryLearned', added), false, 4500);
    else if (rewritten) toast(t('memoryRewritten'), false, 4500);
  } finally {
    memoryRunning = false;
    memoryAbort = null;
    if (leased) await releaseMemoryLease().catch(() => {});
  }
}
