// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le profil courant de la page et sa session (fork pour macOS).  Les données vivent dans le service, rangées
    par profil, et chaque profil a un mot de passe (comptes, 2026-11-22) : la page se connecte
    (/gulliver/v1/login) et reçoit un jeton de session qu'elle envoie avec chaque requête de données, dans
    l'en-tête X-Gulliver-Session (pas de cookie : la passerelle de Cloudflare les retire).  Le service ne sert
    les données qu'au profil de la session.  Le premier profil est l'administrateur : lui seul crée, renomme
    ou supprime les autres et change leur mot de passe (onglet Profils, accounts.js).

    Le jeton est gardé dans localStorage pour ne pas redemander le mot de passe à chaque visite ; il expire
    après 30 jours sans servir, et « Se déconnecter » le détruit côté service.  localStorage peut lever en
    navigation privée, aussi chaque accès est protégé : la session dure alors le temps de la visite.
*/

const KEY = 'gulliver.profile';
const SESSION = 'gulliver.session';

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
    throw e;
  }
  return data;
}

// La liste des profils du service et s'il faut créer le premier mot de passe : {profiles, setup}.
export async function listProfiles() {
  return call('GET', '/gulliver/v1/profiles');
}

// La gestion des profils (administrateur seul) et son propre mot de passe.
export const createProfile = (name, password) => call('POST', '/gulliver/v1/profiles', { name, password });
export const updateProfile = (id, changes) => call('PUT', '/gulliver/v1/profiles/' + encodeURIComponent(id), changes);
export const deleteProfile = (id) => call('DELETE', '/gulliver/v1/profiles/' + encodeURIComponent(id));
export const changePassword = (currentPassword, password) =>
  call('POST', '/gulliver/v1/session/password', { current: currentPassword, password });

// Le message à montrer pour une erreur de connexion ou de mot de passe.
export function accountError(e) {
  if (!e || !e.code) return 'Le service ne répond pas.';
  return {
    wrong_password: 'Mot de passe incorrect.',
    login_throttled: 'Trop d’essais manqués : réessayez dans ' + (e.retry || 30) + ' secondes.',
    password_not_set: 'Ce profil n’a pas encore de mot de passe : l’administrateur doit lui en donner un.',
    password: 'Le mot de passe doit faire de 8 à 256 caractères.',
    name: 'Le nom doit faire de 1 à 80 caractères.',
    name_taken: 'Un profil porte déjà ce nom.',
    setup_remote: 'Le premier mot de passe se choisit depuis le Mac ou le réseau de la maison.',
    setup_done: 'L’administrateur a déjà un mot de passe : connectez-vous.',
    admin_required: 'Seul l’administrateur gère les profils, les modèles et les réglages du service, depuis le Mac ou le réseau de la maison.',
    admin_kept: 'Le profil de l’administrateur ne se supprime pas.'
  }[e.code] || e.message;
}

/*
    S'assure qu'une session est ouverte avant que la page charge ses données.  Une session gardée et encore
    valable sert telle quelle ; sinon la page demande le premier mot de passe (sans administrateur) ou la
    connexion, sans possibilité de fermer (la page a besoin d'un profil).  Renvoie l'identifiant du profil, ou
    null si le service ne répond pas (la page basculera alors en mode mémoire).  dialog, h et t sont passés
    pour éviter un cycle d'imports.
*/
export async function ensureProfile(dialog, h, t) {
  if (sessionToken()) {
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
  let list;
  try {
    list = await listProfiles();
  } catch (e) {
    return null;
  }
  // Échap ou un clic à côté ferment une boîte sans rien rendre : elle se rouvre tant qu'aucune session n'est ouverte.
  let opened = null;
  while (!opened) {
    opened = list.setup ? await setupDialog(dialog, h, list.profiles || [])
      : await loginDialog(dialog, h, list.profiles || []);
  }
  setSession(opened.token);
  account = opened.profile;
  setProfile(account.id);
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

/*
    Le premier mot de passe : sans aucun profil, crée l'administrateur (un nom et un mot de passe) ; avec une
    base d'avant les comptes, donne son mot de passe au profil devenu administrateur.  Résout avec {token, profile}.
*/
export function setupDialog(dialog, h, profiles) {
  const admin = profiles.find((p) => p.admin);
  return dialog((box, close) => {
    box.append(h('h2', null, admin ? 'Choisir le mot de passe de l’administrateur' : 'Créer le profil de l’administrateur'));
    box.append(h('p', { class: 'help' }, admin
      ? 'Gulliver protège maintenant chaque profil par un mot de passe. « ' + admin.name + ' » est l’administrateur : ' +
        'il gère les autres profils et leurs mots de passe. Choisissez son mot de passe (8 caractères au moins).'
      : 'Chaque profil garde ses discussions, ses projets et ses documents, protégés par un mot de passe. Le premier ' +
        'est l’administrateur : il crée les autres profils et gère leurs mots de passe.'));
    const name = h('input', { placeholder: 'Un nom, par exemple le vôtre', autocomplete: 'username', style: { width: '100%' } });
    const pass = passwordInput(h, 'Mot de passe (8 caractères au moins)', 'new-password');
    const again = passwordInput(h, 'Le même mot de passe, une seconde fois', 'new-password');
    const error = h('p', { class: 'help', role: 'alert', style: { color: 'var(--danger, #c0392b)' } });
    const submit = async () => {
      if (pass.value !== again.value) { error.textContent = 'Les deux mots de passe diffèrent.'; return; }
      try {
        close(await call('POST', '/gulliver/v1/setup', { name: admin ? admin.name : name.value.trim(), password: pass.value }));
      } catch (e) {
        error.textContent = accountError(e);
      }
    };
    for (const input of [name, pass, again]) input.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(); });
    if (!admin) box.append(h('div', { class: 'field' }, h('label', null, 'Nom'), name));
    box.append(h('div', { class: 'field' }, h('label', null, 'Mot de passe'), pass, again, error));
    box.append(h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Valider')));
  }, true);
}

// La connexion : le profil (choisi dans la liste) et son mot de passe.  Résout avec {token, profile}.
export function loginDialog(dialog, h, profiles) {
  return dialog((box, close) => {
    box.append(h('h2', null, 'Se connecter'));
    box.append(h('p', { class: 'help' }, 'Choisissez votre profil et tapez son mot de passe. Un profil sans mot de passe ' +
      'en reçoit un de l’administrateur.'));
    const last = currentProfile();
    const select = h('select', { style: { width: '100%' } }, profiles.map((p) =>
      h('option', { value: p.id, selected: p.id === last }, p.name + (p.admin ? ' (administrateur)' : ''))));
    const pass = passwordInput(h, 'Mot de passe', 'current-password');
    const error = h('p', { class: 'help', role: 'alert', style: { color: 'var(--danger, #c0392b)' } });
    const submit = async () => {
      try {
        close(await call('POST', '/gulliver/v1/login', { profile: select.value, password: pass.value }));
      } catch (e) {
        error.textContent = accountError(e);
        pass.select();
      }
    };
    pass.addEventListener('keydown', (e) => { if (e.key === 'Enter') submit(); });
    box.append(h('div', { class: 'field' }, h('label', null, 'Profil'), select));
    box.append(h('div', { class: 'field' }, h('label', null, 'Mot de passe'), pass, error));
    box.append(h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: submit }, 'Se connecter')));
    setTimeout(() => pass.focus(), 50);
  }, true);
}
