# La discussion de Gulliver en ligne : Cloudflare Pages, le calcul sur le Mac

Plan et raisons : [docs/plan-discussion-en-ligne.md](../docs/plan-discussion-en-ligne.md). Ce dossier contient tout ce qui part chez Cloudflare :

| Fichier | Rôle |
|---|---|
| `build.sh` | assemble la page de Gulliver (`dlib/dlib/external/gulliver/web`) dans `dist/` |
| `functions/[[path]].js` | la passerelle : sert les fichiers avec les en-têtes de sécurité du service, et transmet au Mac les seuls appels permis |
| `lib/routes.js` | la liste des routes permises ; tout le reste est refusé (403) |
| `lib/headers.js` | les en-têtes de sécurité, copiés de `host/server.cpp` |
| `test/routes.test.mjs` | `node --test cloudflare/test/` : la liste, les chemins déguisés, le classement de chaque route du service, les en-têtes |
| `test/page_remote.json` | le scénario de la page à travers la passerelle (voir « Essayer sans Cloudflare ») |
| `tunnel/install_tunnel.sh` | lance le tunnel au démarrage du Mac (LaunchAgent) |

Adresses prises en exemple ci-dessous : `gulliver.tortuetech.com` pour la page, `mac-gulliver.tortuetech.com` pour le tunnel. N'importe quels noms du domaine conviennent.

## Ce qui se fait dans le tableau de bord Cloudflare (par toi)

L'ordre compte : le tunnel n'est jamais ouvert sans sa règle Access.

1. **La règle du tunnel d'abord.** Zero Trust → Access → Service auth → *Service Tokens* → *Create Service Token*, nom `gulliver-passerelle`. Note l'identifiant et le secret (le secret ne se montre qu'une fois). Puis Access → Applications → *Add an application* → *Self-hosted*, domaine `mac-gulliver.tortuetech.com`, une seule règle : *Action* **Service Auth**, *Include* **Service Token** = `gulliver-passerelle`. Aucune règle pour des personnes.
2. **Le tunnel.** Zero Trust → Networks → Tunnels → *Create a tunnel* → *Cloudflared*, nom `gulliver-mac`. Copie le jeton affiché (la longue chaîne après `--token` ou `install`) dans un fichier du Mac, sans le coller ailleurs :

   ```sh
   mkdir -p ~/keys && pbpaste > ~/keys/cloudflare-tunnel-gulliver.token && chmod 600 ~/keys/cloudflare-tunnel-gulliver.token
   ```

   Puis, toujours dans le tunnel, *Public Hostname* → *Add* : sous-domaine `mac-gulliver`, domaine `tortuetech.com`, *Service* **HTTPS** `localhost:18443`, et dans *Additional application settings → TLS*, **No TLS Verify** activé (le certificat du service est fait maison ; ce saut-là ne quitte pas le Mac).
3. **Le projet Pages.** Workers & Pages → *Create* → *Pages* → *Connect to Git*, dépôt `Aughra/Dlib-Transformer-extensions`, branche `main`. *Build settings* : *Framework preset* **None**, *Build command* `bash build.sh`, *Build output directory* `dist`, *Root directory* `cloudflare`. Dans *Settings → Variables and Secrets* (production) : `MAC_ORIGIN` = `https://mac-gulliver.tortuetech.com` (texte), `CF_ACCESS_CLIENT_ID` et `CF_ACCESS_CLIENT_SECRET` (type **Secret**) avec les valeurs du jeton de service. Puis *Custom domains* → `gulliver.tortuetech.com`.
4. **La règle de la page.** Access → Applications → *Add* → *Self-hosted*, domaine `gulliver.tortuetech.com`, règle *Allow*, *Include* : ton compte GitHub (comme pour `dev.tortuetech.com`). Ajoute aussi l'adresse `*.pages.dev` du projet (ou désactive-la) : sinon la page y serait joignable sans connexion.

## Ce qui se fait sur le Mac

```sh
bash cloudflare/tunnel/install_tunnel.sh          # démarre le tunnel, et à chaque ouverture de session
tail -f ~/Library/Logs/gulliver-tunnel.log        # « Registered tunnel connection » quatre fois : il est en ligne
bash cloudflare/tunnel/install_tunnel.sh --remove # l'arrêter et le retirer
```

`cloudflared` vient de Homebrew (`brew install cloudflared`, déjà fait sur le Mac mini). Le Mac doit rester allumé et sa session ouverte.

## Vérifier, dans cet ordre

```sh
curl -s -o /dev/null -w '%{http_code}\n' https://mac-gulliver.tortuetech.com/gulliver/v1/status   # 302 ou 403 : refusé sans jeton
curl -s -o /dev/null -w '%{http_code}\n' https://gulliver.tortuetech.com/                          # 302 : la connexion GitHub est demandée
```

Puis, depuis le téléphone hors du Wi-Fi de la maison : `https://gulliver.tortuetech.com`, connexion GitHub, une question. Les onglets « Modèles » et « Service » n'apparaissent pas ; ils restent sur le Mac.

## Essayer sans Cloudflare, sur le Mac

```sh
bash cloudflare/build.sh
npx wrangler@4 pages dev cloudflare/dist --port 8788 \
  --binding MAC_ORIGIN=http://<adresse du Mac>:8444 --binding CF_ACCESS_CLIENT_ID=essai --binding CF_ACCESS_CLIENT_SECRET=essai
```

(lancé depuis le dossier `cloudflare/`, pour que `functions/` soit pris), puis `http://localhost:8788`. L'accès réseau de la maison (port 8444) doit être ouvert dans l'onglet « Service ».

Le scénario de la page à travers la passerelle (page prête, marque « remote », menu et paramètres sans « Modèles » ni « Service », une question et sa réponse en flux), dans un profil d'essai créé puis supprimé :

```sh
ID=$(curl -s -H 'Content-Type: application/json' -d '{"name":"essai-passerelle"}' http://localhost:8788/gulliver/v1/profiles | build/macos-gulliver/bin/json_get - id)
PAGE_TEST_WORK=$(mktemp -d) PAGE_URL=http://localhost:8788/ PAGE_TEST_PROFILE=$ID \
  node dlib/dlib/external/gulliver/tools/page_test/cdp.mjs cloudflare/test/page_remote.json
curl -s -X DELETE http://localhost:8788/gulliver/v1/profiles/$ID
```

Après un changement de la page, relancer `bash cloudflare/build.sh` : `wrangler` sert `dist/`, pas `web/`.

## Ce qu'il faut savoir

- Cloudflare voit le contenu des conversations au passage (Access et la passerelle déchiffrent le trafic pour le filtrer) ; il ne les garde pas.
- 100 Mo au plus par requête ; une réponse muette pendant 100 s est coupée (le flux envoie un signal d'attente).
- La passerelle refuse toute route qu'elle ne connaît pas : une route nouvelle du service fait échouer `node --test cloudflare/test/` tant qu'elle n'est pas classée dans `lib/routes.js`.
