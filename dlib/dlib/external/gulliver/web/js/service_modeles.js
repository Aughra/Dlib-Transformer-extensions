// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Les modèles » de l'onglet Service : le dossier des modèles, la bibliothèque de modèles et la place gardée sur le disque.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema } from './service_commun.js';

export const items = {
  models_dir: {
    title: 'Dossier des modèles',
    control: { kind: 'text', placeholder: 'vide : dans les données du service' },
    restart: true,
    effect: 'Où le service range et cherche ses modèles. Vide : le dossier « models » de ses données.',
    principle: 'Les modèles sont de gros fichiers (2 à 4 Go) : on peut les ranger ailleurs, sur un autre disque.',
    pros: ['Libère le disque du Mac en mettant les modèles sur un disque externe.'],
    cons: ['Les modèles de l’ancien dossier ne sont pas déplacés : il faut le faire dans le Finder.',
      'Disque débranché : plus de modèle au démarrage.'],
    normal: "Le service garde ses modèles dans un dossier. En changer, c’est lui dire de regarder ailleurs : il ne " +
      "déplace rien lui-même. Déplace d’abord les fichiers .gguf dans le nouveau dossier, puis indique-le ici. Le " +
      "dossier doit exister.",
    owl: "models_dir vide : <données>/models (effective_models_dir). Le service vérifie que le dossier existe avant " +
      "d’enregistrer. Les modèles installés sont reconnus par leurs fichiers dans ce dossier ; les poids sont ouverts " +
      "par mmap et pris par le GPU sans copie, donc un disque lent ralentit surtout le chargement.",
    marmot: "C’est le placard où tu ranges tes jeux. Si tu changes de placard, il faut porter les jeux dedans ; sinon " +
      "tu ouvres un placard vide.",
    bee: "Un dossier vide ou un disque débranché, et le service démarre sans modèle : plus de réponses jusqu’à ce que " +
      "les fichiers reviennent."
  },
  library_dir: {
    title: 'Bibliothèque du Mac',
    control: { kind: 'text', placeholder: 'vide : le dossier « library » des données du service' },
    restart: false,
    effect: 'Le dossier de fichiers .gguf que « Rafraîchir depuis le Mac » liste dans l’onglet Modèles, pour les charger sans réseau.',
    principle: 'Des modèles déjà présents sur le Mac, venus d’ailleurs qu’Gulliver, deviennent utilisables même sans Hugging Face.',
    pros: ['Un modèle se charge sans Internet, en quelques secondes : le temps de vérifier son empreinte.',
      'La copie ne prend pas de place sur le même disque (clone APFS), et le fichier reste dans la bibliothèque.',
      'Chaque modèle vient avec son projecteur d’images, quand le dossier le contient.'],
    cons: ['Sur un autre disque que celui des modèles, la copie prend vraiment sa taille.',
      'Le service refuse un dossier que d’autres comptes du Mac peuvent modifier.'],
    normal: "Dépose des fichiers .gguf dans ce dossier avec le Finder, puis clique « Rafraîchir depuis le Mac » dans " +
      "l’onglet Modèles : la section « Sur ce Mac » les liste, avec leur architecture, leur taille et le projecteur " +
      "qui va avec chacun. « Charger dans Gulliver » en fait une copie dans le dossier des modèles. Le service ne " +
      "déplace, ne renomme et n’efface jamais rien dans la bibliothèque. Vide : <données>/library, créé à la première " +
      "lecture. Un autre dossier doit exister et être nommé par son chemin complet.",
    owl: "library_dir vide : <données>/library (effective_library_dir), créé par make_dirs. GET /gulliver/v1/library : " +
      "le dossier doit passer folder_admin_only (propriétaire, permissions POSIX, ACL) ; chaque .gguf du dossier, sans " +
      "sous-dossier, est ouvert par link_free_file (aucun lien, un seul nom) et décrit par describe_gguf ; les " +
      "projecteurs sont appariés par pair_projectors, comme un import. POST /gulliver/v1/library/load {file} : " +
      "model_manager::import_files(fichier et projecteur, move = false), SHA-256 par CommonCrypto, copie par " +
      "fclonefileat. « Déjà dans Gulliver » : un modèle installé de même taille et de même architecture ; l’import " +
      "tranche par l’empreinte.",
    marmot: "C’est l’étagère de ta chambre où tu ranges des jeux que tu as eus ailleurs. Le service regarde l’étagère " +
      "et te montre ce qu’il y a ; si tu choisis un jeu, il en fait une photocopie magique qui ne prend pas de place, " +
      "et laisse l’original sur l’étagère.",
    bee: "Un dossier que n’importe quel compte du Mac peut modifier, et le service ne le lit plus : quelqu’un pourrait " +
      "y glisser un faux modèle. Un dossier sur une clé USB débranchée, et la liste est vide."
  },
  reserve_gb: {
    title: 'Réserve des modèles',
    control: { kind: 'range', values: () => [0, 50, 100, 200, 300, 500, 1000],
      label: (v) => v === 0 ? 'Rien de plus' : v.toLocaleString('fr-FR') + ' Go' },
    restart: false,
    effect: 'La place que peuvent prendre, sur le Mac, les modèles supprimés gardés pour revenir sans réseau.',
    principle: 'Chaque modèle téléchargé est gardé dans la réserve ; le réinstaller devient une copie locale, sans Internet.',
    pros: ['Réinstaller un modèle prend quelques secondes, sans réseau, même si Hugging Face est en panne.',
      'Tant qu’un modèle reste installé, sa copie de réserve ne prend aucune place (un clone APFS).'],
    cons: ['Les modèles supprimés continuent d’occuper le disque, jusqu’au plafond.',
      'Plafond baissé : les copies les plus anciennes partent aussitôt.'],
    normal: "Quand un modèle finit de se télécharger, le service en garde une copie dans le dossier « reserve » de ses " +
      "données. Quand tu supprimes le modèle, la copie reste : le réinstaller la reprend sur le Mac, sans Internet. Le " +
      "plafond ne compte que les copies des modèles supprimés ; au-delà, les moins récemment utilisées partent d’abord. " +
      "« Vider la réserve » retire toutes les copies.",
    owl: "<données>/reserve/<sha256>.gguf, dossier 0700, fichiers 0600. La copie est un clone APFS (fclonefileat) : " +
      "les blocs sont partagés avec le fichier du modèle tant qu’aucun des deux ne change. Chaque copie est relue et " +
      "hachée (SHA-256 par CommonCrypto). Restauration fichier par fichier : en ligne, seuls les fichiers absents ou " +
      "abîmés se téléchargent ; une copie abîmée est retirée. Plafond (reserve_gb) : somme des copies dont l’empreinte " +
      "n’est plus celle d’un modèle installé ; tri par mtime, rafraîchie à chaque restauration (trim_reserve).",
    marmot: "C’est comme ranger un jeu que tu n’utilises plus dans un carton au grenier au lieu de le jeter : si tu " +
      "le veux de nouveau, tu remontes le chercher au lieu de le racheter. Le plafond, c’est la taille du grenier.",
    bee: "À 0, rien n’est gardé : un modèle supprimé se retélécharge en entier (2 à 3 Go). Trop haut, le disque se " +
      "remplit de modèles oubliés sans que tu le voies : regarde la place utilisée sous ce réglage."
  }
};

export const schemas = {
  models_dir: schema('Où vivent les modèles',
    box(30, 76, 150, 56, 'Service', 's-blue') + arrow(180, 104, 300, 104, 'lit') +
    box(300, 60, 260, 88, 'Dossier des modèles', 's-gold', 'vide : dans les données') +
    text(300, 180, 'un disque externe convient, s’il reste branché', 's-f'), 200),
  library_dir: schema('La bibliothèque : une étagère que le service consulte',
    box(20, 70, 170, 64, 'Bibliothèque', 's-gold', 'vos fichiers .gguf') + arrow(190, 102, 330, 102, 'clone') +
    box(330, 70, 150, 64, 'Gulliver', 's-blue', 'modèles installés') +
    box(510, 70, 70, 64, 'HF', 's-soft') + arrow(510, 102, 480, 102) +
    text(300, 172, 'lue sans réseau, jamais modifiée ; « Rafraîchir depuis le Mac » en donne la liste', 's-f') +
    text(300, 192, 'une copie par clone APFS : aucune place prise sur le même disque', 's-f'), 210),
  reserve_gb: schema('La réserve : un placard à côté de l’atelier',
    box(20, 70, 150, 64, 'Modèles', 's-blue', 'installés') + arrow(170, 92, 300, 92, 'clone') +
    box(300, 60, 170, 84, 'Réserve', 's-gold', 'gardés par empreinte') + arrow(300, 124, 170, 124, 'restaure') +
    box(500, 70, 80, 64, 'HF', 's-soft') + arrow(500, 102, 470, 102) +
    text(300, 178, 'clone d’un modèle installé : aucune place ; plafond : les copies des modèles supprimés', 's-f') +
    text(300, 198, 'au-delà du plafond, les plus anciennes partent d’abord', 's-f'), 215)
};
