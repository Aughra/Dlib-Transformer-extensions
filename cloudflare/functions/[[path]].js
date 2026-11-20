// La passerelle de Gulliver sur Cloudflare Pages (plan : docs/plan-discussion-en-ligne.md).
//
// Les fichiers de la page sont servis par Pages ; les appels à l'interface (/v1/…, /gulliver/v1/…)
// partent vers le Mac par le tunnel, seulement s'ils sont dans la liste de lib/routes.js. Le tunnel
// n'accepte que le jeton de service de Cloudflare Access, que seule cette fonction connaît.
//
// Variables du projet Pages :
//   MAC_ORIGIN              l'adresse du tunnel, par exemple https://mac-gulliver.tortuetech.com
//   CF_ACCESS_CLIENT_ID     l'identifiant du jeton de service (secret)
//   CF_ACCESS_CLIENT_SECRET le secret du jeton de service (secret)

import { allowed, isApiPath } from '../lib/routes.js';
import { securityHeaders } from '../lib/headers.js';

// Les en-têtes qui ne partent pas vers le Mac : ceux de Cloudflare et de la connexion, et le cookie
// de la session Access de l'utilisateur, qui n'a rien à faire sur le Mac.
const DROPPED = ['host', 'cookie', 'cf-access-jwt-assertion', 'cf-connecting-ip', 'cf-ipcountry', 'cf-ray',
  'cf-visitor', 'x-forwarded-for', 'x-forwarded-proto', 'x-real-ip', 'connection', 'content-length'];

function refuse(status, message) {
  return new Response(JSON.stringify({ error: { message, type: 'gateway', code: 'gateway_refused' } }), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'X-Gulliver-Remote': '1' },
  });
}

export async function onRequest(context) {
  const { request, env } = context;
  const url = new URL(request.url);
  if (!isApiPath(url.pathname)) {
    // Un fichier de la page : servi par Pages, avec les en-têtes de sécurité du service.
    const file = await context.next();
    const h = new Headers(file.headers);
    for (const [k, v] of Object.entries(securityHeaders(url.pathname))) h.set(k, v);
    return new Response(file.body, { status: file.status, statusText: file.statusText, headers: h });
  }

  if (!allowed(request.method, url.pathname))
    return refuse(403, "Cette action ne se fait que depuis le Mac (ou le réseau de la maison).");
  if (!env.MAC_ORIGIN || !env.CF_ACCESS_CLIENT_ID || !env.CF_ACCESS_CLIENT_SECRET)
    return refuse(503, "La passerelle n'est pas réglée (MAC_ORIGIN et le jeton de service).");

  const headers = new Headers();
  for (const [k, v] of request.headers) if (!DROPPED.includes(k.toLowerCase())) headers.set(k, v);
  headers.set('CF-Access-Client-Id', env.CF_ACCESS_CLIENT_ID);
  headers.set('CF-Access-Client-Secret', env.CF_ACCESS_CLIENT_SECRET);
  headers.set('X-Gulliver-Remote', '1');

  const target = new URL(url.pathname + url.search, env.MAC_ORIGIN);
  const hasBody = !['GET', 'HEAD'].includes(request.method);
  let reply;
  try {
    reply = await fetch(target, {
      method: request.method,
      headers,
      body: hasBody ? request.body : undefined,
      redirect: 'manual',
    });
  } catch (e) {
    return refuse(502, "Le Mac ne répond pas (éteint, en veille, ou tunnel arrêté).");
  }

  // La réponse repart telle quelle, en flux (les réponses de la discussion arrivent mot à mot),
  // avec la marque qui dit à la page qu'elle passe par la passerelle.
  const out = new Headers(reply.headers);
  out.set('X-Gulliver-Remote', '1');
  out.delete('set-cookie');
  return new Response(reply.body, { status: reply.status, statusText: reply.statusText, headers: out });
}
