// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    La température et la charge de la puce du Mac dans le temps (fork pour macOS, 2026-11-22) : les relevés que le
    service prend toutes les cinq minutes (GET /gulliver/v1/thermal/history : température, utilisation moyenne du
    processeur et du GPU sur chaque période), tracés en SVG, trois graphiques de même forme, chacun avec le haut, la
    moyenne et le bas de la période affichée ; relus chaque minute.  La valeur du moment vient de GET /gulliver/v1/speed.
*/
const SVG = 'http://www.w3.org/2000/svg';
const W = 820, H = 240, L = 44, R = 92, T = 12, B = 28;
let minutes = 60;

function svg(tag, attrs, text) {
  const e = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, v);
  if (text !== undefined) e.textContent = text;
  return e;
}

const decimal = (v, n) => v.toLocaleString('fr-FR', { minimumFractionDigits: n, maximumFractionDigits: n });
const clock = (t) => new Date(t * 1000).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
const STATES = { nominal: 'normal', fair: 'assez chaud', serious: 'chaud', critical: 'critique' };

async function getJSON(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) throw new Error(url + ' : ' + r.status);
  return r.json();
}

function faint(text) {
  return Object.assign(document.createElement('span'), { className: 'faint', textContent: text });
}

async function showNow() {
  const now = document.getElementById('now');
  try {
    const s = await getJSON('/gulliver/v1/speed');
    if (typeof s.chip_temp_max !== 'number') return now.replaceChildren(faint('Aucune sonde ne répond.'));
    const level = s.thermal_state === 'fair' ? ' warm' : s.thermal_state === 'serious' || s.thermal_state === 'critical' ? ' hot' : '';
    const big = Object.assign(document.createElement('span'), { className: 'big' + level, textContent: decimal(s.chip_temp_max, 1) + ' °C' });
    const detail = Object.assign(document.createElement('span'), { className: 'detail',
      textContent: 'la sonde la plus chaude ; moyenne ' + decimal(s.chip_temp_mean, 1) + ' °C ; macOS : ' + (STATES[s.thermal_state] || s.thermal_state) });
    now.replaceChildren(big, detail);
  } catch (e) {
    now.replaceChildren(faint('Le service ne répond pas.'));
  }
}

// Les points d'une courbe, coupée là où le service ne tournait pas (plus de trois relevés manquants) ou la valeur manque.
function path(samples, key, x, y, period) {
  let d = '', last = null;
  for (const s of samples) {
    if (typeof s[key] !== 'number') { last = null; continue; }
    const gap = last === null || s.t - last > 3 * period;
    d += (gap ? 'M' : 'L') + x(s.t).toFixed(1) + ' ' + y(s[key]).toFixed(1) + ' ';
    last = s.t;
  }
  return d.trim();
}

/*
    Un graphique : les courbes lines ([clé, classe]) des relevés, une échelle de bottom à top (unit), et, pour la clé
    stat, le haut, la moyenne et le bas de la période en pointillés, avec leur valeur à droite.
*/
function chart(box, samples, period, start, end, { lines, stat, unit, bottom, top, step, digits }) {
  const root = svg('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img' });
  const x = (t) => L + (W - L - R) * (t - start) / (end - start);
  const y = (v) => T + (H - T - B) * (top - v) / (top - bottom);
  for (let v = bottom; v <= top; v += step) {
    root.append(svg('line', { class: 'grid', x1: L, x2: W - R, y1: y(v), y2: y(v) }));
    root.append(svg('text', { class: 'axis', x: L - 6, y: y(v) + 4, 'text-anchor': 'end' }, v + (unit === '%' ? ' %' : '°')));
  }
  for (let i = 0; i <= 4; ++i) {
    const t = start + (end - start) * i / 4;
    root.append(svg('text', { class: 'axis', x: x(t), y: H - 8, 'text-anchor': i === 0 ? 'start' : i === 4 ? 'end' : 'middle' }, clock(t)));
  }
  const values = samples.map((s) => s[stat]).filter((v) => typeof v === 'number');
  if (values.length < 2) {
    root.append(svg('text', { class: 'empty', x: (W - R + L) / 2, y: H / 2, 'text-anchor': 'middle' },
      'Pas encore assez de relevés : le service en prend un toutes les cinq minutes.'));
    return box.replaceChildren(root);
  }
  for (const [key, cls] of lines) root.append(svg('path', { class: cls, d: path(samples, key, x, y, period) }));
  // Le haut, la moyenne et le bas de la période, en pointillés, leur valeur à droite (écartées si elles se touchent).
  const mean = values.reduce((a, b) => a + b, 0) / values.length;
  const marks = [['haut', Math.max(...values)], ['moyenne', mean], ['bas', Math.min(...values)]];
  let lastY = -100;
  for (const [label, v] of marks) {
    const yy = y(v);
    root.append(svg('line', { class: 'stat', x1: L, x2: W - R, y1: yy, y2: yy }));
    const ty = Math.max(yy + 4, lastY + 13);
    root.append(svg('text', { class: 'stat-label', x: W - R + 6, y: ty }, label + ' ' + decimal(v, digits) + (unit === '%' ? ' %' : ' °C')));
    lastY = ty;
  }
  box.replaceChildren(root);
}

async function showCharts() {
  let h;
  try {
    h = await getJSON('/gulliver/v1/thermal/history?minutes=' + minutes);
  } catch (e) {
    return;
  }
  const samples = h.samples || [];
  const end = Math.floor(Date.now() / 1000), start = end - 60 * minutes;
  const temps = samples.flatMap((s) => [s.max, s.mean]).filter((v) => typeof v === 'number');
  const hi = Math.max(50, ...temps.map((v) => v + 5)), lo = Math.min(20, ...temps.map((v) => v - 5));
  chart(document.getElementById('chart-temp'), samples, h.period, start, end, {
    lines: [['mean', 'mean'], ['max', 'max']], stat: 'max', unit: '°C',
    bottom: Math.floor(lo / 10) * 10, top: Math.ceil(hi / 10) * 10, step: 10, digits: 1 });
  chart(document.getElementById('chart-cpu'), samples, h.period, start, end, {
    lines: [['cpu', 'load']], stat: 'cpu', unit: '%', bottom: 0, top: 100, step: 25, digits: 0 });
  chart(document.getElementById('chart-gpu'), samples, h.period, start, end, {
    lines: [['gpu', 'load']], stat: 'gpu', unit: '%', bottom: 0, top: 100, step: 25, digits: 0 });
}

function refresh() {
  if (document.hidden) return;
  showNow();
  showCharts();
}

document.getElementById('periods').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-minutes]');
  if (!b) return;
  minutes = Number(b.dataset.minutes);
  for (const o of document.querySelectorAll('#periods button')) o.classList.toggle('on', o === b);
  showCharts();
});
document.addEventListener('visibilitychange', refresh);
refresh();
setInterval(refresh, 60000);
