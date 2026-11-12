// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La vérification des modules de la page par ESLint (fork pour macOS), lancée par page_test.sh : noms non définis,
// imports réaffectés, déclarations en double, et, en avertissement, les noms jamais utilisés.
import globals from 'globals';
export default [{
  files: ['**/*.js'],
  languageOptions: {
    ecmaVersion: 'latest', sourceType: 'module',
    globals: { ...globals.browser, hljs: 'readonly', katex: 'readonly' }
  },
  rules: {
    'no-undef': 'error', 'no-redeclare': 'error', 'no-const-assign': 'error', 'no-import-assign': 'error',
    'no-unused-vars': ['warn', { args: 'none', caughtErrors: 'none' }]
  }
}];
