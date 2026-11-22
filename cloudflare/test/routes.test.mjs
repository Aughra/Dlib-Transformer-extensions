// Tests de la liste de la passerelle : node --test cloudflare/test/
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { allowed, isApiPath } from '../lib/routes.js';

test('le calcul et les données passent', () => {
  for (const [m, p] of [
    ['POST', '/v1/chat/completions'], ['POST', '/v1/embeddings'], ['GET', '/v1/models'],
    ['POST', '/gulliver/v1/extract'], ['POST', '/gulliver/v1/count'], ['POST', '/gulliver/v1/web/search'],
    ['GET', '/gulliver/v1/status'], ['HEAD', '/gulliver/v1/status'], ['GET', '/gulliver/v1/models'],
    ['GET', '/gulliver/v1/profiles'], ['POST', '/gulliver/v1/profiles'], ['DELETE', '/gulliver/v1/profiles/abc'],
    ['GET', '/gulliver/v1/profile/conversations'], ['PUT', '/gulliver/v1/profile/conversations/42'],
    ['POST', '/gulliver/v1/profile/search'], ['POST', '/gulliver/v1/profile/documents/d1/index'],
    ['GET', '/gulliver/v1/profile/images/7/blob'], ['GET', '/gulliver/v1/documents/rapport.pdf'],
    ['GET', '/gulliver/v1/settings'], ['GET', '/gulliver/v1/licenses/dlib.txt'], ['GET', '/gulliver/v1/carnet/file'],
    ['GET', '/gulliver/v1/thermal/history'], ['GET', '/gulliver/v1/models/advice'],
  ]) assert.ok(allowed(m, p), m + ' ' + p);
});

test("l'administration est refusée", () => {
  for (const [m, p] of [
    ['PUT', '/gulliver/v1/settings'], ['POST', '/gulliver/v1/restart'], ['GET', '/gulliver/v1/logs'],
    ['POST', '/gulliver/v1/models/upload'], ['POST', '/gulliver/v1/models/x/download'],
    ['DELETE', '/gulliver/v1/models/x'], ['POST', '/gulliver/v1/models/x/activate'],
    ['POST', '/gulliver/v1/models/refresh'], ['POST', '/gulliver/v1/models/import'], ['POST', '/gulliver/v1/import'],
    ['GET', '/gulliver/v1/hub/search'], ['POST', '/gulliver/v1/hub/add'], ['GET', '/gulliver/v1/library'],
    ['POST', '/gulliver/v1/library/load'], ['GET', '/gulliver/v1/reserve'], ['DELETE', '/gulliver/v1/reserve'],
    ['POST', '/gulliver/v1/engines/benchmark'], ['POST', '/gulliver/v1/relay/test'],
    ['POST', '/gulliver/v1/settings/profiles'], ['DELETE', '/gulliver/v1/settings/profiles'],
    ['DELETE', '/gulliver/v1/documents'], ['POST', '/gulliver/v1/une-route-future'], ['GET', '/gulliver/v1'],
  ]) assert.equal(allowed(m, p), null, m + ' ' + p);
});

test('les chemins déguisés sont refusés', () => {
  for (const p of ['/gulliver/v1/profile/../settings', '/gulliver/v1/profile//x', '/gulliver/v1/profile/%2e%2e/settings',
    '/gulliver/v1/profiles/a%2Fb', '/gulliver/v1/profile/a\\b', '/gulliver/v1/profile/%5c'])
    assert.equal(allowed('GET', p), null, p);
});

test('les fichiers de la page ne sont pas l’interface', () => {
  for (const p of ['/', '/index.html', '/js/app.js', '/v1x', '/gulliver/v1x']) assert.equal(isApiPath(p), false, p);
  for (const p of ['/v1/models', '/gulliver/v1/status']) assert.equal(isApiPath(p), true, p);
});

test("chaque route de l'interface du service est classée", () => {
  // Toute route d'openapi.json doit être soit permise, soit refusée en connaissance de cause : la liste
  // ci-dessous est celle des routes refusées. Une route nouvelle du service fait échouer ce test.
  const here = fileURLToPath(new URL('.', import.meta.url));
  const spec = JSON.parse(readFileSync(here + '../../dlib/dlib/external/gulliver/web/openapi.json', 'utf8'));
  const refused = new Set([
    'PUT /gulliver/v1/settings', 'POST /gulliver/v1/restart', 'GET /gulliver/v1/logs', 'POST /gulliver/v1/import',
    'POST /gulliver/v1/models/import', 'POST /gulliver/v1/models/refresh', 'POST /gulliver/v1/models/upload',
    'DELETE /gulliver/v1/models/{id}', 'POST /gulliver/v1/models/{id}/activate', 'POST /gulliver/v1/models/{id}/deactivate',
    'POST /gulliver/v1/models/{id}/download', 'DELETE /gulliver/v1/models/{id}/download', 'POST /gulliver/v1/models/{id}/verify',
    'POST /gulliver/v1/hub/add', 'GET /gulliver/v1/hub/files', 'GET /gulliver/v1/hub/recent', 'GET /gulliver/v1/hub/search',
    'GET /gulliver/v1/library', 'POST /gulliver/v1/library/load', 'GET /gulliver/v1/reserve', 'DELETE /gulliver/v1/reserve',
    'POST /gulliver/v1/engines/benchmark', 'POST /gulliver/v1/relay/test',
    'POST /gulliver/v1/settings/profiles', 'DELETE /gulliver/v1/settings/profiles',
  ]);
  for (const [path, ops] of Object.entries(spec.paths)) {
    for (const method of Object.keys(ops).filter((k) => ['get', 'post', 'put', 'delete', 'patch'].includes(k))) {
      const key = method.toUpperCase() + ' ' + path;
      const concrete = path.replace(/\{[^}]+\}/g, 'x');
      const ok = allowed(method.toUpperCase(), concrete);
      if (refused.has(key)) assert.equal(ok, null, key + ' devrait être refusée');
      else assert.ok(ok, key + ' : ni permise ni dans la liste des refus');
    }
  }
});

test('les en-têtes de sécurité suivent ceux du service', async () => {
  const { securityHeaders } = await import('../lib/headers.js');
  assert.equal(securityHeaders('/index.html')['X-Frame-Options'], 'DENY');
  assert.match(securityHeaders('/index.html')['Content-Security-Policy'], /frame-ancestors 'none'/);
  assert.equal(securityHeaders('/sandbox.html')['X-Frame-Options'], 'SAMEORIGIN');
  assert.equal(securityHeaders('/sandbox')['X-Frame-Options'], 'SAMEORIGIN');
  assert.match(securityHeaders('/sandbox.html')['Content-Security-Policy'], /'unsafe-eval'/);
  assert.equal(securityHeaders('/js/sandbox.js')['Cross-Origin-Resource-Policy'], 'cross-origin');
  // Les politiques recopiées doivent être celles du service, mot pour mot.
  const here = fileURLToPath(new URL('.', import.meta.url));
  const server = readFileSync(here + '../../dlib/dlib/external/gulliver/host/server.cpp', 'utf8').replace(/"\s*\n\s*"/g, '');
  for (const p of ['/index.html', '/docs/x.html', '/sandbox.html'])
    assert.ok(server.includes(securityHeaders(p)['Content-Security-Policy']), 'politique de ' + p + ' différente du service');
});
