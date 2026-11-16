// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le contexte de la prochaine question (fork pour macOS, demande de l'humain du 2026-11-15) : ce que la page
    enverrait au modèle si l'on envoyait maintenant ce qui est tapé, assemblé comme le fait une vraie question
    (systemPrompt, apiMessages, les outils de toolSet), compté par le service (POST /albireo/v1/count), message par
    message, dans une fenêtre.  Les images y sont nommées au lieu d'être envoyées.  À l'envoi, la page peut encore
    ajouter des extraits de documents trouvés pour la question, et résumer les échanges les plus anciens si la place
    manque (generation.js) : la fenêtre le dit.
*/

import * as api from './api.js';
import { apiMessages, systemPrompt, tokenEstimate } from './context.js';
import { dialog } from './dialogs.js';
import { formatNumber, h } from './dom.js';
import { state, ui } from './state.js';
import { chatRole, modelName, roleCapabilities, roleModel, toolSet } from './status.js';

const roles = { system: 'Système', user: 'Vous', assistant: 'Albireo', tool: 'Résultat d’outil' };

// Le texte d'un message : son contenu, ses images nommées, et les appels d'outils d'une réponse.
function textOf(m) {
  let text = '';
  if (typeof m.content === 'string') text = m.content;
  else if (Array.isArray(m.content)) {
    text = m.content.map((p) => p.type === 'text' ? p.text : p.type === 'image_url' ? '[image]' : '').join('\n');
  }
  if (m.reasoning_content) text = '[raisonnement]\n' + m.reasoning_content + (text ? '\n\n[réponse]\n' + text : '');
  if (Array.isArray(m.tool_calls) && m.tool_calls.length) {
    text += (text ? '\n\n' : '') + m.tool_calls.map((c) => '[appel] ' + (c.function ? c.function.name + ' ' +
      c.function.arguments : JSON.stringify(c))).join('\n');
  }
  return text;
}

export async function contextPreview() {
  const c = state.current;
  if (!c) return;
  const role = chatRole(c);
  const model = roleModel(role);
  const caps = roleCapabilities(role);
  const set = toolSet(c, caps);
  const typed = ui.input && typeof ui.input.value === 'string' ? ui.input.value.trim() : '';
  const view = Object.assign({}, c, {
    messages: c.messages.concat(typed ? [{ role: 'user', content: typed, id: 'preview' }] : [])
  });
  const messages = apiMessages(view, systemPrompt(view, caps, set), null, { read: false, url: () => null });
  dialog((box, close) => {
    const head = h('p', { class: 'preview-head' }, 'Comptage par le service…');
    const list = h('div', { class: 'preview-list' });
    box.append(h('h2', null, 'Contexte de la prochaine question'),
      h('p', { class: 'help' }, 'Ce que le modèle lirait si vous envoyiez maintenant ' +
        (typed ? 'ce que vous avez tapé' : 'une question (rien n’est tapé : la question manque à la fin)') +
        '. À l’envoi, Albireo peut encore ajouter des extraits de documents trouvés pour la question, et résumer ' +
        'les échanges les plus anciens si la place manque.'), head, list,
      h('div', { class: 'actions' }, h('button', { class: 'button primary', type: 'button', onclick: () => close(true) },
        'Fermer')));
    const show = (total, context) => {
      const estimate = total ? tokenEstimate(messages, total, 'albireo-' + role) : null;
      const parts = [model ? modelName(model) : 'aucun modèle'];
      if (total) parts.push(formatNumber(total) + ' jetons' + (context ? ' sur ' + formatNumber(context) + ' (' +
        Math.round(100 * total / context) + ' %)' : ''));
      parts.push(messages.length + ' messages');
      const tools = (set.definitions || []).map((d) => d.function.name);
      parts.push(tools.length ? 'outils : ' + tools.join(', ') : 'sans outils');
      head.textContent = parts.join(' · ');
      list.replaceChildren(...messages.map((m, i) => {
        const text = textOf(m);
        const tokens = estimate ? Math.round(estimate.of([m])) : 0;
        return h('details', { class: 'preview-item', open: i === messages.length - 1 ? true : null },
          h('summary', null, h('b', null, roles[m.role] || m.role), ' · ' + (tokens ? '≈ ' + formatNumber(tokens) +
            ' jetons · ' : '') + formatNumber(text.length) + ' caractères'),
          h('pre', { class: 'preview-text' }, text || '(vide)'));
      }));
    };
    show(0, 0);
    api.count('albireo-' + role, messages, set.definitions).then((r) => {
      if (r && typeof r.tokens === 'number') show(r.tokens, r.context || 0);
    }).catch(() => { head.textContent = 'Le service ne compte pas ce contexte maintenant (modèle absent ou occupé).'; });
  });
}
