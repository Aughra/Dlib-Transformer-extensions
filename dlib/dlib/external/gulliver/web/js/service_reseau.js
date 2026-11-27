// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Le réseau » de l'onglet Service : l'accès depuis le réseau de la maison et les ports du service.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema } from './service_commun.js';

export const items = {
  lan: {
    title: 'Accès depuis le réseau',
    control: { kind: 'switch', on: true, off: false },
    restart: true,
    effect: 'Ouvre la page aux appareils de la maison (http, port réseau ci-dessous), sans mot de passe.',
    principle: 'Le service ouvre une seconde porte, sur l’adresse du Mac dans ton réseau, pour le téléphone ou la tablette.',
    pros: ['Gulliver depuis le canapé, sur le téléphone ou la tablette, sans rien installer.',
      'Le Mac fait tout le calcul : le téléphone n’affiche que la page.',
      'Chaque profil retrouve ses discussions sur tous les appareils.'],
    cons: ['En http : le trafic n’est pas chiffré sur le Wi-Fi (le mot de passe et le code y passent en clair).',
      'Pas de dictée au micro sur le téléphone (elle exige https).'],
    normal: "Normalement, la page n’est visible que sur le Mac lui-même. Allumé, ce réglage ouvre une seconde entrée " +
      "pour les autres appareils de ton réseau. Chacun s’y connecte avec son adresse mail, son mot de passe et le code " +
      "reçu par mail, et retrouve ses discussions, rangées sur le Mac. Seul l’administrateur y change les modèles et " +
      "les réglages. Le calcul, lui, se fait toujours sur le Mac.",
    owl: "Un second serveur httplib (HTTP, sans TLS) écoute sur 0.0.0.0:lan_port avec les mêmes routes. Les données " +
      "exigent une session (X-Gulliver-Session) ; les actions qui changent le service (require_manager), celle de " +
      "l’administrateur. Le serveur ne répond qu’aux appareils qui peuvent joindre le Mac : ceux du réseau local, " +
      "sauf si la box redirige ce port depuis Internet (elle ne le fait pas d’elle-même).",
    marmot: "C’est une deuxième porte d’entrée, côté jardin. Chacun a sa clé (son mot de passe) et reçoit un code " +
      "par mail pour entrer, mais la porte est en verre : un voisin bricoleur sur le même Wi-Fi peut voir passer ce " +
      "qu’on tape.",
    bee: "Sur un Wi-Fi partagé ou public, ne l’allume pas : le trafic n’y est pas chiffré, et un autre appareil du " +
      "réseau pourrait lire une discussion ou un mot de passe au passage."
  },
  lan_port: {
    title: 'Port réseau',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'Le numéro de la porte réseau. L’adresse à taper sur le téléphone s’affiche plus bas.',
    principle: 'Une adresse réseau a des « ports », comme un immeuble a des portes numérotées. Celui-ci sert aux autres appareils.',
    pros: ['Séparé du port du Mac : on peut couper l’un sans toucher à l’autre.'],
    cons: ['Changer le port change l’adresse à taper sur chaque appareil.'],
    normal: "Le Mac a une adresse sur ton réseau (par exemple 192.168.1.12), et chaque service y a son numéro de porte. " +
      "Gulliver utilise 8444 par défaut pour les autres appareils. On tape donc « http://adresse-du-mac:8444/ » sur le téléphone.",
    owl: "Entier de 1024 à 65535, différent du port HTTPS du Mac. Le service vérifie qu’il peut lier 0.0.0.0 sur ce port " +
      "avant d’enregistrer (409 port_unavailable sinon). macOS peut demander, au premier lancement, d’autoriser les " +
      "connexions entrantes pour gulliver-service (pare-feu applicatif).",
    marmot: "Ton immeuble a une adresse, et chaque appartement un numéro. Les copains doivent connaître les deux pour " +
      "sonner chez toi. Changer de numéro, c’est leur redonner la nouvelle adresse.",
    bee: "Un port déjà pris par un autre programme est refusé. Si tu le changes, les favoris enregistrés sur les " +
      "appareils ne mènent plus nulle part."
  },
  port: {
    title: 'Port du Mac',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'L’adresse de la page sur ce Mac (https://localhost:port). La changer change l’adresse de la page.',
    principle: 'Le service écoute sur ce port, seulement pour le Mac lui-même (127.0.0.1), en https.',
    pros: ['Utile seulement si un autre programme veut ce numéro.'],
    cons: ['Les conversations sont rangées par adresse dans le navigateur : à la nouvelle adresse, la page est vide.',
      'Il faut mettre à jour les favoris et les outils qui appellent l’API.'],
    normal: "C’est la porte d’Gulliver sur le Mac lui-même. La page que tu regardes y est servie. Le navigateur range " +
      "les conversations selon l’adresse exacte : en changeant de port, tu arrives sur une page neuve, et les anciennes " +
      "conversations restent à l’ancienne adresse (exporte-les d’abord depuis l’onglet Données).",
    owl: "Portier TLS de Network.framework lié à 127.0.0.1:port (TLS 1.2 au moins), qui passe le HTTP au serveur httplib " +
      "par une prise Unix privée ; certificat émis par l’autorité locale du dossier de données. " +
      "Le stockage de la page (IndexedDB, localStorage) dépend de l’origine https://localhost:port. Le service vérifie " +
      "que le port est libre avant d’enregistrer ; au démarrage, un port pris le fait revenir au dernier port servi.",
    marmot: "C’est le numéro de ta chambre dans un hôtel. Si tu changes de chambre, tes affaires restent dans " +
      "l’ancienne : il faut les déménager avant.",
    bee: "Sans export préalable, tes conversations semblent avoir disparu (elles sont toujours à l’ancienne adresse). " +
      "Un programme ou un script qui parlait à l’ancien port ne trouve plus le service."
  }
};

export const schemas = {
  lan: schema('Le téléphone sur le canapé, le Mac au bureau',
    box(20, 70, 120, 60, 'Téléphone', 's-soft', 'navigateur') +
    arrow(140, 100, 250, 100, 'Wi-Fi') +
    `<rect x="250" y="56" width="90" height="88" rx="12" class="s-box s-gold"/>` + text(295, 96, 'porte') + text(295, 114, 'ouverte') +
    arrow(340, 100, 430, 100, 'http') +
    box(430, 60, 150, 80, 'Mac', 's-blue', 'http, port 8444') +
    box(430, 160, 150, 40, 'localhost : libre', 's-soft') +
    text(80, 166, 'chaque appareil garde', 's-f') + text(80, 182, 'ses conversations', 's-f'), 215),
  lan_port: schema('Deux portes au même immeuble',
    `<rect x="190" y="44" width="220" height="160" rx="12" class="s-zone"/>` + text(300, 66, 'le Mac', 's-f') +
    box(210, 84, 80, 96, '18443', 's-blue', 'Mac seul') + box(310, 84, 80, 96, '8444', 's-gold', 'réseau') +
    arrow(80, 132, 210, 132, 'localhost') + arrow(520, 132, 390, 132, 'Wi-Fi') +
    text(70, 160, 'le Mac lui-même', 's-f') + text(530, 160, 'les autres appareils', 's-f'), 215),
  port: schema("L'adresse de la page sur le Mac",
    box(30, 80, 150, 56, 'Navigateur', 's-soft') + arrow(180, 108, 330, 108, 'https://localhost:18443') +
    box(330, 80, 150, 56, 'Service', 's-blue') +
    `<rect x="30" y="150" width="150" height="44" rx="9" class="s-box s-gold"/>` + text(105, 170, 'conversations', 's-t') +
    text(105, 186, 'rangées par adresse', 's-f') +
    text(400, 176, 'nouvelle adresse = page vide', 's-f'), 210)
};
