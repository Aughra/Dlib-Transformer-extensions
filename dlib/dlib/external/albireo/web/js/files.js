// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les fichiers et les images qu'on joint (fork pour macOS : sortis d'app.js) : les documents du Mac, la lecture des
// fichiers choisis, les images des questions et leurs places dans le contexte, les vignettes et la visionneuse, le
// collage et le glisser-déposer.

import * as api from './api.js';
import * as images from './images.js';
import * as store from './store.js';
import { closeMenu, confirmDialog, dialog, showMenu } from './dialogs.js';
import { errorText } from './app.js';
import { inputsLabel, modelName, roleInputs, roleModel } from './status.js';
import { escapeHtml } from './markdown.js';
import { extractText, fileKind, maxFileBytes, pdfPages } from './documents.js';
import { formatLimit, formatNumber, formatSize, h, iconButton, toast } from './dom.js';
import { icon } from './icons.js';
import { language, t, tn } from './i18n.js';
import { maxContextImages, maxProjectImages, maxProjectItems } from './context.js';
import { state, ui } from './state.js';

// Les documents du Mac (fork pour macOS).
/*
    Chaque fichier joint dans la page va aussi, une fois, dans le dossier des documents du Mac : les fichiers qui
    viennent de ce dossier sont marqués fromMac, et ceux déjà envoyés pendant cette session sont retenus par leur nom,
    leur taille et leur date.  Un échec est signalé, et le fichier reste joint malgré tout.
*/
const sentToMac = new Set();

function copyToMac(f) {
  if (!f || f.fromMac || typeof f.size !== 'number') return;
  const key = [f.name, f.size, f.lastModified].join('|');
  if (sentToMac.has(key)) return;
  sentToMac.add(key);
  fetch('/albireo/v1/documents?name=' + encodeURIComponent(f.name), {
    method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: f
  }).then(async (r) => {
    if (!r.ok) {
      let message = r.status + ' ' + r.statusText;
      try { message = (await r.json()).error.message || message; } catch (e) { /* pas du JSON */ }
      throw new Error(message);
    }
  }).catch((e) => {
    sentToMac.delete(key);
    toast(t('macDocsCopyFailed', { name: f.name, error: e.message }), true, 8000);
  });
}

// Le menu d'un bouton qui joint des fichiers : depuis cet appareil (input), ou depuis les documents du Mac.
export function attachMenu(anchor, input, onFiles) {
  showMenu(anchor, [
    { icon: 'upload', label: t('fromDevice'), action: () => input.click() },
    { icon: 'folder', label: t('macDocs'), action: () => macDocumentsDialog(onFiles) }
  ]);
}

/*
    Les documents du Mac : une liste à cocher, les fichiers cochés donnés à onFiles comme objets File marqués fromMac,
    un bouton qui envoie au Mac des fichiers de cet appareil, et la suppression d'un document.
*/
function macDocumentsDialog(onFiles) {
  return dialog((box, close) => {
    const list = h('div', { class: 'mac-docs' }, h('p', { class: 'help' }, '…'));
    const picked = new Set();
    const add = h('button', { class: 'button primary', type: 'button', disabled: true }, t('macDocsAdd'));
    const sendInput = h('input', { type: 'file', multiple: true, class: 'hidden' });
    const date = (sec) => new Intl.DateTimeFormat(language(), { dateStyle: 'medium', timeStyle: 'short' })
      .format(new Date(sec * 1000));
    const refresh = async () => {
      let r;
      try {
        r = await api.getJSON('/albireo/v1/documents');
      } catch (e) {
        list.replaceChildren(h('p', { class: 'help' }, e.message));
        return;
      }
      const docs = (r.documents || []).sort((a, b) => b.modified - a.modified);
      if (!docs.length) {
        list.replaceChildren(h('p', { class: 'help' }, t('macDocsEmpty', { folder: r.folder })));
        return;
      }
      list.replaceChildren(...docs.map((d) => {
        const box = h('input', { type: 'checkbox', checked: picked.has(d.name) });
        box.addEventListener('change', () => {
          if (box.checked) picked.add(d.name);
          else picked.delete(d.name);
          add.disabled = !picked.size;
        });
        return h('label', { class: 'mac-doc' }, box,
          h('span', { class: 'name' }, d.name),
          h('span', { class: 'meta' }, formatSize(d.size) + ' · ' + date(d.modified)),
          iconButton('trash', t('macDocsDelete'), async (e) => {
            e.preventDefault();
            if (!await confirmDialog(t('macDocsDelete'), d.name)) return;
            await fetch('/albireo/v1/documents/' + encodeURIComponent(d.name), { method: 'DELETE' });
            picked.delete(d.name);
            add.disabled = !picked.size;
            refresh();
          }));
      }));
    };
    sendInput.addEventListener('change', async () => {
      const files = Array.from(sendInput.files);
      sendInput.value = '';
      for (const f of files) {
        try {
          const r = await fetch('/albireo/v1/documents?name=' + encodeURIComponent(f.name), {
            method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: f
          });
          if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
          sentToMac.add([f.name, f.size, f.lastModified].join('|'));
          toast(t('macDocsSent', { name: (await r.json()).name || f.name }));
        } catch (e) {
          toast(t('macDocsCopyFailed', { name: f.name, error: e.message }), true, 8000);
        }
      }
      refresh();
    });
    add.addEventListener('click', async () => {
      const files = [];
      for (const name of picked) {
        try {
          const r = await fetch('/albireo/v1/documents/' + encodeURIComponent(name), { cache: 'no-store' });
          if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
          const blob = await r.blob();
          const f = new File([blob], name, { type: blob.type });
          f.fromMac = true;
          files.push(f);
        } catch (e) {
          toast(name + ' : ' + e.message, true);
        }
      }
      close(true);
      if (files.length) onFiles(files);
    });
    box.append(h('h2', { html: icon('folder') + escapeHtml(t('macDocs')) }), h('p', { class: 'help' }, t('macDocsHelp')),
      list, h('div', { class: 'actions' },
        h('button', { class: 'button', type: 'button', onclick: () => sendInput.click(),
          html: icon('upload') + '<span>' + escapeHtml(t('macDocsSend')) + '</span>' }), sendInput,
        h('span', { class: 'grow' }),
        h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('cancel')), add));
    refresh();
  });
}

// Fichiers.
/*
    Lit les fichiers joints pour le modèle de role (les documents et les images d'un projet pour
    none) : les documents deviennent du texte ; une image est jointe quand le modèle lit les images,
    toujours à un projet, et préparée (images.js) dans la limite de room places, les images au-delà
    étant refusées avec un message qui dit pourquoi ; un son n'est joint que quand le modèle le lit.
    Un PDF dont les pages sont des images sans texte, un scan, est laissé de côté avec une boîte de
    dialogue, ou proposé comme images de ses pages à un modèle qui lit les images ; un PDF dont
    certaines pages sont de telles images est ajouté avec un avertissement qui les compte.  Les
    images reviennent avec kind 'image'.  Un document de plus de maxFileBytes et une image de plus
    de images.maxImageBytes sont refusés avant d'être lus, avec un message qui nomme le fichier et
    la limite.
*/
export async function readFiles(files, role, room, most) {
  const out = [];
  const scans = [];
  const refused = [];
  const overflow = [];
  const inputs = role ? roleInputs(role) : ['text', 'image'];
  let left = room || 0;
  for (const f of files) {
    // La connaissance d'un projet, qui prend au plus most éléments de plus.
    if (most !== undefined && out.length >= most) {
      overflow.push(f.name);
      continue;
    }
    const kind = fileKind(f);
    if (kind !== 'document' && !inputs.includes(kind)) {
      const model = role ? modelName(roleModel(role)) : '';
      toast(role ? t('inputRefused', { name: f.name, model, inputs: inputsLabel(inputs) })
        : t('unsupported', { name: f.name }), true);
      continue;
    }
    const limit = kind === 'image' ? images.maxImageBytes : maxFileBytes;
    if (f.size > limit) {
      toast(t(kind === 'image' ? 'imageTooLarge' : 'tooLarge',
        { name: f.name, size: formatSize(f.size), limit: formatLimit(limit) }), true, 9000);
      continue;
    }
    // Une copie dans les documents du Mac.
    copyToMac(f);
    if (kind === 'image') {
      if (left <= 0) {
        refused.push(f.name);
        continue;
      }
      try {
        out.push(Object.assign({ kind: 'image' }, await images.prepare(f)));
        --left;
      } catch (e) {
        toast(t('imageUnreadable', { name: f.name }), true);
      }
      continue;
    }
    try {
      const r = await extractText(f);
      out.push({ id: store.uid(), name: f.name, size: f.size, format: r.format, text: r.text, added: Date.now() });
      if (r.unreadablePages) {
        toast(tn('pdfPagesWithoutText', r.unreadablePages, { total: r.pages, name: f.name }), false, 9000);
      }
    } catch (e) {
      if (e.message === 'pdf-images') scans.push({ file: f, pages: e.pages || 0 });
      else if (e.message === 'unsupported') toast(t('unsupported', { name: f.name }), true);
      else if (e.message === 'legacy') toast(t('legacyFormat', { name: f.name }), true);
      else if (e.message === 'too-large') {
        toast(t('tooLarge', { name: f.name, size: formatSize(f.size), limit: formatLimit(maxFileBytes) }), true, 9000);
      }
      else if (e.message === 'too-large-content') toast(t('tooLargeContent', { name: f.name }), true);
      else if (e.message === 'pdf-password') toast(t('pdfPassword', { name: f.name }), true, 9000);
      else if (e.message === 'pdf-invalid') toast(t('pdfInvalid', { name: f.name }), true);
      else if (e.message === 'pdf-reader') toast(t('pdfReader', { name: f.name }), true, 12000);
      else toast(t('readFailed', { name: f.name, error: errorText(e) }), true);
    }
  }
  if (refused.length) toast(imageLimitText(refused, left, !role), true, 10000);
  if (overflow.length) toast(itemLimitText(overflow, most - out.length), true, 10000);
  for (const scan of scans) {
    if (role && inputs.includes('image')) {
      const pages = await scanPages(scan, left);
      out.push(...pages);
      left -= pages.length;
    } else {
      scanDialog(scan.file.name);
    }
  }
  return out;
}

/*
    Propose de joindre les pages d'un PDF scanné comme images, à un modèle qui les lit, dans la
    limite des places restantes pour les images : les pages sont dessinées par pdf.js, leur plus
    grand côté à images.maxSide pixels, les premières quand les places ne les couvrent pas toutes.
    Renvoie les images des pages, aucune quand l'utilisateur refuse ou quand il ne reste aucune
    place, ce que dit la boîte de dialogue.
*/
async function scanPages(scan, left) {
  const name = scan.file.name;
  const count = Math.min(scan.pages || left, left);
  const accepted = await dialog((box, close) => {
    const text = left > 0 ? t('scanOffer', { name, pages: scan.pages || '?', left })
      + (scan.pages > left ? ' ' + tn('scanFirstPages', count) : '')
      : t('scanNoRoom', { name, max: maxContextImages });
    box.append(h('h2', { html: icon('image') + escapeHtml(t('pdfImagesTitle')) }), h('p', null, text),
      h('div', { class: 'actions' }, left > 0 ? [
        h('button', { class: 'button', type: 'button', onclick: () => close(false) }, t('cancel')),
        h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('scanAttach'))
      ] : h('button', { class: 'button primary', type: 'button', onclick: () => close(false) }, t('close'))));
  }, true);
  if (!accepted || count <= 0) return [];
  toast(t('scanRendering', { name }));
  try {
    const r = await pdfPages(scan.file, images.maxSide, count);
    const out = [];
    const base = name.replace(/\.pdf$/i, '');
    for (let i = 0; i < r.canvases.length; ++i) {
      const meta = await images.prepareCanvas(r.canvases[i], t('scanPageName', { name: base, n: i + 1 }));
      out.push(Object.assign({ kind: 'image' }, meta));
    }
    return out;
  } catch (e) {
    toast(t('readFailed', { name, error: errorText(e) }), true);
    return [];
  }
}

// La boîte de dialogue qui dit qu'un PDF d'images n'a pas été ajouté, affichée une fois la précédente fermée.
let scanDialogs = Promise.resolve();

function scanDialog(name) {
  scanDialogs = scanDialogs.then(() => dialog((box, close) => {
    box.append(h('h2', { html: icon('image') + escapeHtml(t('pdfImagesTitle')) }),
      h('p', null, t('pdfImagesText', { name })), h('div', { class: 'actions' },
        h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('close'))));
  }, true));
}

// Images.
// Les ids des images des questions d'une liste de messages.
export function imageIds(messages) {
  const ids = new Set();
  for (const x of messages) for (const img of x.images || []) ids.add(img.id);
  return ids;
}

// Les images des questions de c à partir du message from, le début de son contexte par défaut.
export function contextImages(c, from) {
  let n = 0;
  if (!c) return n;
  for (let i = from == null ? c.contextStart || 0 : from; i < c.messages.length; ++i) {
    n += (c.messages[i].images || []).length;
  }
  return n;
}

export function pendingImageCount() {
  return state.pending.filter((a) => a.kind === 'image').length;
}

// Les places restantes pour les images dans le contexte de c, les images en attente dans la zone de saisie déduites.
export function imageRoom(c) {
  return c ? Math.max(0, maxContextImages - contextImages(c) - pendingImageCount()) : 0;
}

// Pourquoi des images n'ont pas été ajoutées : leurs noms, la limite d'une discussion ou d'un projet, et les places
// restantes.
export function imageLimitText(names, left, project) {
  return t(project ? 'projectImagesNotAdded' : 'imagesNotAdded', {
    max: project ? maxProjectImages : maxContextImages, left: Math.max(0, left),
    notAdded: tn('notAddedImages', names.length, { names: names.join(', ') })
  });
}

// Le message des fichiers laissés hors de la connaissance d'un projet, dont les éléments atteignent maxProjectItems.
export function itemLimitText(names, left) {
  const n = formatNumber(names.length);
  const notAdded = names.length > 8 ? t('notAddedItemsAmong', { n, names: names.slice(0, 8).join(', ') })
    : tn('notAddedItems', names.length, { n, names: names.join(', ') });
  return t('projectItemsNotAdded', { max: maxProjectItems, left: Math.max(0, left), notAdded });
}

// La description d'une image telle qu'une question ou un projet la garde.
export function imageRecord(a) {
  return { id: a.id, name: a.name, type: a.type, size: a.size, width: a.width, height: a.height, added: a.added };
}

function imageLabel(img) {
  return img.name + ' · ' + img.width + ' × ' + img.height + ' · ' + formatSize(img.size || 0);
}

// L'infobulle d'une vignette, avec la raison pour laquelle son image n'atteint plus le modèle.
function thumbTitle(button, outside) {
  button.classList.toggle('outside', outside);
  button.title = button.dataset.label + (outside ? '\n' + t('imageOutside') : '');
  button.setAttribute('aria-label', t('imageOpen', { name: button.dataset.name }) + (outside ? '. ' + t('imageOutside') : ''));
}

/*
    La vignette d'une image, un bouton qui l'ouvre dans la visionneuse.  options : list, les images
    que la visionneuse parcourt ; outside, pour une image dont l'échange a quitté le contexte,
    affichée grisée ; onremove, qui ajoute le bouton qui retire l'image.
*/
export function imageThumb(img, options) {
  const o = options || {};
  const list = o.list || [img];
  const pic = h('img', { alt: '', draggable: 'false' });
  const button = h('button', { type: 'button', class: 'image-thumb', onclick: () => openViewer(list, list.indexOf(img), button) },
    pic);
  button.dataset.image = img.id;
  button.dataset.name = img.name;
  button.dataset.label = imageLabel(img);
  thumbTitle(button, !!o.outside);
  images.thumbUrl(img.id).then((url) => {
    if (url) {
      pic.src = url;
      return;
    }
    // Une image que ce navigateur ne contient pas, comme une image dont l'enregistrement a échoué.
    button.classList.add('missing');
    button.dataset.label = img.name + ' · ' + t('imageMissing');
    thumbTitle(button, !!o.outside);
    pic.replaceWith(h('span', { html: icon('image') }));
  });
  if (!o.onremove) return button;
  return h('span', { class: 'image-pending' }, button, h('button', {
    type: 'button', class: 'image-remove', title: t('removeImage'), 'aria-label': t('removeImage') + ': ' + img.name,
    html: icon('close'), onclick: o.onremove
  }));
}

/*
    Affiche des images en entier par-dessus la page, à partir de celle d'indice index dans list.
    Échap, un clic à côté de l'image et le bouton de fermeture ferment la visionneuse, les flèches
    parcourent les images, et le focus reste dans la visionneuse, puis revient à opener.
*/
function openViewer(list, index, opener) {
  closeMenu();
  let at = Math.max(0, index);
  let sequence = 0;
  const pic = h('img', { class: 'viewer-image', alt: '' });
  const missing = h('div', { class: 'viewer-missing hidden' }, h('span', { html: icon('image') }), t('imageMissing'));
  const name = h('span', { class: 'viewer-name' });
  const info = h('span', { class: 'viewer-info' });
  const stage = h('div', { class: 'viewer-stage' }, pic, missing);
  const buttons = [];
  const show = async (k) => {
    at = (k + list.length) % list.length;
    const img = list[at];
    const mine = ++sequence;
    name.textContent = img.name;
    info.textContent = img.width + ' × ' + img.height + ' · ' + formatSize(img.size || 0)
      + (list.length > 1 ? ' · ' + (at + 1) + ' / ' + list.length : '');
    box.setAttribute('aria-label', img.name);
    pic.alt = img.name;
    // L'image que montre la visionneuse garde son URL d'objet (releaseHiddenImages).
    pic.dataset.image = img.id;
    const url = await images.fullUrl(img.id);
    if (mine !== sequence) return;
    pic.classList.toggle('hidden', !url);
    missing.classList.toggle('hidden', !!url);
    if (url) pic.src = url;
    else pic.removeAttribute('src');
  };
  if (list.length > 1) {
    buttons.push(iconButton('chevron', t('previousImage'), () => show(at - 1), 'icon-button viewer-prev'),
      iconButton('chevron', t('nextImage'), () => show(at + 1), 'icon-button viewer-next'));
  }
  const closeButton = iconButton('close', t('close'), () => done(), 'icon-button viewer-close');
  buttons.push(closeButton);
  const box = h('div', { class: 'viewer', role: 'dialog', 'aria-modal': 'true', tabindex: '-1' },
    h('div', { class: 'viewer-bar' }, h('span', { class: 'viewer-title' }, name, info), buttons), stage);
  const overlay = h('div', { class: 'overlay viewer-overlay' }, box);
  // Les touches atteignent d'abord la visionneuse, où que soit le focus, et ne vont pas plus loin.
  const onKey = (e) => {
    if (e.key === 'Escape') done();
    else if (e.key === 'ArrowLeft' && list.length > 1) show(at - 1);
    else if (e.key === 'ArrowRight' && list.length > 1) show(at + 1);
    else if (e.key === 'Tab') {
      const i = buttons.indexOf(document.activeElement);
      buttons[i < 0 ? 0 : (i + (e.shiftKey ? buttons.length - 1 : 1)) % buttons.length].focus();
    } else return;
    e.preventDefault();
    e.stopPropagation();
  };
  const done = () => {
    document.removeEventListener('keydown', onKey, true);
    overlay.remove();
    if (opener && opener.isConnected) opener.focus();
  };
  overlay.addEventListener('mousedown', (e) => {
    if (e.target === overlay || e.target === stage) done();
  });
  document.addEventListener('keydown', onKey, true);
  document.body.append(overlay);
  show(at);
  closeButton.focus();
}

// Grise les images des questions qui ont quitté le contexte, dans le fil affiché.
export function markOutside() {
  const c = state.current;
  if (!c || !ui.thread || !ui.thread.isConnected) return;
  const start = c.contextStart || 0;
  for (const el of ui.thread.querySelectorAll('.message.user[data-index]')) {
    const outside = Number(el.dataset.index) < start;
    for (const b of el.querySelectorAll('.image-thumb')) thumbTitle(b, outside);
  }
}

/*
    Les fichiers d'un collage.  Une image du presse-papiers, une capture d'écran par exemple, prend
    un nom avec l'heure du collage au lieu de celui que le navigateur leur donne à toutes.
*/
export function pastedFiles(e) {
  const files = Array.from(e.clipboardData ? e.clipboardData.files : []);
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  const date = d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + '.'
    + pad(d.getMinutes()) + '.' + pad(d.getSeconds());
  return files.map((f, i) => {
    if (fileKind(f) !== 'image' || !/^(image\.[a-z0-9]+)?$/i.test(f.name || '')) return f;
    const ext = ((f.type || '').split('/')[1] || 'png').replace('jpeg', 'jpg');
    const name = t('pastedImage', { date }) + (files.length > 1 ? ' (' + (i + 1) + ')' : '') + '.' + ext;
    return new File([f], name, { type: f.type, lastModified: f.lastModified });
  });
}

// Si le glisser de l'événement e porte des fichiers, venant du système ou d'une autre application.
function carriesFiles(e) {
  return !!e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files');
}

/*
    Fait prendre à el les fichiers déposés sur lui, donnés à onFiles, avec la classe drag sur el pendant que des
    fichiers sont glissés au-dessus.  Le pointeur qui traverse les enfants de el entre dans chaque enfant avant de
    quitter l'élément sur lequel il était : el compte les entrées et les sorties du glisser, et perd la classe une fois
    qu'elles s'équilibrent.  Un glisser sans fichiers, d'un texte ou d'un lien, est laissé au navigateur.
*/
export function dropFiles(el, onFiles) {
  let depth = 0;
  const end = () => {
    depth = 0;
    el.classList.remove('drag');
  };
  el.addEventListener('dragenter', (e) => {
    if (!carriesFiles(e)) return;
    e.preventDefault();
    ++depth;
    el.classList.add('drag');
  });
  el.addEventListener('dragover', (e) => {
    if (carriesFiles(e)) e.preventDefault();
  });
  el.addEventListener('dragleave', (e) => {
    if (carriesFiles(e) && --depth <= 0) end();
  });
  el.addEventListener('drop', (e) => {
    if (!carriesFiles(e)) return;
    e.preventDefault();
    end();
    if (e.dataTransfer.files.length) onFiles(Array.from(e.dataTransfer.files));
  });
}

/*
    Un fichier déposé là où rien ne le prend serait ouvert par le navigateur à la place de l'application : hors des
    endroits qui prennent les fichiers (la zone de saisie, la modification d'une question, la connaissance d'un projet),
    qui annulent les événements du glisser avant que la fenêtre les reçoive, un glisser de fichiers montre qu'il ne peut
    pas être déposé, et son dépôt est annulé.
*/
export function guardFileDrops() {
  window.addEventListener('dragover', (e) => {
    if (e.defaultPrevented || !carriesFiles(e)) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'none';
  });
  window.addEventListener('drop', (e) => {
    if (!e.defaultPrevented && carriesFiles(e)) e.preventDefault();
  });
}
