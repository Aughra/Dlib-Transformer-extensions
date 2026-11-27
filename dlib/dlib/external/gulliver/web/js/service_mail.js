// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Le mail des comptes » de l'onglet Service : l'adresse d'envoi, le port et le mot de passe de Proton Mail Bridge, le lien des mails.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema } from './service_commun.js';

export const items = {
  mail_from: {
    title: 'Adresse d’envoi',
    control: { kind: 'text', placeholder: 'vide : pas de mail' },
    restart: false,
    effect: 'L’adresse d’où partent les codes de connexion et les liens. Vide : personne ne peut se connecter.',
    principle: 'Gulliver confie ses mails à Proton Mail Bridge, qui les envoie depuis cette adresse.',
    pros: ['Les codes et les liens partent d’une adresse que tu connais : pas de service en plus.',
      'Proton chiffre le message avant qu’il quitte le Mac.'],
    cons: ['Il faut que Proton Mail Bridge tourne sur le Mac, avec cette adresse dans son compte.',
      'Vide ou fausse, plus personne ne reçoit de code : la connexion échoue (« le mail n’a pas pu partir »).'],
    normal: "Pour se connecter, on tape son adresse et son mot de passe, puis un code à 8 chiffres reçu par mail. " +
      "Ce réglage dit de quelle adresse part ce mail. Elle doit appartenir au compte Proton ouvert dans Proton Mail " +
      "Bridge sur le Mac. Elle sert aussi de nom d’utilisateur auprès du Bridge.",
    owl: "mail_from, normalisée en minuscules, validée par valid_mail_address (host/mailer.cpp). send_mail ouvre une " +
      "prise TCP vers 127.0.0.1:mail_port, EHLO, AUTH PLAIN (mail_from, mail_secret) si un mot de passe est rangé, " +
      "MAIL FROM, RCPT TO, DATA : texte UTF-8 en base64, sujet RFC 2047. Vide : /login, /setup et /password/forgot " +
      "répondent 503 mail_unconfigured.",
    marmot: "C’est l’adresse de l’expéditeur sur l’enveloppe. Si elle n’est pas la tienne chez Proton, le facteur " +
      "refuse de porter la lettre.",
    bee: "Si tu la vides, plus personne ne peut se connecter depuis un appareil neuf : les sessions déjà ouvertes " +
      "restent, mais aucun code ne part. Pour revenir, remets l’adresse depuis le Mac."
  },
  mail_port: {
    title: 'Port de Proton Mail Bridge',
    control: { kind: 'number', min: 1, max: 65535 },
    restart: false,
    effect: 'Le port SMTP du Bridge sur le Mac (1025 par défaut). Le facteur ne parle qu’au Mac lui-même.',
    principle: 'Le Bridge écoute sur une porte du Mac (127.0.0.1) ; Gulliver y dépose ses mails.',
    pros: ['La conversation avec le Bridge ne quitte jamais le Mac : le mot de passe ne passe pas sur le Wi-Fi.'],
    cons: ['Il doit être le même que celui des réglages du Bridge (Paramètres → SMTP).'],
    normal: "Proton Mail Bridge ouvre un petit serveur d’envoi sur le Mac, au port 1025 le plus souvent. Gulliver " +
      "s’y connecte pour lui confier les mails. Si tu changes le port dans le Bridge, change-le ici aussi.",
    owl: "Entier de 1 à 65535. Connexion TCP vers 127.0.0.1 seulement (INADDR_LOOPBACK, aucune autre machine permise), " +
      "en clair sur la boucle locale : le Bridge annonce AUTH PLAIN avant STARTTLS et chiffre lui-même vers Proton. " +
      "Délai de 20 s par échange (SO_RCVTIMEO, SO_SNDTIMEO).",
    marmot: "C’est le numéro de la boîte aux lettres du Bridge, dans le couloir du Mac. Gulliver n’a pas besoin de " +
      "sortir de la maison pour poster.",
    bee: "Un mauvais port : « the mail server 127.0.0.1:… does not answer » dans le journal, et la connexion dit que " +
      "le mail n’a pas pu partir."
  },
  mail_secret: {
    title: 'Mot de passe du Bridge',
    control: { kind: 'text', secret: true, placeholder: 'le mot de passe SMTP du Bridge' },
    restart: false,
    effect: 'Le mot de passe que Proton Mail Bridge donne pour l’envoi (pas celui de ton compte Proton).',
    principle: 'Le Bridge invente un mot de passe à part pour les programmes du Mac ; Gulliver le lui redonne à chaque mail.',
    pros: ['Rangé à part, lisible par le seul compte du service ; la page n’en voit que le masque.'],
    cons: ['Un nouveau mot de passe dans le Bridge (après une réinstallation) doit être recopié ici.'],
    normal: "Dans Proton Mail Bridge, ouvre ton compte, puis « Configurer le client de messagerie » : le Bridge montre " +
      "le nom d’utilisateur (ton adresse) et un mot de passe propre au Bridge. Copie ce mot de passe ici. Ce n’est " +
      "pas le mot de passe de ton compte Proton.",
    owl: "mail_secret, rendu masqué (********) par GET /settings ; gardé dans mail.secret (0600, write_file_restricted) " +
      "avec l’adresse et le port pour lesquels il a été donné ; le masque renvoyé tel quel garde le mot de passe " +
      "enregistré. Envoyé en AUTH PLAIN sur 127.0.0.1 seulement, effacé de la mémoire après usage (wipe_secret).",
    marmot: "C’est la clé de la boîte aux lettres du Bridge. Gulliver la garde dans un tiroir fermé, et la page ne " +
      "voit que des étoiles.",
    bee: "Sans lui, le Bridge refuse les mails (« the mail server refused the login ») ; un faux, de même. Vide-le " +
      "pour l’effacer du Mac."
  },
  mail_link: {
    title: 'Adresse des liens',
    control: { kind: 'text', placeholder: 'vide : le réseau de la maison' },
    restart: false,
    effect: 'L’adresse de la page mise dans les liens « mot de passe oublié ». Vide : celle du réseau de la maison.',
    principle: 'Le lien du mail rouvre Gulliver à cette adresse, avec un jeton qui permet de choisir le mot de passe.',
    pros: ['Vide, le lien marche sur tous les appareils de la maison.',
      'Une adresse en ligne (la discussion par Cloudflare) fait marcher le lien hors de la maison.'],
    cons: ['Vide et réseau de la maison fermé, le lien ne s’ouvre que sur le Mac lui-même.'],
    normal: "Quand quelqu’un a oublié son mot de passe, ou se connecte pour la première fois, il reçoit un lien. Ce " +
      "lien doit mener à Gulliver depuis l’appareil où il lit ses mails. Vide, c’est l’adresse du Mac sur le Wi-Fi " +
      "(http://adresse-du-mac:8444/) ; tu peux y mettre l’adresse en ligne de la discussion.",
    owl: "Une adresse http(s) sans « ? » ni « # » ; le service y ajoute « #reset=<jeton> » (fragment : jamais envoyé " +
      "au serveur ni gardé dans ses journaux ; profile.js le lit puis l’efface de l’adresse). Vide : " +
      "http://<première adresse IPv4>:lan_port/ si lan, sinon https://localhost:port/. Jamais tirée de l’en-tête Host " +
      "de la requête, qu’un autre pourrait choisir pour recevoir le jeton.",
    marmot: "C’est l’adresse écrite sur le carton d’invitation : si elle est fausse, l’invité sonne à la mauvaise porte.",
    bee: "Une adresse fausse : le lien mène nulle part, et il faut en redemander un une fois corrigée. Le jeton vaut " +
      "30 minutes et une seule fois."
  }
};

export const schemas = {
  mail_from: schema('Le code part de chez toi, par Proton',
    box(16, 70, 120, 60, 'Gulliver', 's-blue', 'code, lien') + arrow(136, 100, 236, 100, '127.0.0.1') +
    box(236, 70, 128, 60, 'Bridge', 's-gold', 'chiffre') + arrow(364, 100, 464, 100, 'Proton') +
    box(464, 70, 120, 60, 'Ta boîte', 's-soft', 'le mail') +
    text(300, 170, 'expéditeur : l’adresse réglée ici', 's-f'), 190),
  mail_port: schema('Une porte sur le Mac lui-même',
    `<rect x="150" y="44" width="300" height="140" rx="12" class="s-zone"/>` + text(300, 66, 'le Mac', 's-f') +
    box(170, 86, 110, 70, 'Gulliver', 's-blue') + arrow(280, 121, 330, 121, '1025') +
    box(330, 86, 100, 70, 'Bridge', 's-gold', 'SMTP') +
    text(300, 205, 'rien ne sort avant le Bridge', 's-f'), 220),
  mail_secret: schema('La clé du Bridge, rangée à part',
    box(30, 76, 150, 60, 'settings.json', 's-soft', '********') +
    box(225, 76, 150, 60, 'mail.secret', 's-gold', 'droits 0600') + arrow(375, 106, 445, 106, 'AUTH') +
    box(445, 76, 130, 60, 'Bridge', 's-blue') +
    text(300, 172, 'la page ne voit que le masque', 's-f'), 190),
  mail_link: schema('Le lien du mail ouvre la page',
    box(20, 76, 130, 60, 'Mail', 's-soft', 'le lien') + arrow(150, 106, 250, 106, 'clic') +
    box(250, 76, 150, 60, 'Page', 's-blue', 'adresse réglée') + arrow(400, 106, 480, 106) +
    box(480, 76, 100, 60, 'Mot', 's-gold', 'de passe') +
    text(300, 172, 'vide : http://adresse-du-mac:8444/', 's-f'), 190)
};
