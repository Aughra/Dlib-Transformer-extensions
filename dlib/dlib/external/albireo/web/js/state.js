// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// L'état de la page que partagent ses modules (fork pour macOS) : les réglages par défaut, les discussions, les projets
// et la vue en cours (state), et les éléments de la page que plusieurs parties modifient (ui).

export const defaults = {
  theme: 'auto', language: 'auto', userName: '', defaultRole: 'general', temperature: null, topP: null, topK: null,
  maxTokens: 0, sendOnEnter: true, systemPrompt: '', sidebar: true,
  memoryEnabled: true, memorySensitive: false, memory: '', memoryUpdated: 0, memoryLanguage: '', memoryPinned: '',
  storagePersisted: null
};

export const state = {
  settings: Object.assign({}, defaults),
  conversations: [],
  projects: [],
  current: null,
  view: 'chat',
  projectId: null,
  incognito: false,
  pending: [],
  status: null,
  catalog: null,
  search: '',
  statusError: false
};

export const ui = {};

// Les fonctions du service, leurs icônes, les couleurs des projets, et le nombre maximal d'étapes de l'agent.
export const roles = ['general', 'code', 'security'];
export const roleIcons = { general: 'compass', code: 'code', security: 'shield' };
export const projectColors = ['#5b8cff', '#f2b544', '#3fb68b', '#e5534b', '#a371f7', '#2fb9d4', '#e08a3c', '#8a90aa'];
// Le nombre maximal d'étapes d'une réponse avec outils : chaque étape est une requête au modèle, puis les appels qu'il
// demande.
export const maxAgentSteps = 100;
