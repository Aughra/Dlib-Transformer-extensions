// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le carnet du portage (fork pour macOS, demande de l'humain du 2026-11-15 : « Albireo affiche lui-même les
    documents ») : le sommaire et les documents du dépôt du fork, que le service lit dans sa liste fermée
    (GET /albireo/v1/carnet, GET /albireo/v1/carnet/file).  L'adresse dit ce qui est lu : #/ pour le sommaire,
    #/chemin/du/document.md (et #ancre) pour un document.  Les Markdown sont mis en page par marked et nettoyés
    par DOMPurify ; leurs liens relatifs mènent aux autres documents du carnet.
*/

import { Marked } from '../vendor/marked/marked.esm.js';
import DOMPurify from '../vendor/dompurify/purify.es.mjs';

const main = document.getElementById('carnet');
const fileUrl = (path) => '/albireo/v1/carnet/file?path=' + encodeURIComponent(path);

function el(tag, props, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (k === 'class') e.className = v;
    else e.setAttribute(k, v);
  }
  for (const c of children.flat()) if (c != null) e.append(c);
  return e;
}

// Le thème, partagé avec la page de l'interface : système, clair, sombre.
(function themeButton() {
  const themes = ['auto', 'light', 'dark'];
  const names = { auto: 'Thème : système', light: 'Thème : clair', dark: 'Thème : sombre' };
  let choice = 'auto';
  try { choice = localStorage.getItem('docs.theme') || 'auto'; } catch (e) { /* stockage indisponible */ }
  const button = document.getElementById('theme');
  const apply = () => {
    const dark = choice === 'dark' || (choice === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches);
    document.documentElement.classList.toggle('dark', dark);
    button.textContent = names[choice];
  };
  button.addEventListener('click', () => {
    choice = themes[(themes.indexOf(choice) + 1) % themes.length];
    try { localStorage.setItem('docs.theme', choice); } catch (e) { /* stockage indisponible */ }
    apply();
  });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', apply);
  apply();
}());

async function showIndex() {
  document.title = 'Albireo · documents du portage';
  const r = await fetch('/albireo/v1/carnet', { cache: 'no-store' });
  const index = r.ok ? await r.json() : null;
  if (!index || !index.found) {
    main.replaceChildren(el('h1', null, 'Documents du portage'),
      el('p', { class: 'faint' }, 'Le dépôt du fork est introuvable : le service le cherche là où il a été construit, ' +
        'ou dans la variable ALBIREO_FORK_ROOT.'));
    return;
  }
  const out = [el('h1', null, 'Documents du portage'),
    el('p', { class: 'faint' }, 'Dlib Transformer Extensions sur macOS : Albireo, Pseudalis et leur portage, à lire depuis le canapé.')];
  for (const s of index.sections) {
    if (!s.entries.length) continue;
    out.push(el('div', { class: 'section-title' }, s.title));
    for (const e of s.entries) {
      out.push(el('a', { class: 'card', href: '#/' + e.path }, el('b', null, e.title), el('span', null, e.desc)));
    }
  }
  for (const b of index.blogs) {
    out.push(el('div', { class: 'section-title' }, b.title + ' (' + b.episodes.length + ' épisodes)'));
    out.push(el('ol', { class: 'episodes' }, b.episodes.map((e) =>
      el('li', { value: String(e.number) }, el('a', { href: '#/' + e.path }, e.title)))));
  }
  main.replaceChildren(...out);
  window.scrollTo(0, 0);
}

// Un lien ou une image relatifs au document, en chemin du dépôt ; null pour une adresse absolue ou une ancre.
function resolve(href, folder) {
  if (/^([a-z][a-z0-9+.-]*:|#|\/)/i.test(href)) return null;
  const u = new URL(href, 'http://carnet/' + folder);
  return { path: decodeURIComponent(u.pathname.slice(1)), anchor: u.hash };
}

const slug = (text) => text.trim().toLowerCase().replace(/[^\p{L}\p{N}\s-]/gu, '').replace(/\s/g, '-');

async function showDocument(path, anchor) {
  main.replaceChildren(el('p', { class: 'faint' }, 'Chargement…'));
  if (!path.endsWith('.md')) { location.href = fileUrl(path); return; }
  const r = await fetch(fileUrl(path), { cache: 'no-store' });
  if (!r.ok) {
    main.replaceChildren(el('h1', null, 'Introuvable'), el('p', { class: 'faint' }, path + ' n’est pas dans le carnet.'));
    return;
  }
  const folder = path.includes('/') ? path.slice(0, path.lastIndexOf('/') + 1) : '';
  const box = el('article');
  box.innerHTML = DOMPurify.sanitize(new Marked({ gfm: true }).parse(await r.text()));
  for (const a of box.querySelectorAll('a[href]')) {
    const href = a.getAttribute('href');
    const target = resolve(href, folder);
    if (target) a.href = target.path.endsWith('.md') ? '#/' + target.path + target.anchor : fileUrl(target.path);
    else if (href.startsWith('#')) a.href = '#/' + path + href;
    else if (/^https?:/i.test(href)) { a.target = '_blank'; a.rel = 'noopener'; }
  }
  for (const img of box.querySelectorAll('img[src]')) {
    const target = resolve(img.getAttribute('src'), folder);
    if (target) img.src = fileUrl(target.path);
  }
  for (const h of box.querySelectorAll('h1, h2, h3, h4')) h.id = slug(h.textContent);
  const h1 = box.querySelector('h1');
  document.title = (h1 ? h1.textContent : path) + ' · Albireo';
  main.replaceChildren(el('div', { class: 'path' }, path), box);
  const target = anchor && document.getElementById(decodeURIComponent(anchor));
  if (target) target.scrollIntoView(); else window.scrollTo(0, 0);
}

function route() {
  const hash = location.hash.replace(/^#\/?/, '');
  if (!hash) return showIndex();
  const at = hash.indexOf('#');
  const path = decodeURIComponent(at < 0 ? hash : hash.slice(0, at));
  return showDocument(path, at < 0 ? '' : hash.slice(at + 1));
}

document.getElementById('home').addEventListener('click', (e) => { e.preventDefault(); location.hash = '#/'; });
window.addEventListener('hashchange', route);
route();
