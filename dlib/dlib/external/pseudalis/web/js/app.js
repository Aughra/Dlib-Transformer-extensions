// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// L'application web de Pseudalis : des fichiers et des textes protégés et restaurés par le service local.
(function () {
  'use strict';

  const texts = window.PSEUDALIS_TEXTS;
  let language = 'en';
  // La carte du texte est prête : ses marques suivent la langue (renderMarks()).
  let textReady = false;

  /* La langue de la page : celle choisie dans son menu, sinon la première langue du navigateur,
     qui suit le système à moins que son utilisateur n'en ait décidé autrement, que l'application
     connaisse, sinon l'anglais. */
  function pick() {
    let saved = null;
    try { saved = localStorage.getItem('pseudalis.language'); } catch (e) { /* stockage indisponible */ }
    if (saved && texts[saved]) return saved;
    const wanted = navigator.languages && navigator.languages.length ? navigator.languages
      : [navigator.language || 'en'];
    for (let i = 0; i < wanted.length; ++i) {
      const code = String(wanted[i] || '').slice(0, 2).toLowerCase();
      if (texts[code]) return code;
    }
    return 'en';
  }

  function t(key, values) {
    let s = (texts[language] && texts[language][key]) || texts.en[key] || key;
    if (values) Object.keys(values).forEach(function (k) { s = s.split('{' + k + '}').join(values[k]); });
    return s;
  }

  /* Un texte qui compte n choses : sa forme au singulier, key_one, pour une chose, et pour aucune en
     français, sinon sa forme au pluriel. */
  function tn(key, n, values) {
    const one = n === 1 || (language === 'fr' && n === 0);
    const k = one && texts[language] && texts[language][key + '_one'] ? key + '_one' : key;
    return t(k, Object.assign({ n: n }, values || {}));
  }

  function translate() {
    document.documentElement.lang = language;
    document.querySelectorAll('[data-t]').forEach(function (e) { e.textContent = t(e.getAttribute('data-t')); });
    if (textReady) renderMarks();
    refreshStatus();
    if (admin) {
      renderRecords();
      renderTerms();
    }
  }

  /* Le jeton d'administration, que l'icône donne dans le fragment de l'adresse qu'elle ouvre : le
     navigateur n'envoie jamais un fragment, et l'adresse le perd aussitôt. */
  let admin = '';
  (function () {
    const m = /(?:^#|&)admin=([0-9A-Za-z_-]+)/.exec(location.hash || '');
    try {
      if (m) sessionStorage.setItem('pseudalis.admin', m[1]);
      admin = sessionStorage.getItem('pseudalis.admin') || '';
    } catch (e) {
      admin = m ? m[1] : '';
    }
    if (m) history.replaceState(null, '', location.pathname + location.search);
  })();

  function adminFetch(route, options) {
    const o = Object.assign({ cache: 'no-store' }, options || {});
    o.headers = Object.assign({}, o.headers || {}, { 'X-Pseudalis-Admin': admin });
    return fetch(route, o);
  }

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  /* Une fenêtre de l'application par-dessus la page, à la place de celles du navigateur : son titre, son
     texte et ses boutons, {title, text, confirm, cancel, danger}.  Échap et un clic à côté la ferment comme
     le fait son bouton d'annulation ; le focus revient là où il était.  Se résout avec true pour le bouton
     de confirmation. */
  function dialog(o) {
    return new Promise(function (resolve) {
      if (typeof HTMLDialogElement !== 'function') {
        resolve(o.cancel ? window.confirm(o.text) : (window.alert(o.text), true));
        return;
      }
      const previous = document.activeElement;
      const d = el('dialog', 'modal');
      d.setAttribute('aria-labelledby', 'modal-title');
      d.setAttribute('aria-describedby', 'modal-text');
      if (o.cancel) d.setAttribute('role', 'alertdialog');
      const title = el('h3', '', o.title || '');
      title.id = 'modal-title';
      const text = el('p', '', o.text || '');
      text.id = 'modal-text';
      const buttons = el('div', 'buttons');
      let result = false;
      const close = function (value) {
        result = value;
        d.close();
      };
      let no = null;
      if (o.cancel) {
        no = el('button', 'secondary', o.cancel);
        no.type = 'button';
        no.addEventListener('click', function () { close(false); });
        buttons.appendChild(no);
      }
      const yes = el('button', o.danger ? 'danger' : '', o.confirm || t('btn_close'));
      yes.type = 'button';
      yes.addEventListener('click', function () { close(true); });
      buttons.appendChild(yes);
      d.appendChild(title);
      d.appendChild(text);
      d.appendChild(buttons);
      d.addEventListener('click', function (e) {
        // Un clic sur ses boutons, y compris depuis le clavier (en 0, 0), leur revient.
        if (e.target !== d) return;
        const r = d.getBoundingClientRect();
        if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) close(!o.cancel);
      });
      d.addEventListener('close', function () {
        d.remove();
        if (previous && previous.focus) previous.focus();
        resolve(result);
      });
      document.body.appendChild(d);
      d.showModal();
      // Le choix qui ne change rien prend le focus en premier.
      (no || yes).focus();
    });
  }

  // Un échec annoncé dans une fenêtre de l'application.
  function failureDialog(text) {
    return dialog({ title: t('dialog_error'), text: text, confirm: t('btn_close') });
  }

  // ---------------------------------------------------------------------------------------
  // L'état du service.

  function pill(label, value, ok) {
    const p = el('span', 'pill' + (ok === true ? ' ok' : ok === false ? ' off' : ''));
    p.appendChild(document.createTextNode(label + ' '));
    p.appendChild(el('b', '', value));
    return p;
  }

  async function refreshStatus() {
    const box = document.getElementById('status');
    try {
      const r = await fetch('v1/info', { cache: 'no-store' });
      const info = await r.json();
      box.textContent = '';
      document.getElementById('version').textContent = [info.edition, info.version].filter(Boolean).join(' ');
      box.appendChild(pill(t('s_version'), info.version));
      box.appendChild(pill(t('s_engine'), (info.engine && info.engine.variant) || '?'));
      const d = info.drive || {};
      box.appendChild(pill(t('s_drive'), d.mounted ? d.letter + ':' : t('s_drive_off'), !!d.mounted));
      const a = info.albireo || {};
      box.appendChild(pill(t('s_albireo'), a.found ? a.model : t('s_albireo_off'), !!a.found));
      box.appendChild(pill(t('s_records'), String(info.records || 0)));
      const h = info.history || {};
      if (h.limit) box.appendChild(pill(t('s_history'), size(h.bytes || 0) + ' / ' + size(h.limit)));
    } catch (e) {
      box.textContent = '';
      box.appendChild(pill('', t('s_offline'), false));
    }
  }

  // ---------------------------------------------------------------------------------------
  // Les fichiers.

  function bytesOf(base64) {
    const s = atob(base64);
    const b = new Uint8Array(s.length);
    for (let i = 0; i < s.length; ++i) b[i] = s.charCodeAt(i);
    return b;
  }

  /* L'échec d'une requête dans la langue de la page : le texte de son code, avec ses valeurs
     (une date écrite à la manière de la langue, des tailles en mégaoctets), sinon le texte
     anglais du service. */
  function errorText(body, status) {
    if (body && body.code && texts.en['err_' + body.code]) {
      const values = { e: body.error || '' };
      const args = body.args || {};
      Object.keys(args).forEach(function (k) { values[k] = String(args[k]); });
      if (/^\d{4}-\d{2}-\d{2}$/.test(values.date || '')) {
        const d = new Date(values.date + 'T12:00:00');
        if (!isNaN(d.getTime())) values.date = d.toLocaleDateString(language);
      }
      const sizes = body.sizes || {};
      Object.keys(sizes).forEach(function (k) { values[k] = String(Math.ceil((Number(sizes[k]) || 0) / 1048576)); });
      return t('err_' + body.code, values);
    }
    if (body && body.error) return body.error;
    return t('err_http', { status: status });
  }

  /* Envoie un élément, {name, read}, dont read() donne les octets : un fichier, ou l'archive ZIP d'un
     dossier, que le service traite en entier et rend sous forme d'archive. */
  async function send(item, route, list, describe) {
    const li = el('li');
    const busy = el('span', 'busy');
    const grow = el('div', 'grow');
    grow.appendChild(el('div', 'name', item.name));
    const detail = el('div', 'detail', t('working'));
    grow.appendChild(detail);
    li.appendChild(busy);
    li.appendChild(grow);
    list.prepend(li);
    let data;
    try {
      data = await item.read();
    } catch (e) {
      busy.remove();
      detail.className = 'error';
      detail.textContent = t(e && e.message === 'folder_too_big' ? 'folder_too_big' : 'read_failed');
      return;
    }
    try {
      const r = await fetch(route + '&name=' + encodeURIComponent(item.name), {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: data
      });
      const body = await r.json().catch(function () { return null; });
      busy.remove();
      if (!r.ok || !body) {
        detail.className = 'error';
        detail.textContent = errorText(body, r.status);
        return;
      }
      grow.firstChild.textContent = body.name;
      detail.textContent = describe(body);
      const url = URL.createObjectURL(new Blob([bytesOf(body.data)], { type: 'application/octet-stream' }));
      const a = el('a', 'button', t('download'));
      a.href = url;
      a.download = body.name;
      li.appendChild(a);
    } catch (e) {
      // La requête n'a pas atteint le service, ou sa réponse a été coupée.
      busy.remove();
      detail.className = 'error';
      detail.textContent = t('s_offline');
    }
  }

  // ---------------------------------------------------------------------------------------
  // Les dossiers : leurs fichiers et leurs dossiers, avec leurs chemins, dans une archive ZIP.

  const crcTable = (function () {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; ++n) {
      let c = n;
      for (let k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      table[n] = c >>> 0;
    }
    return table;
  })();

  function crc32(bytes) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < bytes.length; ++i) c = crcTable[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }

  /* Une archive ZIP des entrées {path, bytes, folder}, stockées sans compression, leurs noms en
     UTF-8 : le service la lit, et écrit compressée l'archive qu'il rend. */
  function zipOf(entries) {
    const encoder = new TextEncoder();
    const now = new Date();
    const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
    const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
    const parts = [];
    const central = [];
    let offset = 0, size = 0;
    entries.forEach(function (e) {
      const name = encoder.encode(e.path);
      const crc = crc32(e.bytes);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);
      local.setUint16(6, 0x0800, true);
      local.setUint16(10, time, true);
      local.setUint16(12, date, true);
      local.setUint32(14, crc, true);
      local.setUint32(18, e.bytes.length, true);
      local.setUint32(22, e.bytes.length, true);
      local.setUint16(26, name.length, true);
      parts.push(local.buffer, name, e.bytes);
      const c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true);
      c.setUint16(4, 20, true);
      c.setUint16(6, 20, true);
      c.setUint16(8, 0x0800, true);
      c.setUint16(12, time, true);
      c.setUint16(14, date, true);
      c.setUint32(16, crc, true);
      c.setUint32(20, e.bytes.length, true);
      c.setUint32(24, e.bytes.length, true);
      c.setUint16(28, name.length, true);
      c.setUint32(38, e.folder ? 0x10 : 0, true);
      c.setUint32(42, offset, true);
      central.push(c.buffer, name);
      offset += 30 + name.length + e.bytes.length;
      size += 46 + name.length;
    });
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, size, true);
    end.setUint32(16, offset, true);
    return new Blob(parts.concat(central, [end.buffer]), { type: 'application/zip' });
  }

  // Les noms qu'un dossier contient pour le système ou pour un programme au travail, laissés de côté.
  function passed(name) {
    const n = name.toLowerCase();
    return n === 'desktop.ini' || n === 'thumbs.db' || n === 'ehthumbs.db' || n === '.ds_store' ||
      n.indexOf('~$') === 0;
  }

  // L'archive des éléments {path, file} ou {path, folder: true}, sous forme d'ArrayBuffer.
  async function archiveOf(items) {
    // Les limites d'une archive ZIP sans ses extensions 64 bits.
    if (items.length > 65535) throw new Error('folder_too_big');
    const entries = [];
    let total = 0;
    for (let i = 0; i < items.length; ++i) {
      const it = items[i];
      if (it.folder) {
        entries.push({ path: it.path, bytes: new Uint8Array(0), folder: true });
        continue;
      }
      total += it.file.size + 76 + 2 * it.path.length * 3;
      if (total > 0xFFF00000) throw new Error('folder_too_big');
      entries.push({ path: it.path, bytes: new Uint8Array(await it.file.arrayBuffer()) });
    }
    return zipOf(entries).arrayBuffer();
  }

  // Les fichiers et les dossiers sous un dossier déposé, le dossier lui-même en premier.
  async function collect(entry, prefix, out) {
    if (passed(entry.name)) return;
    if (entry.isFile) {
      const file = await new Promise(function (resolve, reject) { entry.file(resolve, reject); });
      out.push({ path: prefix + entry.name, file: file });
      return;
    }
    const path = prefix + entry.name + '/';
    out.push({ path: path, folder: true });
    const reader = entry.createReader();
    for (;;) {
      const batch = await new Promise(function (resolve, reject) { reader.readEntries(resolve, reject); });
      if (!batch.length) break;
      for (let i = 0; i < batch.length; ++i) await collect(batch[i], path, out);
    }
  }

  function fileItem(file) {
    return { name: file.name, read: function () { return file.arrayBuffer(); } };
  }

  function folderItem(entry) {
    return {
      name: entry.name + '.zip',
      read: async function () {
        const items = [];
        await collect(entry, '', items);
        return archiveOf(items);
      }
    };
  }

  // Un dossier choisi dans la boîte de dialogue : ses fichiers, qui gardent leurs chemins sous lui.
  function chosenFolderItem(files) {
    const top = (files[0].webkitRelativePath || files[0].name).split('/')[0];
    return {
      name: top + '.zip',
      read: function () {
        return archiveOf(files.filter(function (f) { return !passed(f.name); }).map(function (f) {
          return { path: f.webkitRelativePath || f.name, file: f };
        }));
      }
    };
  }

  function protectDetail(body) {
    const report = body.report || {};
    let s = tn('values', report.total || 0);
    if (report.tags === false) s += ', ' + t('protected_untagged');
    const dropped = (report.dropped || []).length;
    if (dropped) s += ', ' + tn('dropped', dropped);
    if (report.restorable === false) s += ', ' + t('not_restorable');
    return s;
  }

  /* Une restauration à partir des étiquettes indique le jour où l'original a été oublié, s'il l'a été, et
     les images retirées, que seuls leurs originaux pourraient rendre. */
  function restoreDetail(body) {
    if (body.restored === 'original') return t('restored_original');
    if (body.restored === 'picture') return t('restored_picture');
    const report = body.report || {};
    let s = tn('restored_tags', (report.tags_restored || 0) + (report.substitutes_restored || 0));
    const forgotten = report.original_forgotten;
    if (forgotten && /^\d{4}-\d{2}-\d{2}$/.test(forgotten.date || '')) {
      const p = forgotten.date.split('-').map(Number);
      s += ', ' + t('restored_forgotten', { date: new Date(p[0], p[1] - 1, p[2]).toLocaleDateString(language) });
    }
    if (report.pictures_removed) s += ', ' + tn('pictures_removed', report.pictures_removed);
    return s;
  }

  function wire(dropId, inputId, folderId, run) {
    const drop = document.getElementById(dropId);
    const input = document.getElementById(inputId);
    const folder = document.getElementById(folderId);
    input.addEventListener('change', function () {
      Array.from(input.files).forEach(function (f) { run(fileItem(f)); });
      input.value = '';
    });
    folder.addEventListener('change', function () {
      const files = Array.from(folder.files);
      folder.value = '';
      if (files.length) run(chosenFolderItem(files));
    });
    ['dragenter', 'dragover'].forEach(function (k) {
      drop.addEventListener(k, function (e) { e.preventDefault(); drop.classList.add('over'); });
    });
    ['dragleave', 'drop'].forEach(function (k) {
      drop.addEventListener(k, function () { drop.classList.remove('over'); });
    });
    drop.addEventListener('drop', function (e) {
      e.preventDefault();
      // Les entrées sont prises pendant l'événement : un dossier parmi elles devient une archive.
      const files = Array.from(e.dataTransfer.files);
      const entries = Array.from(e.dataTransfer.items || []).filter(function (it) { return it.kind === 'file'; })
        .map(function (it) { return it.webkitGetAsEntry ? it.webkitGetAsEntry() : null; });
      if (entries.length !== files.length || !entries.every(Boolean)) {
        files.forEach(function (f) { run(fileItem(f)); });
        return;
      }
      entries.forEach(function (entry, i) { run(entry.isDirectory ? folderItem(entry) : fileItem(files[i])); });
    });
  }

  wire('protect-drop', 'protect-input', 'protect-folder', function (item) {
    const keep = document.getElementById('keep').checked;
    const tags = document.getElementById('tags').checked;
    send(item, 'v1/anonymize?format=json&source=web&record=' + (keep ? 'true' : 'false') + '&tags=' +
      (tags ? 'true' : 'false'), document.getElementById('protect-results'), protectDetail).then(refreshStatus);
  });

  // Sans étiquettes et sans les originaux gardés, les fichiers protégés ne peuvent pas revenir : la
  // carte le dit.
  function protectOptions() {
    document.getElementById('no-restore').hidden =
      document.getElementById('keep').checked || document.getElementById('tags').checked;
  }
  document.getElementById('keep').addEventListener('change', protectOptions);
  document.getElementById('tags').addEventListener('change', protectOptions);
  wire('restore-drop', 'restore-input', 'restore-folder', function (item) {
    send(item, 'v1/restore?format=json', document.getElementById('restore-results'), restoreDetail);
  });

  // ---------------------------------------------------------------------------------------
  // Les textes.

  /* La carte du texte : Protect et Restore écrivent leur résultat à la place du texte, dont les
     étiquettes et les valeurs restaurées sont colorées par type sur des marques dessinées sous la
     zone de texte ; Show the values found les liste en dessous. */
  const textArea = document.getElementById('text');
  const textMarks = document.getElementById('text-marks');
  const textLegend = document.getElementById('text-legend');
  // Les étiquettes du pseudonymiseur : [[KEYWORD:body#CCC]].
  const tagPattern = new RegExp('\\[\\[(PERSON|ORG|LOCATION|ADDRESS|EMAIL|PHONE|IBAN|CARD|NIR|SIRET|ID|IPV4|IPV6|' +
    'MAC|URL|DATE|PLATE|GEO|TITLE|GENDER|NATIONALITY):[^\\[\\]]{1,4096}?#[0-9A-Z]{3}\\]\\]', 'g');
  // Les valeurs tout juste restaurées, en unités UTF-16 du texte, jusqu'à ce que le texte change.
  let restoredMarks = null;
  /* Les substituts des textes protégés sans étiquettes dans cette page, qui seule rend leurs valeurs : ils
     s'en vont avec la page. */
  const textSubstitutions = [];
  const textSubstitutionKeys = new Set();
  // Les valeurs protégées du texte telles que le service les a placées, en unités UTF-16 : étiquettes et
  // substituts.
  let protectedMarks = null;

  function markKind(type) {
    if (type === 'ORG') return 'org';
    if (type === 'LOCATION' || type === 'ADDRESS' || type === 'GEO' || type === 'PLATE') return 'place';
    if (type === 'EMAIL' || type === 'PHONE' || type === 'URL' || type === 'IPV4' || type === 'IPV6' ||
        type === 'MAC') return 'contact';
    if (type === 'ID' || type === 'IBAN' || type === 'CARD' || type === 'NIR' || type === 'SIRET') return 'id';
    if (type === 'DATE') return 'date';
    return 'person';
  }

  /* Des décalages en octets du texte UTF-8, tels que le service les donne, convertis en unités UTF-16
     du texte tel que la zone de texte le contient, chaque saut de ligne étant un LF : le LF d'un CR LF
     ne prend pas de place à lui. */
  function unitSpans(text, spans) {
    const at = new Map([[0, 0]]);
    let bytes = 0, units = 0, previous = '';
    for (const ch of text) {
      const c = ch.codePointAt(0);
      bytes += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
      if (!(ch === '\n' && previous === '\r')) units += ch.length;
      previous = ch;
      at.set(bytes, units);
    }
    const out = [];
    (spans || []).forEach(function (s) {
      const b = at.get(s.begin), e = at.get(s.end);
      if (b !== undefined && e !== undefined && e > b) out.push({ begin: b, end: e, type: s.type });
    });
    return out;
  }

  function syncMarks() {
    // La barre de défilement de la zone de texte rétrécit son texte : les marques prennent la même
    // largeur.
    const bar = Math.max(0, textArea.offsetWidth - textArea.clientWidth - 2);
    textMarks.style.paddingRight = (10 + bar) + 'px';
    textMarks.scrollTop = textArea.scrollTop;
    textMarks.scrollLeft = textArea.scrollLeft;
  }

  function renderMarks() {
    const text = textArea.value;
    let spans = [];
    if (restoredMarks) {
      spans = restoredMarks;
    } else if (protectedMarks) {
      spans = protectedMarks.slice();
    } else {
      tagPattern.lastIndex = 0;
      for (let m = tagPattern.exec(text); m; m = tagPattern.exec(text))
        spans.push({ begin: m.index, end: m.index + m[0].length, type: m[1] });
    }
    spans.sort(function (a, b) { return a.begin - b.begin; });
    textMarks.textContent = '';
    let pos = 0;
    spans.forEach(function (s) {
      if (s.begin < pos) return;
      textMarks.appendChild(document.createTextNode(text.slice(pos, s.begin)));
      const mark = el('mark', 'k-' + markKind(s.type) + (restoredMarks ? ' restored' : ''), text.slice(s.begin, s.end));
      textMarks.appendChild(mark);
      pos = s.end;
    });
    // Un saut de ligne final garde la hauteur d'une dernière ligne vide de la zone de texte.
    textMarks.appendChild(document.createTextNode(text.slice(pos) + '\n'));
    textLegend.hidden = spans.length === 0;
    document.getElementById('text-legend-how').textContent =
      restoredMarks ? t('legend_restored') : t('legend_protected');
    syncMarks();
  }

  textArea.addEventListener('input', function () {
    restoredMarks = null;
    protectedMarks = null;
    renderMarks();
  });
  textArea.addEventListener('scroll', syncMarks);
  if (window.ResizeObserver) new ResizeObserver(syncMarks).observe(textArea);
  textReady = true;

  async function textRoute(route) {
    const out = document.getElementById('text-result');
    const request = { text: textArea.value };
    if (route === 'v1/text/anonymize') request.tags = document.getElementById('text-tags').checked;
    if (route === 'v1/text/restore' && textSubstitutions.length) request.substitutions = textSubstitutions;
    let r;
    try {
      r = await fetch(route, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(request)
      });
    } catch (e) {
      out.hidden = false;
      out.textContent = t('s_offline');
      return;
    }
    const body = await r.json().catch(function () { return null; });
    if (!r.ok || !body) {
      out.hidden = false;
      out.textContent = errorText(body, r.status);
      return;
    }
    if (body.spans) {
      out.hidden = false;
      out.textContent = body.spans.length ? body.spans.map(function (s) {
        return s.type + '  ' + s.text + '  (' + Math.round(s.confidence * 100) + ' %)';
      }).join('\n') : t('none_found');
      return;
    }
    // Protect et Restore : le résultat prend la place du texte.
    out.hidden = true;
    textArea.value = body.text.replace(/\r\n?/g, '\n');
    (body.substitutions || []).forEach(function (x) {
      const key = x.substitute + '\u0000' + x.tag;
      if (textSubstitutionKeys.has(key) || textSubstitutions.length >= 100000) return;
      textSubstitutionKeys.add(key);
      textSubstitutions.push({ substitute: x.substitute, tag: x.tag });
    });
    restoredMarks = route === 'v1/text/restore' ? unitSpans(body.text, body.restored) : null;
    if (restoredMarks && !restoredMarks.length) restoredMarks = null;
    // Les substituts d'un texte sans étiquettes sont colorés là où le service les a trouvés.
    protectedMarks = route === 'v1/text/anonymize' && body.substitutions ? unitSpans(body.text, body.tags) : null;
    renderMarks();
  }

  document.getElementById('text-tags').addEventListener('change', function () {
    document.getElementById('text-untagged').hidden = document.getElementById('text-tags').checked;
  });

  document.getElementById('text-protect').addEventListener('click', function () { textRoute('v1/text/anonymize'); });
  document.getElementById('text-restore').addEventListener('click', function () { textRoute('v1/text/restore'); });
  document.getElementById('text-detect').addEventListener('click', function () { textRoute('v1/text/detect'); });

  // ---------------------------------------------------------------------------------------
  // L'administration : les originaux gardés, la clé, les réglages.

  const records = [];
  let recordsTotal = 0;
  let fingerprint = '';

  function size(bytes) {
    const mb = bytes >= 1048576;
    try {
      return new Intl.NumberFormat(language, { style: 'unit', unit: mb ? 'megabyte' : 'kilobyte',
        maximumFractionDigits: 1 }).format(mb ? bytes / 1048576 : Math.max(0.1, bytes / 1024));
    } catch (e) {
      return mb ? (bytes / 1048576).toFixed(1) + ' MB' : (bytes / 1024).toFixed(1) + ' KB';
    }
  }

  function message(id, text, bad) {
    const p = document.getElementById(id);
    p.hidden = !text;
    p.textContent = text || '';
    p.className = 'message' + (bad ? ' bad' : '');
  }

  function save(blob, name) {
    const a = el('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 60000);
  }

  function renderRecords() {
    const body = document.getElementById('records');
    body.textContent = '';
    records.forEach(function (r) {
      const tr = el('tr');
      tr.appendChild(el('td', 'name', r.original_name));
      tr.appendChild(el('td', '', new Date(r.created).toLocaleString(language)));
      const source = el('td', '', t('source_' + r.source));
      // Protégé sans étiquettes : ses valeurs ne reviennent que grâce à cette fiche.
      if (r.tags === false) source.appendChild(el('div', 'note', t('records_untagged')));
      // Un fichier qui a quitté le lecteur : son original reste jusqu'à une date, ou sans limite de
      // temps.
      if (r.forgotten) {
        const until = new Date(r.forgotten).toLocaleDateString(language);
        source.appendChild(el('div', 'note', t('records_removed', { d: until })));
      } else if (r.removed) {
        source.appendChild(el('div', 'note', t('records_removed_always')));
      }
      tr.appendChild(source);
      tr.appendChild(el('td', 'number', size(r.original_size)));
      const actions = el('td', 'actions');
      const get = el('button', 'secondary', t('btn_original'));
      get.addEventListener('click', function () { original(r); });
      const forget = el('button', 'secondary', t('btn_forget'));
      forget.addEventListener('click', function () { forgetRecord(r); });
      actions.appendChild(get);
      actions.appendChild(forget);
      tr.appendChild(actions);
      body.appendChild(tr);
    });
    document.getElementById('records-none').hidden = recordsTotal > 0;
    document.getElementById('records-more').hidden = records.length >= recordsTotal;
  }

  async function loadRecords(more) {
    let r;
    try {
      r = await adminFetch('v1/records?limit=50&offset=' + (more ? records.length : 0));
    } catch (e) {
      return false;
    }
    if (!r.ok) return false;
    const body = await r.json().catch(function () { return null; });
    if (!body || !Array.isArray(body.records)) return false;
    if (!more) records.length = 0;
    body.records.forEach(function (x) { records.push(x); });
    recordsTotal = body.total;
    renderRecords();
    return true;
  }

  async function original(r) {
    let answer;
    try {
      answer = await adminFetch('v1/records/' + r.id + '/original');
    } catch (e) {
      await failureDialog(t('s_offline'));
      return;
    }
    if (!answer.ok) {
      const body = await answer.json().catch(function () { return null; });
      await failureDialog(errorText(body, answer.status));
      return;
    }
    save(await answer.blob(), r.original_name);
  }

  async function forgetRecord(r) {
    const sure = await dialog({
      title: t('forget_title'),
      text: t(r.tags === false ? 'confirm_forget_untagged' : 'confirm_forget', { f: r.original_name }),
      cancel: t('btn_cancel'),
      confirm: t('btn_forget'),
      danger: true
    });
    if (!sure) return;
    let answer;
    try {
      answer = await adminFetch('v1/records/' + r.id, { method: 'DELETE' });
    } catch (e) {
      await failureDialog(t('s_offline'));
      return;
    }
    if (!answer.ok) {
      const body = await answer.json().catch(function () { return null; });
      await failureDialog(errorText(body, answer.status));
    }
    await loadRecords(false);
    refreshStatus();
  }

  // Une requête de l'administration avec un corps JSON : sa réponse, ou le statut 0 quand le service n'a
  // pas répondu.
  async function sendJson(route, value, method) {
    let r;
    try {
      r = await adminFetch(route, {
        method: method || 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(value)
      });
    } catch (e) {
      return { ok: false, status: 0, body: null };
    }
    const body = await r.json().catch(function () { return null; });
    return { ok: r.ok && !!body, status: r.status, body: body };
  }

  function failure(r) {
    return r.status === 0 ? t('s_offline') : errorText(r.body, r.status);
  }

  async function exportKey() {
    const p1 = document.getElementById('pass1').value, p2 = document.getElementById('pass2').value;
    if (p1 !== p2) return message('key-message', t('passphrase_mismatch'), true);
    const r = await sendJson('v1/key/export', { passphrase: p1 });
    if (!r.ok) return message('key-message', failure(r), true);
    save(new Blob([r.body.key], { type: 'application/json' }), 'pseudalis-key-' + (fingerprint || 'export') + '.json');
    document.getElementById('pass1').value = document.getElementById('pass2').value = '';
    message('key-message', t('key_exported'));
  }

  async function importKey(file) {
    const passphrase = document.getElementById('pass1').value;
    const r = await sendJson('v1/key/import', { key: await file.text(), passphrase: passphrase });
    if (!r.ok) return message('key-message', failure(r), true);
    document.getElementById('pass1').value = document.getElementById('pass2').value = '';
    message('key-message', t('key_imported'));
  }

  async function loadSettings() {
    let r;
    try {
      r = await adminFetch('v1/settings');
    } catch (e) {
      return;
    }
    if (!r.ok) return;
    const s = await r.json().catch(function () { return null; });
    if (!s) return;
    document.getElementById('set-language').value = s.language || 'auto';
    document.getElementById('set-drive').value = s.drive_letter || '';
    document.getElementById('set-dates').value = s.date_order === 'mdy' ? 'mdy' : 'dmy';
    document.getElementById('set-max').value = s.max_file_mb;
    document.getElementById('set-removed').value = s.removed_days;
    document.getElementById('set-history').value = s.history_mb;
    document.getElementById('set-albireo').checked = s.albireo !== 'off';
    document.getElementById('set-tags').checked = s.tags !== false;
  }

  // Les réglages que la carte affiche, par leur nom dans l'interface, avec la clé de leur libellé.
  const settingLabels = {
    language: 'set_language', drive_letter: 'set_drive', date_order: 'set_dates', max_file_mb: 'set_max',
    removed_days: 'set_removed', history_mb: 'set_history', albireo: 'set_albireo',
    tags: 'set_tags'
  };

  // Le nombre entier d'un champ, ou null quand il n'en contient pas : le service le refuse avec sa plage.
  function wholeNumber(id) {
    const text = document.getElementById(id).value.trim();
    return /^\d+$/.test(text) ? parseInt(text, 10) : null;
  }

  async function saveSettings() {
    const r = await sendJson('v1/settings', {
      language: document.getElementById('set-language').value,
      drive_letter: document.getElementById('set-drive').value.trim().toUpperCase(),
      date_order: document.getElementById('set-dates').value,
      max_file_mb: wholeNumber('set-max'),
      removed_days: wholeNumber('set-removed'),
      history_mb: wholeNumber('set-history'),
      albireo: document.getElementById('set-albireo').checked ? 'auto' : 'off',
      tags: document.getElementById('set-tags').checked
    }, 'PUT');
    if (!r.ok) {
      const b = r.body || {};
      if (b.code === 'settings_value' && b.args && settingLabels[b.args.field]) {
        const range = b.args.min !== undefined ? t('range', { min: b.args.min, max: b.args.max }) : '';
        return message('settings-message', t('err_settings_value', { field: t(settingLabels[b.args.field]) }) +
          range, true);
      }
      return message('settings-message', failure(r), true);
    }
    message('settings-message', t(r.body.restart ? 'saved_restart' : 'saved'));
    loadSettings();
    refreshStatus();
    // Les cases des cartes repartent du réglage des étiquettes quand il change, et seulement alors.
    if ((r.body.tags !== false) !== tagsDefault) applyTagsDefault(r.body.tags !== false);
  }

  /* Le réglage des étiquettes par défaut, que la page lit à son démarrage : les cases des cartes
     Protect files et Protect a text en partent, et chacune ne s'applique qu'à ses fichiers ou à son
     texte. */
  let tagsDefault = true;
  function applyTagsDefault(tags) {
    tagsDefault = tags;
    document.getElementById('tags').checked = tags;
    document.getElementById('text-tags').checked = tags;
    document.getElementById('text-untagged').hidden = tags;
    protectOptions();
  }

  // ---------------------------------------------------------------------------------------
  // Les termes imposés : remplacés par l'étiquette de leur type avant tout le reste d'un texte.

  let terms = [];
  let termTypes = ['ORG', 'PERSON', 'LOCATION', 'ADDRESS', 'ID', 'EMAIL', 'PHONE', 'URL'];
  let termsMax = 100, termLength = 200;

  function renderTerms() {
    const body = document.getElementById('terms');
    if (!body) return;
    body.textContent = '';
    terms.forEach(function (term, i) {
      const tr = el('tr');
      if (term.bad) tr.className = 'bad';
      const cell = el('td');
      const input = el('input');
      input.type = 'text';
      input.maxLength = termLength;
      input.value = term.term;
      input.setAttribute('aria-label', t('col_term'));
      input.addEventListener('input', function () {
        term.term = input.value;
        term.bad = false;
        tr.className = '';
      });
      cell.appendChild(input);
      tr.appendChild(cell);
      const check = el('td', 'check');
      const box = el('input');
      box.type = 'checkbox';
      box.checked = !!term.case_sensitive;
      box.setAttribute('aria-label', t('col_case'));
      box.addEventListener('change', function () { term.case_sensitive = box.checked; });
      check.appendChild(box);
      tr.appendChild(check);
      const tag = el('td');
      const select = el('select');
      select.setAttribute('aria-label', t('col_tag'));
      termTypes.forEach(function (k) {
        const o = el('option', '', t('tag_' + k));
        o.value = k;
        select.appendChild(o);
      });
      select.value = termTypes.indexOf(term.type) >= 0 ? term.type : termTypes[0];
      term.type = select.value;
      select.addEventListener('change', function () { term.type = select.value; });
      tag.appendChild(select);
      tr.appendChild(tag);
      const actions = el('td', 'actions');
      const remove = el('button', 'secondary', t('btn_remove'));
      remove.addEventListener('click', function () {
        terms.splice(i, 1);
        renderTerms();
      });
      actions.appendChild(remove);
      tr.appendChild(actions);
      body.appendChild(tr);
    });
    document.getElementById('terms-none').hidden = terms.length > 0;
    document.getElementById('terms-add').disabled = terms.length >= termsMax;
    document.getElementById('terms-count').textContent = t('terms_count', { n: terms.length, max: termsMax });
  }

  async function loadTerms() {
    let r;
    try {
      r = await adminFetch('v1/terms');
    } catch (e) {
      return;
    }
    if (!r.ok) return;
    const body = await r.json().catch(function () { return null; });
    if (!body) return;
    if (Array.isArray(body.types) && body.types.length) termTypes = body.types;
    termsMax = body.max || termsMax;
    termLength = body.max_length || termLength;
    terms = (body.terms || []).map(function (x) {
      return { term: x.term, type: x.type, case_sensitive: !!x.case_sensitive };
    });
    renderTerms();
  }

  function addTerm() {
    if (terms.length >= termsMax) return;
    terms.push({ term: '', type: termTypes[0], case_sensitive: false });
    renderTerms();
    const inputs = document.querySelectorAll('#terms input[type=text]');
    if (inputs.length) inputs[inputs.length - 1].focus();
  }

  async function saveTerms() {
    // Les lignes laissées vides ne sont pas des termes ; les autres gardent leur place pour les erreurs.
    const rows = [];
    const list = [];
    terms.forEach(function (x, i) {
      x.bad = false;
      const text = (x.term || '').trim();
      if (!text) return;
      rows.push(i);
      list.push({ term: text, type: x.type, case_sensitive: !!x.case_sensitive });
    });
    const r = await sendJson('v1/terms', { terms: list }, 'PUT');
    if (!r.ok) {
      const b = r.body || {};
      const at = b.args && b.args.index !== undefined ? Number(b.args.index) : -1;
      const row = at >= 0 && at < rows.length ? rows[at] : -1;
      if (row >= 0) terms[row].bad = true;
      renderTerms();
      const known = ['terms_invalid', 'terms_too_many', 'terms_type', 'terms_duplicate'].indexOf(b.code) >= 0;
      return message('terms-message', known ? t('err_' + b.code, { n: row + 1, max: termsMax, length: termLength })
        : failure(r), true);
    }
    terms = (r.body.terms || []).map(function (x) {
      return { term: x.term, type: x.type, case_sensitive: !!x.case_sensitive };
    });
    renderTerms();
    message('terms-message', t('terms_saved', { n: terms.length }));
  }

  async function startAdmin() {
    if (!admin || !(await loadRecords(false))) return;
    ['records-card', 'key-card', 'settings-card', 'terms-card'].forEach(function (id) {
      document.getElementById(id).hidden = false;
    });
    const info = await fetch('v1/info', { cache: 'no-store' }).then(function (r) { return r.json(); })
      .catch(function () { return {}; });
    fingerprint = info.key_fingerprint || '';
    document.getElementById('fingerprint').textContent = fingerprint;
    loadSettings();
    loadTerms();
  }

  document.getElementById('records-more').addEventListener('click', function () { loadRecords(true); });
  document.getElementById('key-export').addEventListener('click', exportKey);
  document.getElementById('key-import').addEventListener('change', function (e) {
    const f = e.target.files[0];
    e.target.value = '';
    if (f) importKey(f);
  });
  document.getElementById('settings-save').addEventListener('click', saveSettings);
  document.getElementById('terms-add').addEventListener('click', addTerm);
  document.getElementById('terms-save').addEventListener('click', saveTerms);

  const select = document.getElementById('language');
  language = pick();
  select.value = language;
  select.addEventListener('change', function () {
    language = select.value;
    try { localStorage.setItem('pseudalis.language', language); } catch (e) { /* stockage indisponible */ }
    translate();
  });
  translate();
  fetch('v1/info', { cache: 'no-store' }).then(function (r) { return r.json(); })
    .then(function (info) { if (info && info.tags === false) applyTagsDefault(false); })
    .catch(function () { /* le service ne répond pas : les options gardent leurs étiquettes */ });
  startAdmin();
  setInterval(refreshStatus, 15000);
})();
