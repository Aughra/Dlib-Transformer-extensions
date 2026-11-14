// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les champs où l'utilisateur écrit pour le modèle (fork pour macOS, repris de l'original, Albireo 1.0.15) : la
    question d'une discussion, les instructions des réglages et d'un projet, et la mémoire.  Un champ garde comme texte
    le markdown que l'utilisateur écrit, qui va au modèle tel quel, et le dessine au fil de la frappe, comme les
    réponses le dessinent : gras, italique, barré et surligné (==texte==), code, titres, listes avec leurs puces et leur
    retrait, citations, blocs de code et liens, leurs marques en gris clair.  Le champ est un élément dont le texte se
    modifie (contenteditable), redessiné depuis son texte après chaque changement, la sélection gardée sur les mêmes
    caractères, et il garde la partie de l'interface d'un textarea dont l'application se sert : value, placeholder,
    disabled, focus(), et les évènements input, change, keydown et paste.  Un champ redessiné laisse le navigateur sans
    l'historique de ses modifications : le champ garde le sien, que suivent Cmd+Z et Cmd+Maj+Z (ou Ctrl).  Entrée dans
    un élément de liste commence l'élément suivant, et sur un élément vide termine la liste ; Tab et Maj+Tab décalent
    les éléments de la sélection ; Cmd+B et Cmd+I (ou Ctrl) mettent les marques du gras et de l'italique autour de la
    sélection, ou les retirent.
*/

const escapes = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => escapes[c]);
const faint = (s) => s ? '<span class="md-mark">' + esc(s) + '</span>' : '';
const span = (cls, html) => '<span class="' + cls + '">' + html + '</span>';

// Les marques en ligne du markdown, essayées à la position de leur premier caractère (lastIndex).
const codeSpan = /(`+)([^`]|[^`][\s\S]*?[^`])\1(?!`)/y;
const strong = /(\*\*|__)(?=\S)([\s\S]*?\S)\1/y;
const em = /([*_])(?=[^\s*_])([\s\S]*?[^\s*_])\1(?![*_])/y;
const struck = /~~(?=\S)([\s\S]*?\S)~~/y;
const highlighted = /==(?=[^\s=])([\s\S]*?[^\s=])==/y;
const link = /(!?\[)([^\]\n]+)(\]\([^()\s]*(?:\([^()\s]*\)[^()\s]*)*(?:\s+"[^"\n]*")?\))/y;
const word = /[A-Za-z0-9]/;

function at(re, s, i) {
  re.lastIndex = i;
  return re.exec(s);
}

/*
    Le HTML d'une ligne de texte avec ses marques en ligne dessinées.  Une marque dont la recherche de la fin n'a rien
    trouvé, depuis une position où elle peut commencer, ne trouve rien non plus depuis une position plus loin, et un
    lien dont la recherche a échoué échoue depuis toute position avant le premier ] qui le suit : la ligne est
    parcourue une fois pour chaque sorte de marque.
*/
function inline(s) {
  let out = '';
  let from = 0;
  let i = 0;
  const unclosed = {};
  let linkless = -1;
  const flush = (to) => {
    if (to > from) out += esc(s.slice(from, to));
  };
  // La marque key que re trouve en i, quand elle peut encore se terminer ; starts dit si elle peut commencer en i.
  const mark = (key, re, starts) => {
    if (!starts || unclosed[key]) return null;
    const r = at(re, s, i);
    if (!r) unclosed[key] = true;
    return r;
  };
  while (i < s.length) {
    const c = s[i];
    let m = null;
    let html = '';
    if (c === '\\' && i + 1 < s.length && /[!-/:-@[-`{-~]/.test(s[i + 1])) {
      html = faint('\\') + esc(s[i + 1]);
      m = { length: 2 };
    } else if (c === '`') {
      const r = at(codeSpan, s, i);
      if (r) {
        html = span('md-code', faint(r[1]) + esc(r[2]) + faint(r[1]));
        m = { length: r[0].length };
      } else {
        // Une suite d'accents graves sans sa suite fermante reste telle quelle.
        let n = 1;
        while (s[i + n] === '`') ++n;
        flush(i);
        out += esc(s.slice(i, i + n));
        i += n;
        from = i;
        continue;
      }
    } else if ((c === '[' || (c === '!' && s[i + 1] === '[')) && i >= linkless) {
      const r = at(link, s, i);
      if (r) {
        html = span('md-link', faint(r[1]) + inline(r[2]) + faint(r[3]));
        m = { length: r[0].length };
      } else {
        const close = s.indexOf(']', i);
        linkless = close < 0 ? s.length : close;
      }
    } else if (c === '*' || c === '_') {
      // Les marques _ ne se placent qu'entre des mots, comme les réponses les lisent.
      const intraword = c === '_' && i > 0 && word.test(s[i - 1]);
      let r = mark('strong' + c, strong, !intraword && s[i + 1] === c && /\S/.test(s[i + 2] || ''));
      if (r && (c !== '_' || !word.test(s[i + r[0].length] || ''))) {
        html = span('md-strong', faint(r[1]) + inline(r[2]) + faint(r[1]));
        m = { length: r[0].length };
      } else {
        r = mark('em' + c, em, !intraword && /[^\s*_]/.test(s[i + 1] || ''));
        if (r && (c !== '_' || !word.test(s[i + r[0].length] || ''))) {
          html = span('md-em', faint(r[1]) + inline(r[2]) + faint(r[1]));
          m = { length: r[0].length };
        }
      }
    } else if (c === '~') {
      const r = mark('~', struck, s[i + 1] === '~' && /\S/.test(s[i + 2] || ''));
      if (r) {
        html = faint('~~') + span('md-del', inline(r[1])) + faint('~~');
        m = { length: r[0].length };
      }
    } else if (c === '=') {
      const r = mark('=', highlighted, s[i + 1] === '=' && /[^\s=]/.test(s[i + 2] || ''));
      if (r) {
        html = span('md-hl', faint('==') + inline(r[1]) + faint('=='));
        m = { length: r[0].length };
      }
    }
    if (m) {
      flush(i);
      out += html;
      i += m.length;
      from = i;
    } else {
      ++i;
    }
  }
  flush(s.length);
  return out;
}

// La largeur du retrait d'une ligne, une tabulation comptant quatre espaces.
function indentWidth(s) {
  let n = 0;
  for (const c of s) n += c === '\t' ? 4 - (n % 4) : 1;
  return n;
}

// Le HTML d'une ligne hors des blocs de code.
function line(s) {
  let m = /^( {0,3})(#{1,6})([ \t]+|$)(.*)$/.exec(s);
  if (m) return span('md-h md-h' + m[2].length, faint(m[1] + m[2] + m[3]) + inline(m[4]));
  if (/^ {0,3}([-*_])(?:[ \t]*\1){2,}[ \t]*$/.test(s)) return span('md-hr', esc(s));
  m = /^( {0,3}(?:>[ \t]?)+)(.*)$/.exec(s);
  if (m) return span('md-quote', faint(m[1]) + line(m[2]));
  m = /^([ \t]*)([-*+])([ \t]+)(\[[ xX]\](?:[ \t]+|$))?(.*)$/.exec(s);
  if (m) {
    const level = Math.floor(indentWidth(m[1]) / 2) % 3;
    return esc(m[1]) + span('md-bullet md-level' + level, esc(m[2])) + esc(m[3]) + faint(m[4] || '')
      + inline(m[5]);
  }
  m = /^([ \t]*)(\d{1,9}[.)])([ \t]+)(.*)$/.exec(s);
  if (m) return esc(m[1]) + span('md-number', esc(m[2])) + esc(m[3]) + inline(m[4]);
  // Une ligne d'un tableau : ses cellules, entre leurs barres.
  if (/^\s*\|.*\|\s*$/.test(s)) return s.split('|').map((cell) => inline(cell)).join(faint('|'));
  return inline(s);
}

// Le HTML d'un texte en markdown, ses lignes et leurs sauts de ligne.
export function drawMarkdown(text) {
  const lines = String(text).split('\n');
  const out = [];
  let fence = '';
  for (let k = 0; k < lines.length; ++k) {
    const s = lines[k];
    const end = k + 1 < lines.length ? '\n' : '';
    if (fence) {
      const close = /^ {0,3}(`{3,}|~{3,})[ \t]*$/.exec(s);
      if (close && close[1][0] === fence[0] && close[1].length >= fence.length) {
        out.push(span('md-fence', esc(s)) + end);
        fence = '';
      } else {
        out.push(span('md-pre', esc(s)) + end);
      }
      continue;
    }
    const open = /^ {0,3}(`{3,}|~{3,})(.*)$/.exec(s);
    if (open && !(open[1][0] === '`' && open[2].includes('`'))) {
      fence = open[1];
      out.push(span('md-fence', esc(s)) + end);
      continue;
    }
    out.push(line(s) + end);
  }
  return out.join('');
}

const blocks = /^(DIV|P|LI|UL|OL|PRE|BLOCKQUOTE|H[1-6]|TABLE|TR)$/;

/*
    Le texte du champ tel que le navigateur l'a laissé après une modification, et les positions de la sélection dans
    ce texte, -1 quand la sélection est ailleurs.  Un saut de ligne est un caractère du texte ou un élément <br>, et un
    bloc qu'une modification a inséré commence une ligne ; le <br> que le champ dessine après son texte, pour qu'une
    dernière ligne vide se voie, reste à part.
*/
function readField(el) {
  let text = '';
  let start = -1;
  let end = -1;
  const sel = el.ownerDocument.getSelection();
  const range = sel && sel.rangeCount ? sel.getRangeAt(0) : null;
  const mark = (node, offset) => {
    if (!range) return;
    if (node === range.startContainer && offset === range.startOffset) start = text.length;
    if (node === range.endContainer && offset === range.endOffset) end = text.length;
  };
  const walk = (parent) => {
    let i = 0;
    for (let n = parent.firstChild; n; n = n.nextSibling, ++i) {
      mark(parent, i);
      if (n.nodeType === 3) {
        if (range && n === range.startContainer) start = text.length + Math.min(range.startOffset, n.data.length);
        if (range && n === range.endContainer) end = text.length + Math.min(range.endOffset, n.data.length);
        text += n.data;
      } else if (n.nodeName === 'BR') {
        text += '\n';
      } else if (n.nodeType === 1) {
        if (blocks.test(n.nodeName) && text && !text.endsWith('\n')) text += '\n';
        walk(n);
      }
    }
    mark(parent, i);
  };
  walk(el);
  if (el.lastChild && el.lastChild.nodeName === 'BR' && text.endsWith('\n')) text = text.slice(0, -1);
  return { text, start: Math.min(start, text.length), end: Math.min(end, text.length) };
}

/*
    Le texte après une modification, les espaces insécables que le navigateur met à la place des espaces qu'il insère
    redevenus des espaces simples dans la partie que la modification a changée, ceux du texte d'avant gardés.
*/
function plainSpaces(before, after) {
  const n = Math.min(before.length, after.length);
  let a = 0;
  while (a < n && before[a] === after[a]) ++a;
  let b = 0;
  while (b < n - a && before[before.length - 1 - b] === after[after.length - 1 - b]) ++b;
  return after.slice(0, a) + after.slice(a, after.length - b).replace(/\u00a0/g, ' ') + after.slice(after.length - b);
}

// La position dans le DOM d'un caractère du texte d'un champ qui vient d'être dessiné.
function locate(el, offset) {
  let count = 0;
  const walker = el.ownerDocument.createTreeWalker(el, NodeFilter.SHOW_TEXT);
  for (let n = walker.nextNode(); n; n = walker.nextNode()) {
    if (offset <= count + n.data.length) return [n, offset - count];
    count += n.data.length;
  }
  // Un texte vide : avant le saut de ligne dessiné après lui.
  return [el, Math.max(0, el.childNodes.length - 1)];
}

function selectRange(el, start, end) {
  const sel = el.ownerDocument.getSelection();
  if (!sel) return;
  const range = el.ownerDocument.createRange();
  const [a, ao] = locate(el, start);
  const [b, bo] = locate(el, end);
  range.setStart(a, ao);
  range.setEnd(b, bo);
  sel.removeAllRanges();
  sel.addRange(range);
}

// Fait voir le curseur dans un champ qui défile.
function reveal(el) {
  const sel = el.ownerDocument.getSelection();
  if (!sel || !sel.rangeCount || el.scrollHeight <= el.clientHeight) return;
  const rects = sel.getRangeAt(0).getClientRects();
  const r = rects.length ? rects[rects.length - 1] : null;
  if (!r) return;
  const box = el.getBoundingClientRect();
  if (r.bottom > box.bottom) el.scrollTop += r.bottom - box.bottom + 4;
  else if (r.top < box.top) el.scrollTop -= box.top - r.top + 4;
}

const listItem = /^([ \t]*)([-*+]|(\d{1,9})([.)]))([ \t]+)(\[[ xX]\][ \t]+)?/;
// Les modifications qui rejoignent l'entrée de l'historique d'avant : une frappe dans ce délai après la précédente.
const joinMs = 1000;
const historyLength = 200;
// Les caractères d'un texte dont le champ dessine les marques, au-delà desquels il dessine le texte seul.
const drawnLength = 30000;
/*
    Les caractères d'un texte au-delà desquels le champ redessine ses marques une fois que la frappe marque une pause
    de settleMs, le navigateur montrant entre-temps les caractères tapés : dessiner un long texte à chaque touche
    ralentit la frappe.
*/
const quickLength = 4000;
const settleMs = 150;

/*
    Un champ de markdown.  options : placeholder, label (son nom pour les lecteurs d'écran), className.
*/
export function markdownField(options = {}) {
  const el = document.createElement('div');
  el.className = 'md-field md-text' + (options.className ? ' ' + options.className : '');
  el.setAttribute('role', 'textbox');
  el.setAttribute('aria-multiline', 'true');
  if (options.label) el.setAttribute('aria-label', options.label);
  el.spellcheck = true;
  let text = '';
  let placeholder = '';
  let disabled = false;
  let composing = false;
  let focusText = '';
  let history = [{ text: '', start: 0, end: 0 }];
  let at = 0;
  let lastKind = '';
  let lastTime = 0;
  // Un texte donné par l'application, dont le curseur va à la fin au prochain focus, comme dans un textarea.
  let fresh = false;
  // Le dessin qui attend une pause de la frappe (quickLength), et un texte déplacé par glisser (deleteByDrag).
  let settle = 0;
  let dragged = null;

  const editable = () => {
    if (disabled) {
      el.contentEditable = 'false';
      return;
    }
    // Les navigateurs qui n'éditent que du texte brut tiennent hors du champ le texte enrichi d'un collage ou d'un dépôt.
    try {
      el.contentEditable = 'plaintext-only';
    } catch (e) {
      el.contentEditable = 'true';
    }
    if (el.contentEditable !== 'plaintext-only') el.contentEditable = 'true';
  };

  const draw = (start, end) => {
    clearTimeout(settle);
    settle = 0;
    el.innerHTML = (text.length > drawnLength ? esc(text) : drawMarkdown(text)) + '<br>';
    el.classList.toggle('empty', !text);
    if (start != null && el.ownerDocument.activeElement === el) {
      selectRange(el, start, end == null ? start : end);
      reveal(el);
    }
  };

  const remember = (start, end, kind) => {
    const now = Date.now();
    const group = (k) => (k === 'insertText' ? 'insert' : k.startsWith('delete') ? 'delete' : k);
    const join = (kind === 'insertText' || kind.startsWith('delete')) && group(kind) === group(lastKind)
      && now - lastTime < joinMs && at > 0;
    history.length = at + 1;
    if (join) {
      history[at] = { text, start, end };
    } else if (history[at].text !== text) {
      history.push({ text, start, end });
      if (history.length > historyLength) history.shift();
      at = history.length - 1;
    }
    lastKind = kind;
    lastTime = now;
  };

  // Un évènement input du champ lui-même, après une modification qu'il a faite à la place du navigateur.
  let own = false;
  const fire = (kind, data) => {
    own = true;
    try {
      el.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: kind, data: data == null ? null : data }));
    } finally {
      own = false;
    }
  };

  // Remplace les caractères de start à end par insert, le curseur après lui sauf si caret dit où.
  const replace = (start, end, insert, kind, caret) => {
    const before = text;
    text = text.slice(0, start) + insert + text.slice(end);
    const c = caret == null ? start + insert.length : caret;
    draw(c, c);
    if (text !== before) {
      remember(c, c, kind);
      fire(kind, insert);
    }
  };

  const selection = () => {
    const r = readField(el);
    if (r.start < 0 || r.end < 0) return { start: text.length, end: text.length };
    return { start: Math.min(r.start, r.end), end: Math.max(r.start, r.end) };
  };

  // Un saut de ligne, qui continue une liste : l'élément suivant, ou la fin de la liste après un élément vide.
  const newline = (kind) => {
    const { start, end } = selection();
    const lineStart = text.lastIndexOf('\n', start - 1) + 1;
    const m = start === end ? listItem.exec(text.slice(lineStart, start)) : null;
    if (!m) {
      replace(start, end, '\n', kind);
      return;
    }
    const lineEnd = text.indexOf('\n', start) < 0 ? text.length : text.indexOf('\n', start);
    if (lineStart + m[0].length === start && lineEnd === start) {
      // Un élément vide termine la liste.
      replace(lineStart, lineEnd, '', kind);
      return;
    }
    const marker = m[3] ? String(Number(m[3]) + 1) + m[4] : m[2];
    replace(start, end, '\n' + m[1] + marker + m[5] + (m[6] ? '[ ] ' : ''), kind);
  };

  // Les lignes de celle de start à celle de end, sous la forme [début de ligne, fin de ligne].
  const linesOf = (start, end) => {
    const out = [];
    let s = text.lastIndexOf('\n', start - 1) + 1;
    for (;;) {
      const e = text.indexOf('\n', s) < 0 ? text.length : text.indexOf('\n', s);
      out.push([s, e]);
      if (e >= end || e >= text.length) break;
      s = e + 1;
    }
    return out;
  };

  /*
      Décale vers la droite ou vers la gauche les éléments de liste de la sélection, de la largeur de leur marque
      (deux caractères pour une puce, trois pour un numéro) ; false quand la sélection n'en contient aucun.
  */
  const indent = (outdent) => {
    const { start, end } = selection();
    // [début de ligne, caractères retirés, caractères ajoutés], la dernière ligne d'abord.
    const edits = [];
    for (const [s0, e0] of linesOf(start, end)) {
      const item = text.slice(s0, e0);
      if (!listItem.test(item)) continue;
      const width = /^[ \t]*\d/.test(item) ? 3 : 2;
      if (!outdent) {
        edits.unshift([s0, 0, width]);
        continue;
      }
      const lead = /^[ \t]*/.exec(item)[0];
      let cut = 0;
      while (cut < lead.length && cut < width && lead[cut] === ' ') ++cut;
      if (cut === 0 && lead[0] === '\t') cut = 1;
      if (cut) edits.unshift([s0, cut, 0]);
    }
    if (!edits.length) return !outdent ? false : linesOf(start, end).some(([a, b]) => listItem.test(text.slice(a, b)));
    const move = (p) => {
      let q = p;
      for (const [s0, removed, added] of edits) {
        if (p >= s0) q += added - Math.min(removed, p - s0);
      }
      return q;
    };
    const a = move(start);
    const b = move(end);
    for (const [s0, removed, added] of edits) text = text.slice(0, s0) + ' '.repeat(added) + text.slice(s0 + removed);
    draw(a, b);
    remember(a, b, 'indent');
    fire('insertText', null);
    return true;
  };

  // Met les marques d'un format autour de la sélection, ou les retire quand elles l'entourent déjà.
  const surround = (marks) => {
    const { start, end } = selection();
    const n = marks.length;
    if (start >= n && text.slice(start - n, start) === marks && text.slice(end, end + n) === marks
        && !(n === 1 && (text[start - n - 1] === marks || text[end + n] === marks))) {
      const before = text;
      text = text.slice(0, start - n) + text.slice(start, end) + text.slice(end + n);
      draw(start - n, end - n);
      if (text !== before) {
        remember(start - n, end - n, 'format');
        fire('insertText', null);
      }
      return;
    }
    const before = text;
    text = text.slice(0, start) + marks + text.slice(start, end) + marks + text.slice(end);
    draw(start + n, end + n);
    if (text !== before) {
      remember(start + n, end + n, 'format');
      fire('insertText', null);
    }
  };

  const restore = (to) => {
    if (to < 0 || to >= history.length) return;
    const kind = to < at ? 'historyUndo' : 'historyRedo';
    at = to;
    const h = history[at];
    text = h.text;
    draw(h.start, h.end);
    lastKind = 'history';
    fire(kind, null);
  };

  el.addEventListener('beforeinput', (e) => {
    if (disabled) return;
    const kind = e.inputType || '';
    if (kind === 'insertParagraph' || kind === 'insertLineBreak') {
      e.preventDefault();
      newline(kind);
    } else if (kind === 'insertFromPaste' || kind === 'insertFromPasteAsQuotation' || kind === 'insertFromYank') {
      const data = e.dataTransfer ? e.dataTransfer.getData('text/plain') : null;
      if (data == null) return;
      e.preventDefault();
      const { start, end } = selection();
      replace(start, end, data.replace(/\r\n?/g, '\n'), 'insertFromPaste');
    } else if (kind === 'historyUndo') {
      e.preventDefault();
      restore(at - 1);
    } else if (kind === 'historyRedo') {
      e.preventDefault();
      restore(at + 1);
    } else if (kind === 'formatBold') {
      e.preventDefault();
      surround('**');
    } else if (kind === 'formatItalic') {
      e.preventDefault();
      surround('*');
    } else if (kind.startsWith('format')) {
      // Les autres formats enrichis n'ont pas de markdown : le texte reste tel quel.
      e.preventDefault();
    }
  });

  el.addEventListener('input', (e) => {
    if (own || composing || e.isComposing) return;
    const r = readField(el);
    text = plainSpaces(text, r.text);
    const start = r.start < 0 ? text.length : r.start;
    const end = r.end < 0 ? start : r.end;
    const kind = e.inputType || 'insertText';
    if (kind === 'deleteByDrag') {
      /*
          Le dépôt qui suit dans le champ place le texte sur les éléments que le navigateur a laissés : le champ se
          dessine une fois le dépôt fait, ou après le glisser quand le texte est allé ailleurs.
      */
      dragged = { start, end };
      setTimeout(() => {
        if (!dragged) return;
        const d = dragged;
        dragged = null;
        draw(d.start, d.end);
        remember(d.start, d.end, 'deleteByDrag');
      }, 0);
      return;
    }
    dragged = null;
    if (text.length > quickLength && (kind === 'insertText' || kind.startsWith('delete'))) {
      clearTimeout(settle);
      settle = setTimeout(() => {
        settle = 0;
        if (composing) return;
        const s = selection();
        draw(s.start, s.end);
      }, settleMs);
    } else {
      draw(start, end);
    }
    remember(start, end, kind);
  });

  el.addEventListener('compositionstart', () => { composing = true; });
  el.addEventListener('compositionend', () => {
    composing = false;
    const r = readField(el);
    text = plainSpaces(text, r.text);
    const start = r.start < 0 ? text.length : r.start;
    draw(start, r.end < 0 ? start : r.end);
    remember(start, r.end < 0 ? start : r.end, 'insertCompositionText');
  });

  el.addEventListener('keydown', (e) => {
    if (disabled || e.isComposing) return;
    const key = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    const command = (e.ctrlKey || e.metaKey) && !e.altKey;
    if (command && key === 'z') {
      e.preventDefault();
      restore(e.shiftKey ? at + 1 : at - 1);
    } else if (command && key === 'y' && !e.shiftKey) {
      e.preventDefault();
      restore(at + 1);
    } else if (command && !e.shiftKey && (key === 'b' || key === 'i')) {
      e.preventDefault();
      surround(key === 'b' ? '**' : '*');
    } else if (e.key === 'Tab' && !e.ctrlKey && !e.altKey && !e.metaKey) {
      // Tab quitte un champ dont la sélection ne contient aucun élément de liste, comme il quitte un textarea.
      if (indent(e.shiftKey)) e.preventDefault();
    }
  });

  el.addEventListener('focus', () => { focusText = text; });
  el.addEventListener('blur', () => {
    if (text !== focusText) {
      focusText = text;
      el.dispatchEvent(new Event('change', { bubbles: true }));
    }
  });

  Object.defineProperties(el, {
    value: {
      get: () => text,
      set: (v) => {
        text = String(v == null ? '' : v).replace(/\r\n?/g, '\n');
        history = [{ text, start: text.length, end: text.length }];
        at = 0;
        lastKind = '';
        focusText = text;
        fresh = true;
        const focused = el.ownerDocument.activeElement === el;
        draw(focused ? text.length : null, focused ? text.length : null);
      }
    },
    placeholder: {
      get: () => placeholder,
      set: (v) => {
        placeholder = String(v == null ? '' : v);
        el.setAttribute('data-placeholder', placeholder);
        if (!options.label) el.setAttribute('aria-label', placeholder);
      }
    },
    disabled: {
      get: () => disabled,
      set: (v) => {
        disabled = !!v;
        editable();
        el.classList.toggle('disabled', disabled);
        el.setAttribute('aria-disabled', String(disabled));
      }
    }
  });

  /*
      Le focus met le curseur à la fin d'un texte que l'application a donné, et d'un champ dont la sélection est
      ailleurs, comme le fait un textarea ; un champ que l'utilisateur a modifié garde sa sélection.
  */
  const focus = HTMLElement.prototype.focus;
  el.focus = function (opts) {
    focus.call(el, opts);
    const r = readField(el);
    if (fresh || r.start < 0) {
      selectRange(el, text.length, text.length);
      reveal(el);
    }
    fresh = false;
  };
  el.addEventListener('pointerdown', () => { fresh = false; });

  editable();
  el.placeholder = options.placeholder || '';
  draw(null, null);
  return el;
}
