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
