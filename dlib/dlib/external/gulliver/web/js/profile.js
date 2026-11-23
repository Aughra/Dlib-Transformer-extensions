// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le profil courant de la page et sa session (fork pour macOS).  Les données vivent dans le service, rangées
    par profil, et chaque profil a un mot de passe (comptes, 2026-11-22) : la page se connecte
    (/gulliver/v1/login) et reçoit un jeton de session qu'elle envoie avec chaque requête de données, dans
    l'en-tête X-Gulliver-Session (pas de cookie : la passerelle de Cloudflare les retire).  Le service ne sert
    les données qu'au profil de la session.  Le premier profil est l'administrateur : lui seul crée, renomme
    ou supprime les autres et change leur adresse (onglet Profils, accounts.js).

    Depuis le 2026-11-23, on se connecte par son adresse mail et son mot de passe, puis par le code à 8 chiffres
    reçu par mail.  Un mot de passe oublié, ou le premier (un profil neuf n'en a pas), se choisit par le lien d'un
    mail : il rouvre la page avec « #reset=<jeton> », que ce module lit dès son chargement puis efface de
    l'adresse (il ne reste ni dans l'historique ni dans un favori).

    Le jeton est gardé dans localStorage pour ne pas redemander le mot de passe à chaque visite ; il expire
    après 30 jours sans servir, et « Se déconnecter » le détruit côté service.  localStorage peut lever en
    navigation privée, aussi chaque accès est protégé : la session dure alors le temps de la visite.
*/

const KEY = 'gulliver.profile';
const SESSION = 'gulliver.session';
const EMAIL = 'gulliver.email';

// Le jeton du lien « mot de passe oublié », lu avant que la navigation de la page ne touche à l'adresse.
let resetToken = null;
{
  const m = /^#reset=([0-9a-f]{64})$/.exec(location.hash);
  if (m) {
    resetToken = m[1];
    history.replaceState(null, '', location.pathname + location.search + '#/');
  }
}

// L'identifiant du profil courant (undefined tant qu'il n'a pas été lu de localStorage), son jeton et sa fiche.
let current;
let token;
let account = null;

// Lit une clé de localStorage sans jamais lever (une fenêtre de navigation privée peut l'interdire).
function readStore(key) {
  try {
    return localStorage.getItem(key);
  } catch (e) {
    return null;
  }
}

// Écrit (ou efface, avec value null) une clé de localStorage sans jamais lever.
function writeStore(key, value) {
  try {
    if (value == null || value === '') localStorage.removeItem(key);
    else localStorage.setItem(key, value);
  } catch (e) {
    // En navigation privée l'écriture est perdue, sans conséquence : la valeur reste en mémoire pour la visite.
  }
}

// L'identifiant du profil courant : une chaîne, ou null si aucun profil n'est choisi.
export function currentProfile() {
  if (current === undefined) {
    const stored = readStore(KEY);
    current = stored && stored.length ? stored : null;
  }
  return current;
}

// Fixe le profil courant (en mémoire et dans localStorage) ; un identifiant null ou vide efface le choix.
export function setProfile(id) {
  current = id && String(id).length ? String(id) : null;
  writeStore(KEY, current);
}

// Vrai lorsqu'un profil est choisi.
export function hasProfile() {
  return currentProfile() != null;
}

// Le jeton de la session (ou une chaîne vide).
function sessionToken() {
  if (token === undefined) token = readStore(SESSION) || '';
  return token;
}

function setSession(value) {
  token = value || '';
  writeStore(SESSION, token || null);
}

/*
    Chaque appel de la page au service (adresses qui commencent par « / ») porte la session : les routes
    qui changent le service (modèles, réglages) la demandent depuis le réseau de la maison, où seul
    l'administrateur y a droit.  Un seul endroit plutôt qu'un en-tête ajouté à chaque appel.
*/
const nativeFetch = window.fetch.bind(window);
window.fetch = (input, init) => {
  const url = typeof input === 'string' ? input : (input && input.url) || '';
  const same = url.startsWith('/') ? !url.startsWith('//') : url.startsWith(location.origin + '/');
  if (same && sessionToken()) {
    const headers = new Headers((init && init.headers) || (typeof input === 'string' ? undefined : input.headers));
    if (!headers.has('X-Gulliver-Session')) headers.set('X-Gulliver-Session', sessionToken());
    init = Object.assign({}, init, { headers });
  }
  return nativeFetch(input, init);
};

// La fiche du profil connecté : {id, name, admin}, ou null.
export function currentAccount() {
  return account;
}

// Les en-têtes d'une requête de données : la session et le profil qu'elle doit servir.
export function authHeaders(extra) {
  return Object.assign({ 'X-Gulliver-Session': sessionToken(), 'X-Gulliver-Profile': currentProfile() || '' }, extra || {});
}

// Un appel JSON au service avec la session ; lève une erreur qui porte le statut et le code de l'erreur.
async function call(method, path, body) {
  const res = await fetch(path, {
    method, headers: authHeaders({ 'Content-Type': 'application/json' }), body: body ? JSON.stringify(body) : undefined
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const e = new Error((data.error && data.error.message) || ('service ' + res.status));
    e.status = res.status;
    e.code = data.error && data.error.code;
    e.retry = Number(res.headers.get('Retry-After')) || 0;
    e.left = (data.error && data.error.left) || 0;
    throw e;
  }
  return data;
}

// La liste des profils du service et s'il faut créer le premier mot de passe : {profiles, setup}.
export async function listProfiles() {
  return call('GET', '/gulliver/v1/profiles');
}

// La gestion des profils (administrateur seul) et son propre mot de passe.
export const createProfile = (name, email) => call('POST', '/gulliver/v1/profiles', { name, email });
export const updateProfile = (id, changes) => call('PUT', '/gulliver/v1/profiles/' + encodeURIComponent(id), changes);
export const deleteProfile = (id) => call('DELETE', '/gulliver/v1/profiles/' + encodeURIComponent(id));
export const changePassword = (currentPassword, password) =>
  call('POST', '/gulliver/v1/session/password', { current: currentPassword, password });

// Le message à montrer pour une erreur de connexion ou de mot de passe.
export function accountError(e) {
  if (!e || !e.code) return 'Le service ne répond pas.';
  return {
    wrong_password: 'Adresse ou mot de passe incorrect.',
    login_throttled: 'Trop d’essais manqués : réessayez dans ' + (e.retry || 30) + ' secondes.',
    password_not_set: 'Ce profil n’a pas encore de mot de passe : cliquez sur « Mot de passe oublié ou première ' +
      'connexion » pour le choisir.',
    wrong_code: 'Code incorrect.',
    code_expired: 'Ce code ne vaut plus : reconnectez-vous pour en recevoir un nouveau.',
    link_expired: 'Ce lien ne vaut plus (30 minutes, une seule fois) : demandez-en un nouveau.',
    email: 'Tapez une adresse mail valable.',
    email_taken: 'Un autre profil a déjà cette adresse.',
    mail_failed: 'Le mail n’a pas pu partir : Proton Mail Bridge tourne-t-il sur le Mac ?',
    mail_unconfigured: 'Le service n’a pas encore d’adresse d’envoi : réglez « Adresse d’envoi » dans l’onglet Service, ' +
      'depuis le Mac.',
    mail_throttled: 'Un mail vient de partir vers cette adresse : attendez une minute avant d’en redemander un.',
    password_by_mail: 'Chacun choisit son mot de passe par le lien « Mot de passe oublié ».',
    password: 'Le mot de passe doit faire de 8 à 256 caractères.',
    name: 'Le nom doit faire de 1 à 80 caractères.',
    name_taken: 'Un profil porte déjà ce nom.',
    setup_remote: 'Le premier compte se crée depuis le Mac ou le réseau de la maison.',
    setup_done: 'L’administrateur a déjà son adresse : connectez-vous.',
    admin_required: 'Seul l’administrateur gère les profils, les modèles et les réglages du service, depuis le Mac ou le réseau de la maison.',
    admin_kept: 'Le profil de l’administrateur ne se supprime pas.'
  }[e.code] || e.message;
}

/*
    S'assure qu'une session est ouverte avant que la page charge ses données.  Le lien d'un mail « mot de passe
    oublié » passe en premier ; puis une session gardée et encore valable sert telle quelle ; sinon la page
    demande le premier compte (sans administrateur, ou un administrateur sans adresse) ou la connexion, sans
    possibilité de fermer (la page a besoin d'un profil).  Renvoie l'identifiant du profil, ou null si le service
    ne répond pas (la page basculera alors en mode mémoire).  dialog, h et t sont passés pour éviter un cycle
    d'imports.
*/
export async function ensureProfile(dialog, h, t) {
  let opened = null;
  if (resetToken) {
    const token = resetToken;
    resetToken = null;
    opened = await resetDialog(dialog, h, token);
  }
  if (!opened && sessionToken()) {
    try {
      const s = await call('GET', '/gulliver/v1/session');
      account = s.profile;
      setProfile(account.id);
      return currentProfile();
    } catch (e) {
      if (e.status !== 401) return null; // Service injoignable : le démarrage gère ce cas (mode mémoire).
      setSession(null);
    }
  }
  let list = null;
  if (!opened) {
    try {
      list = await listProfiles();
    } catch (e) {
      return null;
    }
  }
  // Échap ou un clic à côté ferment une boîte sans rien rendre : elle se rouvre tant qu'aucune session n'est ouverte.
  while (!opened) {
    const r = list.setup ? await setupDialog(dialog, h, list) : await loginDialog(dialog, h);
    if (!r) continue;
    if (r.forgot) {
      await forgotDialog(dialog, h, r.email);
    } else if (r.sent) {
      list.setup = false;
      await sentDialog(dialog, h, r.email);
    } else if (r.challenge) {
      opened = await codeDialog(dialog, h, r.challenge, r.email);
    } else if (r.token) {
      opened = r;
    }
  }
  setSession(opened.token);
  account = opened.profile;
  setProfile(account.id);
  if (account.email) writeStore(EMAIL, account.email);
  return currentProfile();
}

// Se déconnecter : la session est détruite côté service, puis la page repart sur l'écran de connexion.
export async function logout() {
  try { await call('POST', '/gulliver/v1/logout'); } catch (e) { /* le jeton est oublié de toute façon */ }
  setSession(null);
  location.hash = '';
  location.reload();
}

// Changer de profil, c'est se déconnecter puis se connecter sous un autre nom.
export async function switchProfile() {
  await logout();
}

// Un champ de mot de passe (masqué, sans remplissage automatique d'un autre site).
function passwordInput(h, placeholder, autocomplete) {
  return h('input', { type: 'password', placeholder, autocomplete, style: { width: '100%' } });
}

// Les pièces communes des boîtes de connexion.
function emailInput(h, value) {
  return h('input', { type: 'email', placeholder: 'Adresse mail', autocomplete: 'username', value: value || '',
    autocapitalize: 'off', spellcheck: 'false', style: { width: '100%' } });
}
function errorLine(h) {
  return h('p', { class: 'help', role: 'alert', style: { color: 'var(--danger, #c0392b)' } });
}
function onEnter(inputs, action) {
  for (const input of inputs) input.addEventListener('keydown', (e) => { if (e.key === 'Enter') action(); });
}
function linkButton(h, label, onclick) {
  return h('button', { class: 'button', type: 'button', onclick }, label);
}

/*
    Le premier compte.  Sans aucun profil : un nom et une adresse ; le service crée l'administrateur et lui envoie
    le lien pour choisir son mot de passe (résout avec {sent, email}).  Un administrateur d'une base d'avant les
    adresses donne la sienne, avec son mot de passe actuel s'il en a un ; le code envoyé à cette adresse la
    vérifie (résout avec {challenge, email}).
*/
export function setupDialog(dialog, h, list) {
  const legacy = Boolean(list.setup_password);
  return dialog((box, close) => {
    box.append(h('h2', null, legacy ? 'L’adresse mail de l’administrateur' : 'Créer le profil de l’administrateur'));
    box.append(h('p', { class: 'help' }, legacy
      ? 'Gulliver se connecte maintenant par adresse mail, avec un code reçu par mail à chaque connexion. Donnez ' +
        'l’adresse de l’administrateur et son mot de passe actuel : un code part vers cette adresse pour la vérifier. ' +
        'Mot de passe oublié : sur le Mac, « python3 tools/mot_de_passe.py --email VOTRE_ADRESSE » inscrit l’adresse, ' +
        'puis « Mot de passe oublié » vous envoie un lien.'
      : 'Chaque profil garde ses discussions, ses projets et ses documents. Le premier est l’administrateur : il crée ' +
        'les autres. Donnez votre nom et votre adresse mail : un lien part vers cette adresse pour choisir votre mot de passe.'));
    const name = h('input', { placeholder: 'Un nom, par exemple le vôtre', autocomplete: 'name', style: { width: '100%' } });
    const email = emailInput(h);
    const pass = passwordInput(h, 'Mot de passe actuel', 'current-password');
    const error = errorLine(h);
    const submit = async () => {
      try {
        const body = legacy ? { email: email.value.trim(), password: pass.value } : { name: name.value.trim(), email: email.value.trim() };
        const r = await call('POST', '/gulliver/v1/setup', body);
        close(Object.assign({ email: email.value.trim() }, r));
      } catch (e) {
        error.textContent = accountError(e);
      }
    };
    onEnter([name, email, pass], submit);
    if (!legacy) box.append(h('div', { class: 'field' }, h('label', null, 'Nom'), name));
    box.append(h('div', { class: 'field' }, h('label', null, 'Adresse mail'), email));
    if (legacy) box.append(h('div', { class: 'field' }, h('label', null, 'Mot de passe'), pass));
    box.append(error, h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Valider')));
  }, true);
}

/*
    La connexion, premier temps : l'adresse et le mot de passe.  Résout avec {challenge, email} (un code est parti),
    ou {forgot, email} pour « Mot de passe oublié ou première connexion ».
*/
export function loginDialog(dialog, h) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Se connecter'));
    box.append(h('p', { class: 'help' }, 'Tapez votre adresse mail et votre mot de passe : un code à 8 chiffres part ' +
      'ensuite vers votre adresse. Première connexion, ou mot de passe oublié : le bouton du bas vous envoie un lien ' +
      'pour le choisir.'));
    const email = emailInput(h, readStore(EMAIL));
    const pass = passwordInput(h, 'Mot de passe', 'current-password');
    const error = errorLine(h);
    const submit = async () => {
      try {
        const r = await call('POST', '/gulliver/v1/login', { email: email.value.trim(), password: pass.value });
        writeStore(EMAIL, email.value.trim());
        close({ challenge: r.challenge, email: email.value.trim() });
      } catch (e) {
        error.textContent = accountError(e);
        pass.select();
      }
    };
    onEnter([email, pass], submit);
    box.append(h('div', { class: 'field' }, h('label', null, 'Adresse mail'), email));
    box.append(h('div', { class: 'field' }, h('label', null, 'Mot de passe'), pass, error));
    box.append(h('div', { class: 'actions' },
      linkButton(h, 'Mot de passe oublié ou première connexion', () => close({ forgot: true, email: email.value.trim() })),
      h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Se connecter')));
    setTimeout(() => (email.value ? pass : email).focus(), 50);
  }, true);
}

// La connexion, second temps : le code reçu par mail.  Résout avec {token, profile}, ou null pour revenir en arrière.
export function codeDialog(dialog, h, challenge, email) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Le code reçu par mail'));
    box.append(h('p', { class: 'help' }, 'Un code à 8 chiffres vient de partir vers ' + (email || 'votre adresse') +
      '. Il vaut 10 minutes. Pas de mail ? Regardez dans les indésirables.'));
    const code = h('input', { inputmode: 'numeric', autocomplete: 'one-time-code', placeholder: '8 chiffres',
      maxlength: 9, style: { width: '100%', letterSpacing: '0.2em', fontSize: '1.3em' } });
    const error = errorLine(h);
    const submit = async () => {
      try {
        close(await call('POST', '/gulliver/v1/login/code', { challenge, code: code.value }));
      } catch (e) {
        error.textContent = accountError(e) + (e.code === 'wrong_code' && e.left ? ' Il reste ' + e.left + ' essai' + (e.left > 1 ? 's.' : '.') : '');
        if (e.code === 'code_expired') setTimeout(() => close(null), 2500);
        code.select();
      }
    };
    code.addEventListener('input', () => { if (code.value.replace(/\D/g, '').length === 8) submit(); });
    onEnter([code], submit);
    box.append(h('div', { class: 'field' }, h('label', null, 'Code'), code, error));
    box.append(h('div', { class: 'actions' }, linkButton(h, 'Revenir', () => close(null)),
      h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Valider')));
    setTimeout(() => code.focus(), 50);
  }, true);
}

// « Mot de passe oublié ou première connexion » : l'adresse, puis le lien part.
export function forgotDialog(dialog, h, value) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Choisir son mot de passe'));
    box.append(h('p', { class: 'help' }, 'Tapez l’adresse mail de votre profil : un lien part vers elle. Ouvrez-le ' +
      'sur cet appareil ou un autre de la maison pour choisir votre mot de passe. Il vaut 30 minutes.'));
    const email = emailInput(h, value);
    const error = errorLine(h);
    const submit = async () => {
      try {
        await call('POST', '/gulliver/v1/password/forgot', { email: email.value.trim() });
        box.replaceChildren(h('h2', null, 'Regardez vos mails'),
          h('p', { class: 'help' }, 'Si un profil a l’adresse ' + email.value.trim() + ', un lien vient de partir vers elle. ' +
            'Pas de mail ? Regardez dans les indésirables, ou vérifiez l’adresse auprès de l’administrateur.'),
          h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, 'Revenir à la connexion')));
      } catch (e) {
        error.textContent = accountError(e);
      }
    };
    onEnter([email], submit);
    box.append(h('div', { class: 'field' }, h('label', null, 'Adresse mail'), email, error));
    box.append(h('div', { class: 'actions' }, linkButton(h, 'Revenir', () => close(null)),
      h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Envoyer le lien')));
  }, true);
}

// Après le premier compte : le lien est parti.
function sentDialog(dialog, h, email) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Regardez vos mails'),
      h('p', { class: 'help' }, 'Un lien vient de partir vers ' + (email || 'votre adresse') + ' pour choisir votre ' +
        'mot de passe. Ouvrez-le pour finir.'),
      h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, 'Revenir à la connexion')));
  }, true);
}

// Le lien du mail : choisir le mot de passe (deux fois).  Résout avec {token, profile}, ou null.
export function resetDialog(dialog, h, token) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Choisir votre mot de passe'));
    box.append(h('p', { class: 'help' }, 'Huit caractères au moins. Vos autres appareils devront se reconnecter.'));
    const pass = passwordInput(h, 'Nouveau mot de passe (8 caractères au moins)', 'new-password');
    const again = passwordInput(h, 'Le même, une seconde fois', 'new-password');
    const error = errorLine(h);
    const submit = async () => {
      if (pass.value !== again.value) { error.textContent = 'Les deux mots de passe diffèrent.'; return; }
      try {
        close(await call('POST', '/gulliver/v1/password/reset', { token, password: pass.value }));
      } catch (e) {
        error.textContent = accountError(e);
        if (e.code === 'link_expired') setTimeout(() => close(null), 3000);
      }
    };
    onEnter([pass, again], submit);
    box.append(h('div', { class: 'field' }, h('label', null, 'Mot de passe'), pass, again, error));
    box.append(h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Valider')));
    setTimeout(() => pass.focus(), 50);
  }, true);
}
