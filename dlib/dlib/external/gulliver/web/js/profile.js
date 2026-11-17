// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le profil courant de la page (fork pour macOS).  Les données ne vivent plus dans ce navigateur mais
    dans le service, rangées par profil ; chaque requête de données lui dit à quel profil elle s'adresse,
    par l'en-tête HTTP X-Gulliver-Profile.  Ce module tient l'identifiant du profil choisi : il le lit une
    fois dans localStorage (le navigateur se souvient du dernier profil d'une visite à l'autre), puis le
    garde en mémoire.  localStorage peut lever en navigation privée, aussi chaque accès est protégé.
*/

const KEY = 'gulliver.profile';

/*
    L'identifiant du profil courant, gardé en mémoire.  undefined tant qu'il n'a pas été lu de localStorage,
    une chaîne (ou null) une fois lu : ainsi un premier appel va chercher le dernier profil choisi, les
    suivants répondent de mémoire.
*/
let current;

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
    // En navigation privée l'écriture est perdue, sans conséquence : le profil reste en mémoire pour la visite.
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

// La liste des profils du service : [{id, name, created, last_used}], la plus récemment utilisée d'abord.
export async function listProfiles() {
  const res = await fetch('/gulliver/v1/profiles');
  if (!res.ok) throw new Error('profiles');
  return (await res.json()).profiles || [];
}

// Crée un profil au nom donné et renvoie son enregistrement.  Lève sur un nom déjà pris (409).
export async function createProfile(name) {
  const res = await fetch('/gulliver/v1/profiles', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name })
  });
  if (!res.ok) {
    const e = new Error('create');
    e.status = res.status;
    throw e;
  }
  return res.json();
}

/*
    S'assure qu'un profil est choisi avant que la page charge ses données.  Si le profil mémorisé
    n'existe plus (supprimé ailleurs), il est oublié.  Quand aucun profil n'est choisi, ouvre le
    sélecteur pour en choisir un existant ou en créer un ; il ne se ferme pas tant qu'aucun n'est
    choisi (un profil est nécessaire pour que la page serve à quelque chose).  Renvoie l'identifiant
    choisi, ou null si le service ne répond pas (la page basculera alors en mode mémoire).
*/
export async function ensureProfile(dialog, h, t) {
  let profiles;
  try {
    profiles = await listProfiles();
  } catch (e) {
    return null; // Service injoignable : le démarrage gère ce cas (mode mémoire).
  }
  // Le profil mémorisé a-t-il disparu ?
  const chosen = currentProfile();
  if (chosen && !profiles.some((p) => p.id === chosen)) setProfile(null);
  if (hasProfile()) return currentProfile();
  // Un seul profil et aucun choix mémorisé : le prendre sans rien demander.
  if (profiles.length === 1) {
    setProfile(profiles[0].id);
    return currentProfile();
  }
  const id = await chooseProfile(dialog, h, t, profiles, false);
  setProfile(id);
  return currentProfile();
}

/*
    Ouvre le sélecteur pour changer de profil (depuis le menu).  Si un autre profil est choisi, le
    mémorise et recharge la page pour qu'elle lise les données de ce profil depuis le début.  dialog,
    h et t sont passés pour éviter un cycle d'imports.
*/
export async function switchProfile(dialog, h, t) {
  const before = currentProfile();
  const id = await chooseProfile(dialog, h, t, null, true);
  if (id && id !== before) {
    setProfile(id);
    location.hash = '';
    location.reload();
  }
}

/*
    Le sélecteur de profil : la liste des profils existants et un champ pour en créer un.  Résout avec
    l'identifiant choisi.  Quand cancellable est faux (au démarrage), il n'offre pas d'annuler : la page
    a besoin d'un profil.  dialog, h et t sont passés pour éviter un cycle d'imports avec dialogs.js.
*/
export async function chooseProfile(dialog, h, t, profiles, cancellable) {
  let list = profiles;
  if (!list) list = await listProfiles().catch(() => []);
  return dialog((box, close) => {
    box.append(h('h2', null, t('profileTitle')));
    box.append(h('p', { class: 'help' }, t('profileIntro')));
    const items = h('div', { class: 'profile-list' });
    for (const p of list) {
      items.append(h('button', {
        class: 'button profile-item', type: 'button', onclick: () => close(p.id)
      }, p.name));
    }
    if (list.length) box.append(items);
    const input = h('input', { placeholder: t('profileNewPlaceholder'), style: { width: '100%' } });
    const error = h('p', { class: 'help', style: { color: 'var(--danger, #c0392b)' } });
    const create = async () => {
      const name = input.value.trim();
      if (!name) return;
      try {
        const p = await createProfile(name);
        close(p.id);
      } catch (e) {
        error.textContent = e.status === 409 ? t('profileNameTaken') : t('profileCreateFailed');
      }
    };
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') create(); });
    box.append(h('div', { class: 'field' }, h('label', null, t('profileNewLabel')), input, error));
    const actions = h('div', { class: 'actions' });
    if (cancellable) actions.append(h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('cancel')));
    actions.append(h('button', { class: 'button primary', type: 'button', onclick: create }, t('profileCreate')));
    box.append(actions);
  }, true);
}
