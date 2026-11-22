// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet Profils des paramètres (fork pour macOS, 2026-11-22).  Chacun y voit sous quel profil il est
    connecté et y change son mot de passe.  L'administrateur (le premier profil) y gère les autres : en
    créer un avec son mot de passe, le renommer, lui donner un nouveau mot de passe (ses sessions ouvertes
    se ferment), le supprimer avec toutes ses données.  Le service vérifie chaque droit
    (host/server.cpp, require_admin) ; la page ne fait que montrer ce qui est permis.  Par la passerelle de
    Cloudflare, la gestion des profils est refusée : seule la partie « mon mot de passe » est montrée.
*/

import { confirmDialog } from './dialogs.js';
import { h, toast } from './dom.js';
import { accountError, changePassword, createProfile, currentAccount, deleteProfile, listProfiles, logout,
  updateProfile } from './profile.js';
import { state } from './state.js';

const password = (placeholder, autocomplete) =>
  h('input', { type: 'password', placeholder, autocomplete: autocomplete || 'new-password' });

export function accountsTab(body) {
  const me = currentAccount();
  if (!me) {
    body.append(h('p', { class: 'help' }, 'Aucune session ouverte : la page est en mode mémoire.'));
    return;
  }
  body.append(h('p', null, 'Connecté en tant que ', h('strong', null, me.name), me.admin ? ' (administrateur).' : '.'),
    h('div', { class: 'actions' }, h('button', { class: 'button', type: 'button', onclick: () => logout() }, 'Se déconnecter')));

  // Mon mot de passe.
  const current = password('Mot de passe actuel', 'current-password');
  const fresh = password('Nouveau mot de passe (8 caractères au moins)');
  const again = password('Le nouveau, une seconde fois');
  const ownError = h('p', { class: 'help', role: 'alert' });
  const changeOwn = async () => {
    if (fresh.value !== again.value) { ownError.textContent = 'Les deux nouveaux mots de passe diffèrent.'; return; }
    try {
      await changePassword(current.value, fresh.value);
      current.value = fresh.value = again.value = '';
      ownError.textContent = '';
      toast('Mot de passe changé. Vos autres appareils devront se reconnecter.');
    } catch (e) {
      ownError.textContent = accountError(e);
    }
  };
  body.append(h('h3', null, 'Mon mot de passe'),
    h('div', { class: 'field account-password' }, current, fresh, again, ownError),
    h('div', { class: 'actions' }, h('button', { class: 'button', type: 'button', onclick: changeOwn }, 'Changer mon mot de passe')));

  if (!me.admin) return;
  const remote = Boolean(state.status && state.status.remote);
  body.append(h('h3', null, 'Les profils'));
  if (remote) {
    body.append(h('p', { class: 'help' }, 'La gestion des profils se fait depuis le Mac ou le réseau de la maison.'));
    return;
  }
  body.append(h('p', { class: 'help' }, 'Vous seul gérez les profils. Chaque profil a ses discussions, ses projets et ses ' +
    'documents, que les autres ne voient pas. Il n’y a qu’un administrateur.'));
  const list = h('div', { class: 'account-list' });
  body.append(list);

  const render = async () => {
    let data;
    try {
      data = await listProfiles();
    } catch (e) {
      list.replaceChildren(h('p', { class: 'help' }, accountError(e)));
      return;
    }
    list.replaceChildren(...(data.profiles || []).map(row));
  };

  // Une ligne : le nom, puis renommer, nouveau mot de passe, supprimer.
  const row = (p) => {
    const error = h('p', { class: 'help', role: 'alert' });
    const name = h('input', { value: p.name, 'aria-label': 'Nom du profil' });
    const pass = password(p.has_password ? 'Nouveau mot de passe' : 'Son mot de passe (il n’en a pas)');
    const save = async (changes, done) => {
      try {
        await updateProfile(p.id, changes);
        error.textContent = '';
        toast(done);
        render();
      } catch (e) {
        error.textContent = accountError(e);
      }
    };
    const remove = async () => {
      const ok = await confirmDialog('Supprimer le profil « ' + p.name + ' » ?',
        'Ses discussions, ses projets, ses documents et ses images sont effacés du Mac. On ne peut pas revenir en arrière.');
      if (!ok) return;
      try {
        await deleteProfile(p.id);
        toast('Profil « ' + p.name + ' » supprimé.');
        render();
      } catch (e) {
        error.textContent = accountError(e);
      }
    };
    return h('div', { class: 'account-row' },
      h('div', { class: 'account-title' }, h('strong', null, p.name),
        p.admin ? ' · administrateur' : '', p.id === currentAccount().id ? ' · vous' : '',
        p.has_password ? '' : ' · sans mot de passe'),
      h('div', { class: 'inline' }, name,
        h('button', { class: 'button', type: 'button', onclick: () => save({ name: name.value.trim() }, 'Profil renommé.') },
          'Renommer')),
      h('div', { class: 'inline' }, pass,
        h('button', { class: 'button', type: 'button', onclick: () => save({ password: pass.value },
          'Mot de passe changé ; ses sessions ouvertes sont fermées.') }, 'Changer le mot de passe')),
      p.admin ? null : h('div', { class: 'actions' },
        h('button', { class: 'button danger', type: 'button', onclick: remove }, 'Supprimer ce profil')),
      error);
  };

  // Créer un profil.
  const newName = h('input', { placeholder: 'Nom du nouveau profil', autocomplete: 'off' });
  const newPass = password('Son mot de passe (8 caractères au moins)');
  const newError = h('p', { class: 'help', role: 'alert' });
  const create = async () => {
    try {
      const p = await createProfile(newName.value.trim(), newPass.value);
      newName.value = newPass.value = '';
      newError.textContent = '';
      toast('Profil « ' + p.name + ' » créé. Donnez-lui son mot de passe.');
      render();
    } catch (e) {
      newError.textContent = accountError(e);
    }
  };
  body.append(h('h3', null, 'Nouveau profil'),
    h('div', { class: 'field account-password' }, newName, newPass, newError),
    h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: create }, 'Créer le profil')));
  render();
}
