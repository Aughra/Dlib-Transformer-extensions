// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    La température de la puce du Mac dans le temps (fork pour macOS, 2026-11-22) : les relevés que le service prend
    toutes les cinq minutes (GET /gulliver/v1/thermal/history), tracés en SVG, relus chaque minute ; la
    valeur du moment vient de GET /gulliver/v1/speed.
*/
const SVG = 'http://www.w3.org/2000/svg';
const W = 820, H = 300, L = 40, R = 12, T = 12, B = 28;
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

async function showNow() {
  const now = document.getElementById('now');
  try {
    const s = await getJSON('/gulliver/v1/speed');
    if (typeof s.chip_temp_max !== 'number') {
      now.replaceChildren(Object.assign(document.createElement('span'), { className: 'faint', textContent: 'Aucune sonde ne répond.' }));
      return;
    }
    const level = s.thermal_state === 'fair' ? ' warm' : s.thermal_state === 'serious' || s.thermal_state === 'critical' ? ' hot' : '';
    const big = Object.assign(document.createElement('span'), { className: 'big' + level, textContent: decimal(s.chip_temp_max, 1) + ' °C' });
    const detail = Object.assign(document.createElement('span'), { className: 'detail',
      textContent: 'la sonde la plus chaude ; moyenne ' + decimal(s.chip_temp_mean, 1) + ' °C ; macOS : ' + (STATES[s.thermal_state] || s.thermal_state) });
    now.replaceChildren(big, detail);
  } catch (e) {
    now.replaceChildren(Object.assign(document.createElement('span'), { className: 'faint', textContent: 'Le service ne répond pas.' }));
  }
}

// Les points d'une courbe, coupée là où le service ne tournait pas (plus de trois relevés manquants).
function path(samples, key, x, y, period) {
  let d = '', last = null;
  for (const s of samples) {
    const gap = last === null || s.t - last > 3 * period;
    d += (gap ? 'M' : 'L') + x(s.t).toFixed(1) + ' ' + y(s[key]).toFixed(1) + ' ';
    last = s.t;
  }
  return d.trim();
}

async function showChart() {
  const box = document.getElementById('chart');
  let h;
  try {
    h = await getJSON('/gulliver/v1/thermal/history?minutes=' + minutes);
  } catch (e) {
    return;
  }
  const root = svg('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': 'La température de la puce dans le temps' });
  const samples = h.samples || [];
  const end = Math.floor(Date.now() / 1000), start = end - 60 * minutes;
  const x = (t) => L + (W - L - R) * (t - start) / (end - start);
  const hi = Math.max(50, ...samples.map((s) => s.max + 5)), lo = Math.min(20, ...samples.map((s) => s.mean - 5));
  const top = Math.ceil(hi / 10) * 10, bottom = Math.floor(lo / 10) * 10;
  const y = (c) => T + (H - T - B) * (top - c) / (top - bottom);
  for (let c = bottom; c <= top; c += 10) {
    root.append(svg('line', { class: 'grid', x1: L, x2: W - R, y1: y(c), y2: y(c) }));
    root.append(svg('text', { class: 'axis', x: L - 6, y: y(c) + 4, 'text-anchor': 'end' }, c + '°'));
  }
  for (let i = 0; i <= 4; ++i) {
    const t = start + (end - start) * i / 4;
    root.append(svg('text', { class: 'axis', x: x(t), y: H - 8, 'text-anchor': i === 0 ? 'start' : i === 4 ? 'end' : 'middle' }, clock(t)));
  }
  if (samples.length < 2) {
    root.append(svg('text', { class: 'empty', x: W / 2, y: H / 2, 'text-anchor': 'middle' },
      'Pas encore assez de relevés : le service en prend un toutes les cinq minutes.'));
  } else {
    root.append(svg('path', { class: 'mean', d: path(samples, 'mean', x, y, h.period) }));
    root.append(svg('path', { class: 'max', d: path(samples, 'max', x, y, h.period) }));
  }
  box.replaceChildren(root);
}

function refresh() {
  if (document.hidden) return;
  showNow();
  showChart();
}

document.getElementById('periods').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-minutes]');
  if (!b) return;
  minutes = Number(b.dataset.minutes);
  for (const o of document.querySelectorAll('#periods button')) o.classList.toggle('on', o === b);
  showChart();
});
document.addEventListener('visibilitychange', refresh);
refresh();
setInterval(refresh, 60000);
