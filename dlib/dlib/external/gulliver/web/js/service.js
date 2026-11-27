// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet « Service » des paramètres : chaque réglage du service, avec sa fiche normale et
    les fiches hibou / marmotte / abeille (CLAUDE.md).  Un module par groupe de réglages, chacun
    avec ses fiches (items) et leurs schémas (schemas) :

      - web/js/service_moteur.js : le moteur (GPU, cœurs, contexte, clés et valeurs, brouillons) ;
      - web/js/service_documents.js : la recherche dans les documents ;
      - web/js/service_modeles.js : les dossiers des modèles et la place gardée ;
      - web/js/service_reseau.js : l'accès du réseau de la maison et les ports ;
      - web/js/service_internet.js : hors ligne, Web, recherche sur le Web, proxy ;
      - web/js/service_mail.js : le mail des comptes ;
      - web/js/service_externe.js : le relais vers un modèle extérieur ;
      - web/js/service_commun.js : ce qu'ils partagent (portraits, dessin, formats).

    Ce fichier garde les valeurs conseillées (recommended), l'ordre des groupes (groups) et le rendu
    (serviceTab).  Le kit de dessin est réexporté d'ici pour web/js/models.js.
*/

import * as api from './api.js';
import { portraits, box, text, schema } from './service_commun.js';
import * as moteur from './service_moteur.js';
import * as documents from './service_documents.js';
import * as modeles from './service_modeles.js';
import * as reseau from './service_reseau.js';
import * as internet from './service_internet.js';
import * as mail from './service_mail.js';
import * as externe from './service_externe.js';
export { portraits, box, text, schema };

const parts = [moteur, documents, modeles, reseau, internet, mail, externe];
const items = Object.assign({}, ...parts.map((g) => g.items));
const schemas = Object.assign({}, ...parts.map((g) => g.schemas));

// ---------------------------------------------------------------------------------------------------
// Les valeurs conseillées, mesurées sur les M2 et M5 Pro (voir docs/mesures/).  Le rendu compare la
// valeur courante à celle-ci et affiche « Conseillé : X » quand elles diffèrent.

const recommended = {
  accelerator: 'auto',        // GPU : écriture 21-22 contre 18-19 tokens/s (4B), lecture x1,7 (épisodes 19, 20)
  gpu_prompt_tokens: 24,      // le GPU perd à 16 tokens, gagne dès 24 (épisode 20)
  draft_tokens: 7,            // Mac mini, 4B, 2026-11-21 : recopie 78,5 -> 245, question sur un texte +26 %, code +9 %, résumé +8 %, réponse libre +1 % (4 : 192,6 / +24 % / +9 % / +8 % / +1 %)
  gpu_prompt_context: 262144, // attention matricielle (4.11.0), 2B, mesures alternées : GPU 423-424 / 295 / 171 contre 170-177 / 115-117 / 67 à 4 096 / 8 192 / 16 384
  threads: 0,                 // tous les cœurs physiques (épisode 16) ; 18 sur le M5 Pro, meilleur que 14, 12, 10, 6 (mesure du 2026-11-19)
  spin_us: 2000,
  max_sequences: 4,
  context: 0,
  kv_mb: 0,
  kv_type: 'auto',            // q8 : moitié moins de mémoire, mêmes réponses dans les essais (Albireo 1.0.12)
  warm_history: true,
  offline: false,
  web_access: true,
  web_search: 'wikipedia',
  proxy: '',
  rag_engine: 'hybrid',   // mots + sens : le plus complet par défaut (à mesurer sur le Mac mini)
  rag_top_k: 12,
  rag_rerank: false       // tri fin optionnel : laissé éteint par défaut (gain à mesurer sur le Mac mini)
};

const groups = [
  {
    title: 'Le moteur',
    note: 'Ces réglages demandent un redémarrage du service : quelques secondes, le temps de recharger les modèles.',
    items: ['accelerator', 'gpu_prompt_tokens', 'gpu_prompt_context', 'draft_tokens', 'threads', 'spin_us', 'max_sequences',
      'context', 'kv_mb', 'kv_type', 'warm_history']
  },
  {
    title: 'Documents',
    items: ['rag_engine', 'rag_top_k', 'rag_rerank']
  },
  {
    title: 'Modèles',
    note: 'Le dossier des modèles demande un redémarrage ; la bibliothèque et la réserve s’appliquent tout de suite.',
    items: ['models_dir', 'library_dir', 'reserve_gb']
  },
  {
    title: 'Accès depuis le réseau',
    note: 'Pour ouvrir Gulliver depuis un téléphone ou une tablette de la maison.',
    items: ['lan', 'lan_port', 'port']
  },
  {
    title: 'Internet',
    note: "Ces réglages s'appliquent tout de suite, sans redémarrage.",
    items: ['offline', 'web_access', 'web_search', 'proxy']
  },
  {
    title: 'Mail des comptes',
    note: 'Pour les codes de connexion et les liens « mot de passe oublié ». S’applique tout de suite.',
    items: ['mail_from', 'mail_port', 'mail_secret', 'mail_link']
  },
  {
    title: 'Moteurs extérieurs',
    note: 'Pour envoyer une discussion à un modèle qui ne tourne pas sur le Mac. S’applique tout de suite.',
    items: ['relay']
  }
];
// ---------------------------------------------------------------------------------------------------
// Le rendu.

/*
    serviceTab(body, env) remplit body.  env : h (le constructeur d'éléments de la page), toast,
    status() (l'état du service), refreshStatus().
*/
export function serviceTab(body, env) {
  const { h, toast } = env;
  const st = env.status() || {};
  const cpu = (st.host && st.host.cpu) || {};
  const machine = { logical: cpu.threads || 8, cores: cpu.cores || 8 };
  const pending = {};
  let current = null;

  const intro = h('div', { class: 'svc-intro' },
    h('p', null, 'Les réglages du service Gulliver. Chaque ligne dit ce que le réglage change ; le ',
      h('b', null, '?'), ' ouvre sa fiche, avec trois explications : normale, ', h('b', null, 'le hibou'),
      ' (très technique) et ', h('b', null, 'la marmotte'), ' (pour un ado). ', h('b', null, "L'abeille"),
      ' dit ce qui arrive si on pousse trop ou qu’on s’en passe.'));
  const list = h('div', { class: 'svc-list' }, h('p', { class: 'help' }, 'Lecture des réglages…'));
  const bar = h('div', { class: 'svc-apply', hidden: true });
  body.append(intro, list, bar);

  const valueOf = (key) => (key in pending ? pending[key] : current[key]);

  function renderBar() {
    const keys = Object.keys(pending);
    bar.hidden = keys.length === 0;
    if (!keys.length) return;
    bar.replaceChildren(
      h('span', null, keys.length === 1 ? '1 réglage à appliquer' : keys.length + ' réglages à appliquer',
        h('small', null, ' · le service redémarre')),
      h('button', { class: 'button', type: 'button', onclick: () => { for (const k of keys) delete pending[k]; render(); } },
        'Annuler'),
      h('button', { class: 'button primary', type: 'button', onclick: apply }, 'Appliquer et redémarrer'));
  }

  async function apply() {
    const changes = Object.assign({}, pending);
    try {
      const r = await api.putJSON('/gulliver/v1/settings', changes);
      for (const k of Object.keys(changes)) delete pending[k];
      if (r.restart) await waitRestart(changes);
      await load();
    } catch (e) {
      toast(e.message, true);
    }
  }

  // Attend que le service revienne ; un nouveau port envoie la page à sa nouvelle adresse.
  async function waitRestart(changes) {
    const onLan = location.protocol === 'http:';
    const moved = onLan ? changes.lan_port : changes.port;
    if (moved && Number(moved) !== Number(location.port)) {
      toast('Le service redémarre sur le port ' + moved + '…');
      setTimeout(() => { location.href = location.protocol + '//' + location.hostname + ':' + moved + '/'; }, 4000);
      return;
    }
    if (onLan && changes.lan === false) {
      toast("L'accès réseau se ferme : cette page ne répondra plus.", true);
      return;
    }
    bar.hidden = false;
    bar.replaceChildren(h('span', null, 'Redémarrage du service…'));
    await new Promise((r) => setTimeout(r, 1500));
    for (let i = 0; i < 120; ++i) {
      try {
        const s = await api.status();
        if (s && s.engine && s.engine.running) break;
      } catch (e) {
        // Pas encore revenu.
      }
      await new Promise((r) => setTimeout(r, 1000));
    }
    toast('Service redémarré.');
    if (env.refreshStatus) env.refreshStatus();
  }

  async function setLive(key, value) {
    try {
      await api.putJSON('/gulliver/v1/settings', { [key]: value });
      current[key] = value;
      toast('Appliqué.');
    } catch (e) {
      toast(e.message, true);
    }
    render();
  }

  function change(key, value) {
    const item = items[key];
    if (!item.restart) return setLive(key, value);
    if (value === current[key]) delete pending[key];
    else pending[key] = value;
    render();
  }

  function control(key) {
    const item = items[key], c = item.control, v = valueOf(key);
    if (c.kind === 'switch') {
      const on = key === 'accelerator' ? v !== 'none' : v === c.on;
      return h('button', {
        class: 'svc-switch' + (on ? ' on' : ''), type: 'button', role: 'switch', 'aria-checked': String(on),
        'aria-label': item.title, onclick: () => change(key, on ? c.off : c.on)
      }, h('span'));
    }
    if (c.kind === 'range') {
      const values = c.values(machine);
      let index = values.indexOf(Number(v));
      if (index < 0) index = values.reduce((b, x, i) => Math.abs(x - v) < Math.abs(values[b] - v) ? i : b, 0);
      const out = h('output', null, c.label(values[index], machine));
      const input = h('input', { type: 'range', min: 0, max: values.length - 1, step: 1, value: index, 'aria-label': item.title });
      input.addEventListener('input', () => { out.textContent = c.label(values[Number(input.value)], machine); });
      input.addEventListener('change', () => change(key, values[Number(input.value)]));
      return h('div', { class: 'svc-range' }, input, out);
    }
    if (c.kind === 'choice') {
      return h('div', { class: 'svc-choice', role: 'radiogroup', 'aria-label': item.title },
        c.options.map(([value, label]) => h('button', {
          type: 'button', role: 'radio', 'aria-checked': String(v === value), class: v === value ? 'on' : '',
          onclick: () => change(key, value)
        }, label)));
    }
    if (c.kind === 'relay') {
      const r = current.relay || {};
      return h('span', { class: 'svc-badge' + (r.enabled ? ' changed' : '') }, r.enabled ? 'allumé' : 'coupé');
    }
    if (c.kind === 'number' || c.kind === 'text') {
      const input = h('input', {
        type: c.kind === 'number' ? 'number' : c.secret ? 'password' : 'text', value: v == null ? '' : v, min: c.min, max: c.max,
        placeholder: c.placeholder, 'aria-label': item.title, spellcheck: 'false', autocomplete: 'off'
      });
      input.addEventListener('change', () => change(key, c.kind === 'number' ? Number(input.value) : input.value.trim()));
      return h('div', { class: 'svc-input' }, input);
    }
    return null;
  }

  function sheet(item, key) {
    const pros = h('div', { class: 'svc-pc pros' }, h('h5', null, 'Avantages'), h('ul', null, item.pros.map((p) => h('li', null, p))));
    const cons = h('div', { class: 'svc-pc cons' }, h('h5', null, 'Inconvénients'), h('ul', null, item.cons.map((p) => h('li', null, p))));
    const voice = (who, title, words) => h('section', { class: 'svc-voice ' + who },
      h('div', { class: 'svc-portrait', html: portraits[who] }),
      h('div', null, h('h5', null, title), h('p', null, words)));
    return h('div', { class: 'svc-detail', hidden: true },
      h('figure', { class: 'svc-schema', html: schemas[key] }),
      h('p', { class: 'svc-principle' }, h('b', null, 'Principe : '), item.principle),
      h('div', { class: 'svc-pcs' }, pros, cons),
      h('section', { class: 'svc-normal' }, h('h5', null, 'Explication'), h('p', null, item.normal)),
      voice('owl', 'Le hibou, version très technique', item.owl),
      voice('marmot', 'La marmotte, pour un ado', item.marmot),
      voice('bee', 'L’abeille : ce qui arrive si on pousse trop, ou qu’on s’en passe', item.bee),
      h('button', { type: 'button', class: 'svc-collapse', onclick: (e) => toggle(e.target.closest('.svc-card'), false) },
        'Réduire ⌃'));
  }

  function toggle(card, open) {
    const detail = card.querySelector('.svc-detail'), help = card.querySelector('.svc-help');
    const show = open == null ? detail.hidden : open;
    detail.hidden = !show;
    help.setAttribute('aria-expanded', String(show));
    card.classList.toggle('open', show);
  }

  function card(key, item) {
    const help = h('button', { type: 'button', class: 'svc-help', 'aria-expanded': 'false',
      'aria-label': 'Explication de « ' + item.title + ' »', title: item.principle,
      onclick: (e) => toggle(e.target.closest('.svc-card')) }, '?');
    const badge = item.restart === undefined ? null : h('span', { class: 'svc-badge' + (item.restart ? ' restart' : '') },
      item.restart ? 'redémarrage' : 'en direct');
    const changed = key in pending ? h('span', { class: 'svc-badge changed' }, 'modifié') : null;
    const el = h('div', { class: 'svc-card', 'data-key': key },
      h('div', { class: 'svc-head' },
        h('div', { class: 'svc-title' }, h('span', null, item.title), help, badge, changed),
        control(key)),
      h('p', { class: 'svc-effect' }, item.effect),
      advice(key),
      key === 'lan' ? lanAddresses() : null,
      key === 'reserve_gb' ? reserveBox() : null,
      key === 'relay' ? relayBox() : null,
      h('div', { class: 'svc-bubble', role: 'tooltip' }, item.principle),
      sheet(item, key));
    return el;
  }

  // La valeur conseillée d'un réglage, quand la valeur choisie en diffère.
  function advice(key) {
    if (!(key in recommended) || same(valueOf(key), recommended[key], key)) return null;
    return h('p', { class: 'svc-advice' }, 'Conseillé : ' + describe(key, recommended[key]));
  }

  function same(a, b, key) {
    if (key === 'accelerator') return (a === 'none') === (b === 'none');
    return a === b || (a == null && b === '') || Number(a) === Number(b) && typeof b === 'number';
  }

  function describe(key, v) {
    const c = items[key].control;
    if (c.kind === 'switch') return (key === 'accelerator' ? v !== 'none' : v === c.on) ? 'allumé' : 'coupé';
    if (c.kind === 'range') return c.label(v, machine);
    if (c.kind === 'choice') return (c.options.find((o) => o[0] === v) || [v, v])[1];
    return v === '' ? 'vide' : String(v);
  }

  // Envoie des réglages d'un coup (réglages conseillés, sauvegarde), puis attend le service s'il redémarre.
  async function sendAll(changes, what) {
    if (!Object.keys(changes).length) return toast('Rien à changer : ' + what + ' sont déjà en place.');
    try {
      const r = await api.putJSON('/gulliver/v1/settings', changes);
      for (const k of Object.keys(changes)) delete pending[k];
      if (r.restart) await waitRestart(changes);
      else toast('Appliqué.');
      await load();
    } catch (e) {
      toast(e.message, true);
    }
  }

  function differences() {
    const out = {};
    for (const [k, v] of Object.entries(recommended)) if (!same(current[k], v, k)) out[k] = v;
    return out;
  }

  // Les réglages conseillés et les sauvegardes, en tête de l'onglet.
  function tools() {
    const diff = differences();
    const n = Object.keys(diff).length;
    const reset = h('button', { class: 'button', type: 'button', disabled: n === 0, onclick: async () => {
      const list = Object.entries(diff).map(([k, v]) => items[k].title + ' : ' + describe(k, v)).join('\n');
      if (!window.confirm('Revenir aux réglages conseillés ?\n\n' + list)) return;
      await sendAll(diff, 'les réglages conseillés');
    } }, 'Revenir aux réglages conseillés');
    const label = h('input', { type: 'text', placeholder: 'Libellé, par exemple « avant essai GPU »', maxlength: 120,
      'aria-label': 'Libellé de la sauvegarde' });
    const save = h('button', { class: 'button', type: 'button', onclick: async () => {
      const name = label.value.trim();
      if (!name) return toast('Donne un libellé à la sauvegarde.', true);
      try {
        await api.postJSON('/gulliver/v1/settings/profiles', { label: name });
        toast('Réglages sauvés sous « ' + name + ' ».');
        label.value = '';
        loadProfiles();
      } catch (e) {
        toast(e.message, true);
      }
    } }, 'Sauver les réglages actuels');
    return h('div', { class: 'svc-card svc-tools' },
      h('div', { class: 'svc-head' }, h('div', { class: 'svc-title' }, h('span', null, 'Réglages conseillés')), reset),
      h('p', { class: 'svc-effect' }, n === 0
        ? 'Tous les réglages du moteur et d’Internet sont aux valeurs conseillées pour ce Mac (mesures du portage sur le M2).'
        : n + (n === 1 ? ' réglage diffère' : ' réglages diffèrent') + ' des valeurs conseillées pour ce Mac ; chaque carte concernée le signale.'),
      h('div', { class: 'svc-head', style: { marginTop: '12px' } }, h('div', { class: 'svc-title' }, h('span', null, 'Sauvegardes'))),
      h('p', { class: 'svc-effect' }, 'Garde les réglages actuels sous un libellé, pour y revenir plus tard.'),
      h('div', { class: 'svc-input', style: { marginTop: '8px', flexWrap: 'wrap' } }, label, save),
      profilesBox);
  }

  const profilesBox = h('div', { class: 'svc-profiles' });

  async function loadProfiles() {
    let list = [];
    try {
      list = (await api.getJSON('/gulliver/v1/settings/profiles')).profiles || [];
    } catch (e) {
      profilesBox.replaceChildren(h('p', { class: 'help' }, e.message));
      return;
    }
    list.sort((a, b) => b.saved - a.saved);
    const date = (sec) => new Date(sec * 1000).toLocaleString('fr-FR', { dateStyle: 'medium', timeStyle: 'short' });
    profilesBox.replaceChildren(...(list.length ? list.map((p) => h('div', { class: 'svc-profile' },
      h('span', { class: 'name' }, p.label), h('span', { class: 'meta' }, date(p.saved)),
      h('button', { class: 'button', type: 'button', onclick: async () => {
        const changes = {};
        for (const [k, v] of Object.entries(p.settings || {})) {
          if (k === 'proxy' && String(v).includes('********')) continue;
          if (current[k] !== v) changes[k] = v;
        }
        if (!window.confirm('Restaurer « ' + p.label + ' » ?' + (Object.keys(changes).length ? '' : ' (rien ne change)'))) return;
        await sendAll(changes, 'ces réglages');
      } }, 'Restaurer'),
      h('button', { class: 'button', type: 'button', onclick: async () => {
        if (!window.confirm('Supprimer la sauvegarde « ' + p.label + ' » ?')) return;
        await fetch('/gulliver/v1/settings/profiles?label=' + encodeURIComponent(p.label), { method: 'DELETE' });
        loadProfiles();
      } }, 'Supprimer'))) : [h('p', { class: 'help' }, 'Aucune sauvegarde pour l’instant.')]));
  }

  /*
      Le relais vers un moteur extérieur (fork, repris de l'original 1.0.23) : son état, ses réglages et leur essai.
      Le secret ne revient jamais du service (********) : un champ vide garde celui qui est enregistré.
  */
  const relayReasons = {
    'the relay is turned off': 'le relais est coupé',
    'the relay has no valid address': 'il manque une adresse valable (http:// ou https://)',
    'the relay has no model': 'il manque le modèle',
    'the model of the relay is not one of its provider here': 'ce modèle n’est pas l’un de ceux de ce fournisseur',
    'the relay has no user': 'il manque l’utilisateur',
    'the relay has no password': 'il manque le mot de passe',
    'the relay has no key': 'il manque la clé',
    'offline mode: Claude needs the Internet': 'hors ligne : Claude a besoin d’Internet',
    'offline mode: Ollama needs the Internet': 'hors ligne : Ollama a besoin d’Internet',
    'offline mode: the address of the engine is neither on this computer nor on its local network':
      'hors ligne : l’adresse du moteur n’est ni sur ce Mac ni sur le réseau de la maison',
    'Pseudalis is not installed': 'Pseudalis n’est pas trouvé sur ce Mac',
    'the local authority of Pseudalis is missing': 'l’autorité locale de Pseudalis manque',
    'the data folder of Pseudalis is not protected': 'le dossier de Pseudalis pourrait être modifié par un autre compte'
  };
  const relayCodes = {
    relay_address: 'adresse refusée : un hôte et un port seulement, sans utilisateur, requête ni fragment',
    relay_key_missing: 'il manque la clé', relay_password_missing: 'il manque le mot de passe',
    relay_offline: 'hors ligne : ce fournisseur a besoin d’Internet',
    relay_offline_remote: 'hors ligne : l’adresse n’est pas sur le réseau de la maison',
    relay_offline_unresolved: 'hors ligne : le nom du moteur n’a pas d’adresse sur ce réseau',
    relay_unreachable: 'le moteur ne répond pas à cette adresse', relay_proxy: 'le proxy ne répond pas',
    relay_certificate: 'le certificat du moteur n’est pas reconnu', relay_no_answer: 'le moteur a cessé de répondre',
    relay_http_key: 'le moteur refuse la clé ou l’utilisateur', relay_http_credit: 'le compte n’a plus de crédit',
    relay_http_address: 'le moteur ne connaît pas cette adresse', relay_http_size: 'requête trop grande',
    relay_http_limit: 'le moteur limite les requêtes ou le crédit', relay_http_failing: 'le moteur est surchargé ou en panne',
    relay_http_refused: 'le moteur refuse la requête', relay_not_interface: 'cette adresse ne répond pas comme une interface OpenAI',
    relay_secret: 'la clé enregistrée ne vaut que pour l’ancien fournisseur, adresse et utilisateur : retape-la'
  };
  const relayWhy = (why) => relayReasons[why] || why;
  // Une fenêtre de contexte : « 1 M tokens », « 256 K tokens ».
  const relayTokens = (n) => n >= 1000000 ? (n / 1000000).toLocaleString('fr-FR') + ' M tokens'
    : Math.round(n / 1024) + ' K tokens';

  function relayBox() {
    const box = h('div', { class: 'svc-relay' }, h('p', { class: 'help' }, 'Lecture du relais…'));
    (async () => {
      let info;
      try {
        info = await api.getJSON('/gulliver/v1/relay');
      } catch (e) {
        box.replaceChildren(h('p', { class: 'help' }, e.message));
        return;
      }
      const saved = current.relay || {};
      const draft = Object.assign({}, saved);
      delete draft.secret;
      let typedSecret = '';
      let found = [];
      const providers = info.providers || [];
      const result = h('p', { class: 'svc-where' });

      const field = (label, input, note) => h('label', { class: 'svc-field' }, h('span', null, label), input,
        note ? h('small', null, note) : null);
      const check = (key, label) => {
        const input = h('input', { type: 'checkbox', checked: !!draft[key] });
        input.addEventListener('change', () => { draft[key] = input.checked; paint(); });
        return h('label', { class: 'svc-check' }, input, h('span', null, label));
      };
      const textInput = (key, placeholder, type) => {
        const input = h('input', { type: type || 'text', value: draft[key] == null ? '' : draft[key], placeholder,
          spellcheck: 'false', autocomplete: 'off' });
        input.addEventListener('change', () => {
          draft[key] = type === 'number' ? Number(input.value) : input.value.trim();
        });
        return input;
      };

      // Ce que le service enverrait : le secret seulement s'il vient d'être tapé.
      function body() {
        const b = {};
        for (const k of ['enabled', 'provider', 'url', 'model', 'auth', 'user', 'context', 'max_output', 'images',
          'reasoning', 'pseudonymize', 'keep_substitutes']) if (k in draft) b[k] = draft[k];
        if (typedSecret) b.secret = typedSecret;
        return b;
      }

      function stateLine() {
        const st = info.relay || {};
        if (!saved.enabled) return h('p', { class: 'svc-where' }, 'Coupé : toutes les discussions restent sur le Mac.');
        if (!st.ready) return h('p', { class: 'svc-where' }, 'Pas prêt : ' + relayWhy(st.reason) + '.');
        return h('p', { class: 'svc-where' }, 'Prêt : ', h('b', null, (st.name || st.model) + ' (' + st.provider_name + ')'),
          st.pseudonymize ? ', pseudonymisé par Pseudalis' : ', sans pseudonymisation : la discussion part telle quelle',
          st.image_mode === 'described' ? ' ; images décrites sur le Mac' : st.image_mode === 'sent'
            ? ' ; images envoyées' : ' ; pas d’images', '.');
      }

      function pseudalisLine() {
        const p = info.pseudalis || {};
        if (p.found) return h('small', null, 'Pseudalis répond sur ce Mac (port ' + p.port + ').');
        return h('small', null, 'Pseudalis : ' + relayWhy(p.reason || '') + '. Sans lui, aucune question ne part.');
      }

      function paint() {
        const provider = providers.find((p) => p.id === draft.provider) || providers[0] || { id: 'openai', models: [] };
        draft.provider = provider.id;
        const openai = provider.id === 'openai';
        const auth = openai ? (draft.auth || 'key') : 'key';
        const enabled = check('enabled', 'Allumer le relais');
        const choose = h('select', null, providers.map((p) => h('option', { value: p.id, selected: p.id === provider.id },
          p.id === 'openai' ? 'Interface OpenAI (autre moteur, ordinateur de la maison)' : p.name)));
        choose.addEventListener('change', () => {
          draft.provider = choose.value;
          const next = providers.find((p) => p.id === draft.provider);
          if (next && next.models && next.models.length) draft.model = next.models[0].id;
          paint();
        });
        let model;
        if (!openai) {
          model = h('select', null, (provider.models || []).map((m) => h('option', { value: m.id, selected: m.id === draft.model },
            m.name + ' · ' + relayTokens(m.context) + (m.images ? ' · images' : ''))));
          if (!draft.model && provider.models && provider.models.length) draft.model = provider.models[0].id;
          model.addEventListener('change', () => { draft.model = model.value; });
        } else {
          model = textInput('model', 'le nom du modèle chez ce moteur');
          model.setAttribute('list', 'svc-relay-models');
        }
        const models = h('datalist', { id: 'svc-relay-models' }, found.map((m) => h('option', { value: m })));
        const authChoice = h('select', null, [['key', 'Une clé'], ['basic', 'Utilisateur et mot de passe'], ['none', 'Aucune']]
          .map(([v, l]) => h('option', { value: v, selected: v === auth }, l)));
        authChoice.addEventListener('change', () => { draft.auth = authChoice.value; paint(); });
        const secret = h('input', { type: 'password', autocomplete: 'off', value: typedSecret,
          placeholder: saved.secret ? 'enregistrée (********) : vide pour la garder' : 'colle-la ici' });
        secret.addEventListener('input', () => { typedSecret = secret.value; });
        const test = h('button', { class: 'button', type: 'button', onclick: async () => {
          result.textContent = 'Essai…';
          try {
            const r = await api.postJSON('/gulliver/v1/relay/test', body());
            if (r.ok) {
              found = r.models || [];
              if (r.url && openai) draft.url = r.url;
              result.textContent = 'Le moteur répond : ' + found.length + (found.length > 1 ? ' modèles' : ' modèle') +
                (r.url && openai ? ', à l’adresse ' + r.url : '') + '.';
              paint();
            } else {
              result.textContent = 'Échec : ' + (relayCodes[r.code] || r.error || r.code) +
                (r.args && r.args.s ? ' (HTTP ' + r.args.s + ')' : '') + (r.detail ? ' : ' + r.detail : '') + '.';
            }
          } catch (e) {
            result.textContent = 'Échec : ' + (relayCodes[e.code] || e.message);
          }
        } }, 'Tester');
        const save = h('button', { class: 'button primary', type: 'button', onclick: async () => {
          try {
            await api.putJSON('/gulliver/v1/settings', { relay: body() });
            current = await api.getJSON('/gulliver/v1/settings');
            toast('Relais enregistré.');
            if (env.refreshStatus) env.refreshStatus();
            render();
            // Le rendu remonte en haut de l'onglet : la vue revient sur la carte du relais.
            const again = list.querySelector('.svc-card[data-key="relay"]');
            if (again) again.scrollIntoView({ block: 'start' });
          } catch (e) {
            toast(relayCodes[e.code] || e.message, true);
          }
        } }, 'Enregistrer');
        /*
            Depuis l'accès réseau (http, un autre appareil), les réglages du relais se lisent sans se changer : le
            service ne les accepte que du Mac lui-même (décision de l'humain du 2026-11-16).
        */
        const elsewhere = location.protocol === 'http:';
        if (elsewhere) {
          box.replaceChildren(stateLine(), h('p', { class: 'svc-where' },
            'Ces réglages se changent sur le Mac lui-même, dans cette page ouverte à son adresse locale ' +
            '(https://localhost) : ni le téléphone ni un autre appareil ne peuvent décider où partent les discussions ' +
            'ni quelle clé paie. Le cadenas des discussions reste utilisable ici.'));
          return;
        }
        box.replaceChildren(
          stateLine(),
          enabled,
          field('Fournisseur', choose),
          openai ? field('Adresse', textInput('url', 'http://192.168.1.20:8000/v1'),
            'La base de son interface ; sans /v1, le service l’ajoute si besoin.') : null,
          openai ? field('Authentification', authChoice) : null,
          openai && auth === 'basic' ? field('Utilisateur', textInput('user', '')) : null,
          auth !== 'none' ? field(auth === 'basic' ? 'Mot de passe' : 'Clé', secret,
            'Gardée par le service seul (relay.secret), jamais renvoyée à la page.') : null,
          field('Modèle', model), models,
          openai ? field('Contexte du modèle (tokens)', textInput('context', '131072', 'number')) : null,
          openai ? field('Réponse au plus (tokens)', textInput('max_output', '16384', 'number')) : null,
          openai ? check('images', 'Le modèle lit les images') : null,
          openai ? check('reasoning', 'Le modèle raisonne (reasoning_effort)') : null,
          check('pseudonymize', 'Pseudonymiser par Pseudalis avant l’envoi'),
          draft.pseudonymize ? pseudalisLine() : null,
          draft.pseudonymize ? check('keep_substitutes', 'Laisser les pseudonymes dans les réponses (contrôle)') : null,
          h('div', { class: 'svc-input', style: { marginTop: '8px', flexWrap: 'wrap' } }, test, save),
          result);
      }
      paint();
    })();
    return box;
  }

  // La place que prend la réserve, et son vidage.
  function reserveBox() {
    const box = h('div', { class: 'svc-where' }, 'Lecture de la réserve…');
    const gb = (b) => (b / 1073741824).toLocaleString('fr-FR', { maximumFractionDigits: 1 }) + ' Go';
    (async () => {
      let r;
      try {
        r = await api.getJSON('/gulliver/v1/reserve');
      } catch (e) {
        box.replaceChildren(e.message);
        return;
      }
      const files = r.files || [];
      const spare = files.filter((f) => !f.installed && !f.projector);
      const empty = h('button', { class: 'button', type: 'button', disabled: files.length === 0, onclick: async () => {
        if (!window.confirm('Vider la réserve ?\n\nLes ' + files.length + ' copies sont retirées ; un modèle supprimé ' +
          'devra se retélécharger.')) return;
        try {
          const res = await fetch('/gulliver/v1/reserve', { method: 'DELETE' });
          const o = await res.json();
          if (!res.ok) throw new Error(o.error || res.statusText);
          toast(o.removed + (o.removed === 1 ? ' copie retirée, ' : ' copies retirées, ') + gb(o.freed) + ' libérés.');
        } catch (e) {
          toast(e.message, true);
        }
        render();
      } }, 'Vider la réserve');
      box.replaceChildren(
        h('span', null, 'Utilisée : ', h('b', null, gb(r.used)), ' sur ' + gb(r.limit) +
          (spare.length ? ' (' + spare.map((f) => f.name || f.sha256.slice(0, 12)).join(', ') + ')' : '') +
          ' · clones des modèles installés : ' + gb(r.shared) + ', sans place prise. '),
        empty);
    })();
    return box;
  }

  function lanAddresses() {
    const addresses = current.lan_addresses || [];
    const port = valueOf('lan_port');
    if (!addresses.length) return h('p', { class: 'svc-where' }, 'Le Mac n’a pas d’adresse sur un réseau pour l’instant.');
    return h('p', { class: 'svc-where' }, current.lan ? 'Sur le téléphone : ' : 'Une fois ouvert, sur le téléphone : ',
      addresses.map((a, i) => [i ? ' ou ' : '', h('code', null, 'http://' + a + ':' + port + '/')]));
  }

  function render() {
    const open = new Set(Array.from(list.querySelectorAll('.svc-card.open')).map((c) => c.dataset.key));
    list.replaceChildren(
      tools(),
      ...groups.map((g) => h('section', { class: 'svc-group' }, h('h4', null, g.title), h('p', { class: 'help' }, g.note),
        g.items.map((k) => card(k, items[k])))));
    for (const k of open) {
      const c = list.querySelector('.svc-card[data-key="' + k + '"]');
      if (c) toggle(c, true);
    }
    renderBar();
  }

  async function load() {
    try {
      current = await api.getJSON('/gulliver/v1/settings');
      render();
      loadProfiles();
    } catch (e) {
      list.replaceChildren(h('p', { class: 'help' }, 'Les réglages ne se lisent pas : ' + e.message));
    }
  }
  load();
}
