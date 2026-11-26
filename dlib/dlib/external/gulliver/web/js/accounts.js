// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet Profils des paramètres (fork pour macOS, 2026-11-22).  Chacun y voit sous quel profil il est
    connecté et y change son mot de passe.  L'administrateur (le premier profil) y gère les autres : en
    créer un avec son adresse mail, le renommer, changer son adresse, le supprimer avec toutes ses données.
    Depuis le 2026-11-23, chacun choisit son mot de passe par le lien « mot de passe oublié » reçu par mail :
    l'administrateur ne le choisit plus pour les autres.  Le service vérifie chaque droit
    (host/server.cpp, require_admin) ; la page ne fait que montrer ce qui est permis.  Par la passerelle de
    Cloudflare, la gestion des profils est refusée : seule la partie « mon mot de passe » est montrée.
*/

import { confirmDialog } from './dialogs.js';
import { h, toast } from './dom.js';
import { accountError, canSwitch, changePassword, createProfile, currentAccount, deleteProfile, listProfiles, logout,
  passwordInput, passwordRuleBox, switchTo, updateProfile } from './profile.js';
import { state } from './state.js';

const password = (placeholder, autocomplete) => passwordInput(h, placeholder, autocomplete);

export function accountsTab(body) {
  const me = currentAccount();
  if (!me) {
    body.append(h('p', { class: 'help' }, 'Aucune session ouverte : la page est en mode mémoire.'));
    return;
  }
  body.append(h('p', null, 'Connecté en tant que ', h('strong', null, me.name), me.email ? ' (' + me.email + ')' : '',
    me.admin ? ', administrateur.' : '.'),
    h('div', { class: 'actions' }, h('button', { class: 'button', type: 'button', onclick: () => logout() }, 'Se déconnecter')));

  // Mon mot de passe.  La règle (20 caractères au moins, minuscule + majuscule + chiffre +
  // caractère spécial) est affichée et validée en direct : le bouton ne s'allume que lorsque
  // le nouveau respecte la règle et que les deux saisies sont identiques.
  const current = password('Mot de passe actuel', 'current-password');
  const fresh = password('Nouveau mot de passe');
  const again = password('Le nouveau, une seconde fois');
  const rule = passwordRuleBox(h);
  const ownError = h('p', { class: 'help', role: 'alert' });
  const ownButton = h('button', { class: 'button', type: 'button', disabled: true }, 'Changer mon mot de passe');
  const refreshRule = () => {
    rule.update(fresh.value, again.value);
    ownButton.disabled = !rule.ok() || !current.value;
  };
  fresh.addEventListener('input', refreshRule);
  again.addEventListener('input', refreshRule);
  current.addEventListener('input', refreshRule);
  let busy = false;
  const changeOwn = async () => {
    if (busy || !rule.ok()) return;
    busy = true;
    ownButton.disabled = true;
    try {
      await changePassword(current.value, fresh.value);
      current.value = fresh.value = again.value = '';
      ownError.textContent = '';
      rule.update('', '');
      toast('Mot de passe changé. Vos autres appareils devront se reconnecter.');
    } catch (e) {
      ownError.textContent = accountError(e);
    } finally {
      busy = false;
      refreshRule();
    }
  };
  ownButton.onclick = changeOwn;
  body.append(h('h3', null, 'Mon mot de passe'),
    h('div', { class: 'field account-password' }, current.box, fresh.box, again.box, rule.el, ownError),
    h('div', { class: 'actions' }, ownButton));

  const remote = Boolean(state.status && state.status.remote);
  // TEMPORAIRE (essais de l'humain, 2026-11-26) : passer directement à un profil, sans mot de passe.
  if (canSwitch() && !remote) {
    const buttons = h('div', { class: 'actions' }, h('span', { class: 'help' }, '…'));
    const switchError = h('p', { class: 'help', role: 'alert' });
    body.append(h('h3', null, 'Passer directement à un profil (essai)'),
      h('p', { class: 'help' }, 'Bascule temporaire, sans mot de passe ni code, pour les essais. Elle sera retirée.'),
      buttons, switchError);
    listProfiles().then((data) => {
      buttons.replaceChildren(...(data.profiles || []).filter((p) => p.id !== me.id).map((p) => {
        const button = h('button', { class: 'button', type: 'button' }, 'Passer à ' + p.name + (p.admin ? ' (administrateur)' : ''));
        button.onclick = async () => {
          button.disabled = true;
          try {
            await switchTo(p.id);
          } catch (e) {
            button.disabled = false;
            switchError.textContent = accountError(e);
          }
        };
        return button;
      }));
    }).catch((e) => { switchError.textContent = accountError(e); });
  }

  if (!me.admin) return;
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

  // Une ligne : le nom, puis renommer, changer l'adresse, supprimer.
  const row = (p) => {
    const error = h('p', { class: 'help', role: 'alert' });
    const name = h('input', { value: p.name, 'aria-label': 'Nom du profil' });
    const email = h('input', { type: 'email', value: p.email || '', placeholder: 'Son adresse mail',
      'aria-label': 'Adresse mail du profil', autocomplete: 'off' });
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
        p.has_password ? '' : ' · sans mot de passe (il le choisit par « Mot de passe oublié »)',
        p.has_email ? '' : ' · sans adresse : il ne peut pas se connecter'),
      h('div', { class: 'inline' }, name,
        h('button', { class: 'button', type: 'button', onclick: () => save({ name: name.value.trim() }, 'Profil renommé.') },
          'Renommer')),
      h('div', { class: 'inline' }, email,
        h('button', { class: 'button', type: 'button', onclick: () => save({ email: email.value.trim() }, 'Adresse changée.') },
          'Changer l’adresse')),
      p.admin ? null : h('div', { class: 'actions' },
        h('button', { class: 'button danger', type: 'button', onclick: remove }, 'Supprimer ce profil')),
      error);
  };

  // Créer un profil.
  const newName = h('input', { placeholder: 'Nom du nouveau profil', autocomplete: 'off' });
  const newMail = h('input', { type: 'email', placeholder: 'Son adresse mail', autocomplete: 'off' });
  const newError = h('p', { class: 'help', role: 'alert' });
  const create = async () => {
    try {
      const p = await createProfile(newName.value.trim(), newMail.value.trim());
      newName.value = newMail.value = '';
      newError.textContent = '';
      toast('Profil « ' + p.name + ' » créé. À sa première connexion, il clique sur « Mot de passe oublié » ' +
        'et choisit son mot de passe par le lien reçu par mail.');
      render();
    } catch (e) {
      newError.textContent = accountError(e);
    }
  };
  body.append(h('h3', null, 'Nouveau profil'),
    h('div', { class: 'field account-password' }, newName, newMail, newError),
    h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: create }, 'Créer le profil')));
  render();
}
