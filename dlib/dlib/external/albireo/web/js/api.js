// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Appels au service local : son interface compatible OpenAI et sa propre interface.

// Une erreur du service, avec le code de sa réponse (model_not_found,
// context_length_exceeded, unloaded...), que l'interface explique dans sa langue.
function serviceError(message, code) {
  const e = new Error(message);
  if (code) e.code = code;
  return e;
}

/*
    L'erreur d'une réponse du service, avec retryAfter, les secondes que son en-tête Retry-After demande d'attendre
    avant de renvoyer la requête (busy, engine_busy, starting, model_loading), quand il les donne.
*/
async function errorOf(r) {
  let e = null;
  try {
    const j = await r.json();
    if (j && j.error) {
      e = typeof j.error === 'string' ? serviceError(j.error, j.code) : serviceError(j.error.message || r.statusText,
        j.error.code);
    }
  } catch (x) {
    // Le corps n'était pas du JSON.
  }
  if (!e) e = serviceError(r.status + ' ' + r.statusText);
  const wait = Number(r.headers.get('Retry-After'));
  if (wait > 0) e.retryAfter = wait;
  return e;
}

export async function getJSON(url, signal) {
  const r = await fetch(url, { cache: 'no-store', signal });
  if (!r.ok) throw await errorOf(r);
  return r.json();
}

// Un signal qui met fin à une requête après ms millisecondes, là où le navigateur en propose un.
function timeout(ms) {
  return typeof AbortSignal.timeout === 'function' ? AbortSignal.timeout(ms) : undefined;
}

/*
    Un signal qui met fin à une requête après ms millisecondes (une TimeoutError), ou dès que signal y met fin (avec
    sa raison) : {signal, done}, done oubliant le minuteur et l'écouteur une fois la requête terminée.
*/
export function limitedSignal(signal, ms) {
  const controller = new AbortController();
  const timer = setTimeout(() => {
    controller.abort(new DOMException('The request did not end within ' + Math.round(ms / 1000) + ' seconds.',
      'TimeoutError'));
  }, ms);
  const stop = () => controller.abort(signal.reason);
  if (signal) {
    if (signal.aborted) stop();
    else signal.addEventListener('abort', stop, { once: true });
  }
  return {
    signal: controller.signal,
    done() {
      clearTimeout(timer);
      if (signal) signal.removeEventListener('abort', stop);
    }
  };
}

export async function putJSON(url, body, signal) {
  const r = await fetch(url, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal
  });
  if (!r.ok) throw await errorOf(r);
  return r.json();
}

export async function postJSON(url, body, signal) {
  const r = await fetch(url, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal
  });
  if (!r.ok) throw await errorOf(r);
  return r.json();
}

/*
    Diffuse en flux une complétion de chat.  Produit {type: 'chunk', data} pour chaque morceau de
    l'interface, et {type: 'progress', data} pour l'avancement d'un long prompt, que le
    service envoie sous forme de lignes de commentaire.
*/
export async function* streamChat(body, signal) {
  const r = await fetch('/v1/chat/completions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Accept: 'text/event-stream' },
    body: JSON.stringify(Object.assign({}, body, { stream: true, stream_options: { include_usage: true } })),
    signal
  });
  if (!r.ok) throw await errorOf(r);
  const reader = r.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let finished = false;
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    if (finished) continue;
    buffer += decoder.decode(value, { stream: true });
    let i;
    while (!finished && (i = buffer.indexOf('\n\n')) >= 0) {
      const block = buffer.slice(0, i);
      buffer = buffer.slice(i + 2);
      for (const line of block.split('\n')) {
        if (line.startsWith('data: ')) {
          const payload = line.slice(6);
          // La réponse est lue jusqu'au bout, pour que la connexion reste réutilisable.
          if (payload === '[DONE]') { finished = true; break; }
          const data = JSON.parse(payload);
          if (data.error) throw serviceError(data.error.message || 'error', data.error.code);
          yield { type: 'chunk', data };
        } else if (line.startsWith(': progress ')) {
          try {
            yield { type: 'progress', data: JSON.parse(line.slice(11)) };
          } catch (e) {
            // Une ligne d'avancement mal formée est ignorée.
          }
        }
      }
    }
  }
}

// L'état du service, qu'un service qui ne répond plus laisse inconnu au bout d'un moment.
export function status() {
  return getJSON('/albireo/v1/status', timeout(8000));
}

export function models(lang) {
  return getJSON('/albireo/v1/models?lang=' + encodeURIComponent(lang || 'en'));
}

export function usage() {
  return getJSON('/albireo/v1/usage');
}

// Nombre de tokens d'une liste de messages pour le modèle d'un rôle, et son contexte.
export function count(model, messages, tools) {
  const body = { model, messages };
  if (tools) body.tools = tools;
  return postJSON('/albireo/v1/count', body);
}
