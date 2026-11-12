// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Banc d'essai de la page (fork pour macOS) : pilote Chrome sans fenêtre par le protocole DevTools, ouvre la page du
// service d'essai, joue les étapes d'un scénario (expressions JavaScript évaluées dans la page, attentes, captures,
// rechargements, second onglet) et relève toutes les erreurs de la page.  Lancé par page_test.sh.
//   PAGE_TEST_WORK=<dossier> PAGE_URL=<adresse> node cdp.mjs scenario.json
// Sortie : une ligne « ok » ou « FAIL » par vérification, puis « no page errors » ou la liste des erreurs ; code 0
// seulement si tout passe.  Les captures vont dans <dossier>/shots, les fichiers exportés dans <dossier>/downloads.
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, rmSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = process.env.PAGE_TEST_WORK || dirname(fileURLToPath(import.meta.url));
const stepsFile = process.argv[2];
const steps = stepsFile ? JSON.parse(readFileSync(stepsFile, 'utf8')) : [];
// Un profil neuf à chaque scénario : sa base de discussions part vide.
const profile = join(here, 'chrome-profile');
rmSync(profile, { recursive: true, force: true });
mkdirSync(join(here, 'shots'), { recursive: true });
const port = 9333;
const url = process.env.PAGE_URL || 'https://localhost:18491/';

const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', [
  '--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--ignore-certificate-errors',
  '--no-first-run', '--no-default-browser-check', '--window-size=1280,900', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let version;
for (let i = 0; i < 50 && !version; ++i) {
  await sleep(200);
  try { version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); } catch {}
}
if (!version) { console.log('FAIL chrome did not start'); chrome.kill(); process.exit(2); }

const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener('open', r));
let id = 0; const pending = new Map(); const errors = [];
let session = null;
ws.addEventListener('message', ev => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); return; }
  if (m.method === 'Runtime.exceptionThrown') {
    const d = m.params.exceptionDetails;
    errors.push(`exception: ${d.exception?.description || d.text} @ ${d.url || ''}:${d.lineNumber}`);
  } else if (m.method === 'Runtime.consoleAPICalled' && ['error', 'assert'].includes(m.params.type)) {
    errors.push(`console.${m.params.type}: ${m.params.args.map(a => a.value ?? a.description).join(' ')}`);
  } else if (m.method === 'Log.entryAdded' && m.params.entry.level === 'error') {
    errors.push(`log: ${m.params.entry.text} ${m.params.entry.url || ''}`);
  }
});
const send = (method, params = {}, sid = session) => new Promise((res, rej) => {
  const msg = { id: ++id, method, params }; if (sid) msg.sessionId = sid;
  pending.set(msg.id, { res, rej }); ws.send(JSON.stringify(msg));
});

mkdirSync(join(here, 'downloads'), { recursive: true });
await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: join(here, 'downloads') }, null);
const { targetId } = await send('Target.createTarget', { url: 'about:blank' }, null);
({ sessionId: session } = await send('Target.attachToTarget', { targetId, flatten: true }, null));
await send('Runtime.enable'); await send('Log.enable'); await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
await send('Page.navigate', { url });
await sleep(4000);

const evaluate = async expr => {
  const r = await send('Runtime.evaluate', { expression: `(async () => { ${expr} })()`, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
};
// Les onglets : {"tab": 2} ouvre (la première fois) puis rend actif un autre onglet de la même page, {"tab": 1} revient.
const sessions = { 1: session };
async function openTab(n) {
  if (!sessions[n]) {
    const { targetId: t2 } = await send('Target.createTarget', { url: 'about:blank' }, null);
    const { sessionId } = await send('Target.attachToTarget', { targetId: t2, flatten: true }, null);
    sessions[n] = sessionId;
    session = sessionId;
    await send('Runtime.enable'); await send('Log.enable'); await send('Page.enable');
    await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
    await send('Page.navigate', { url });
    await sleep(4000);
  }
  session = sessions[n];
}
let failed = 0;
for (const s of steps) {
  try {
    if (s.tab) await openTab(s.tab);
    if (s.viewport) {
      const [width, height, mobile] = s.viewport;
      await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: mobile ? 3 : 1, mobile: !!mobile });
      await send('Page.reload', { ignoreCache: true }); await sleep(4000);
    }
    if (s.eval) {
      const v = await evaluate(s.eval);
      const ok = s.expect === undefined || JSON.stringify(v) === JSON.stringify(s.expect) || (s.expect === true && !!v);
      console.log(`${ok ? 'ok  ' : 'FAIL'} ${s.name || s.eval.slice(0, 60)} -> ${JSON.stringify(v)?.slice(0, 300)}`);
      if (!ok) ++failed;
    }
    if (s.wait) await sleep(s.wait);
    if (s.until) {
      let v; const t0 = Date.now();
      while (Date.now() - t0 < (s.timeout || 60000)) { v = await evaluate(s.until); if (v) break; await sleep(500); }
      console.log(`${v ? 'ok  ' : 'FAIL'} ${s.name || 'until'} -> ${JSON.stringify(v)?.slice(0, 300)}`);
      if (!v) ++failed;
    }
    if (s.shot) {
      const { data } = await send('Page.captureScreenshot', { format: 'png' });
      writeFileSync(join(here, 'shots', s.shot), Buffer.from(data, 'base64'));
    }
    if (s.reload) { await send('Page.reload', { ignoreCache: true }); await sleep(s.reload); }
  } catch (e) { console.log(`FAIL ${s.name || ''}: ${e.message}`); ++failed; }
}
console.log(errors.length ? `ERRORS (${errors.length}):\n  ${errors.join('\n  ')}` : 'no page errors');
ws.close(); chrome.kill();
process.exit(failed || errors.length ? 1 : 0);
