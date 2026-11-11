// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Tests des clés, du coffre, de la base de données et des réglages du service, dans un dossier
// temporaire.

#include <iostream>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "../common/imposed_terms.h"
#include "../common/platform.h"
#include "../host/keys.h"
#include "../host/settings.h"
#include "../host/store.h"
#include "../host/vault.h"

using namespace pseudalis;

namespace
{
    int failures = 0;

#define CHECK(cond) check((cond), #cond, __LINE__)
    void check(bool ok, const char* what, int line)
    {
        if (ok) return;
        ++failures;
        std::cerr << "test_host.cpp:" << line << ": failed: " << what << std::endl;
    }
}

int main()
{
    const std::string dir = join_path(join_path(executable_dir(), "test-host-data"), random_hex(8));
    make_dirs(dir);

    // La clé maîtresse : créée une fois, relue, et son export.
    std::string master, again, error;
    bool created = false;
    CHECK(load_master_key(join_path(dir, "keys"), master, created, error) && created && master.size() == 32);
    CHECK(load_master_key(join_path(dir, "keys"), again, created, error) && !created && again == master);
    const std::string exported = export_master_key(master, "a passphrase of the tests");
    std::string imported;
    CHECK(!import_master_key(exported, "another passphrase", imported, error));
    CHECK(import_master_key(exported, "a passphrase of the tests", imported, error) && imported == master);
    const key_set keys = derive_keys(master);
    CHECK(keys.pseudonymization.size() == 32 && keys.vault_encryption != keys.vault_authentication);
    CHECK(aes256_ctr(keys.vault_encryption, std::string(16, '\1'),
                     aes256_ctr(keys.vault_encryption, std::string(16, '\1'), "round trip")) == "round trip");

    // Le coffre : des blobs relus, ceux qui sont altérés refusés.
    vault v(join_path(dir, "vault"), keys.vault_encryption, keys.vault_authentication);
    const std::string data(100000, 'x');
    const std::string id = v.put(data);
    std::string back;
    CHECK(vault::valid_id(id) && v.get(id, back) && back == data);
    std::string file;
    const std::string path = join_path(join_path(join_path(dir, "vault"), id.substr(0, 2)), id + ".bin");
    CHECK(read_file(path, file) && file.find(std::string(100, 'x')) == std::string::npos);
    file[40] ^= 1;
    CHECK(write_file_atomic(path, file) && !v.get(id, back));
    vault other(join_path(dir, "vault"), derive_keys(std::string(32, '\7')).vault_encryption,
                derive_keys(std::string(32, '\7')).vault_authentication);
    const std::string id2 = v.put("second");
    CHECK(!other.get(id2, back) && v.get(id2, back) && back == "second");
    CHECK(v.remove(id2) && !v.exists(id2));
    const std::string empty_id = v.put(std::string());
    CHECK(v.get(empty_id, back) && back.empty());
    // La liste du coffre, qui retire les écritures interrompues.
    const std::string leftover =
        join_path(join_path(join_path(dir, "vault"), id.substr(0, 2)), id + ".bin.tmp-0a1b2c3d");
    CHECK(write_file_atomic(leftover, "cut short"));
    {
        const std::vector<std::string> ids = v.list_and_clean();
        bool has_id = false, has_empty = false;
        for (const std::string& x : ids)
        {
            has_id = has_id || x == id;
            has_empty = has_empty || x == empty_id;
        }
        CHECK(has_id && has_empty && ids.size() == 2 && !file_exists(leftover));
    }
    CHECK(v.remove(empty_id));

    /*
        Les réglages : une valeur hors de sa plage ne fait perdre aucun original (jours sans limite,
        le plus grand espace), l'espace contient le plus gros fichier, et un fichier d'une version
        antérieure à 1.0.2, sans espace, garde tous les originaux, ses 30 jours par défaut devenant 45.
    */
    {
        CHECK(host_settings().removed_days == 45 && host_settings().history_mb == 10240);
        json j = json::object();
        j["removed_days"] = -1;
        j["history_mb"] = 0;
        j["max_file_mb"] = 64;
        j["drive_letter"] = "c";
        host_settings s = host_settings::from_json(j);
        CHECK(s.removed_days == 0 && s.history_mb == host_settings::max_history_mb && s.max_file_mb == 64 &&
              s.drive_letter == "A");
        j["removed_days"] = 0;
        j["history_mb"] = 100;
        s = host_settings::from_json(j);
        CHECK(s.removed_days == 0 && s.history_mb == 100);
        CHECK(host_settings::from_json(s.to_json()).removed_days == 0 &&
              host_settings::from_json(s.to_json()).history_mb == 100);
        j["history_mb"] = 2;
        CHECK(host_settings::from_json(j).history_mb == 64);
        json earlier = json::object();
        earlier["removed_days"] = 30;
        earlier["max_file_mb"] = 150;
        CHECK(host_settings::from_json(earlier).removed_days == 45 &&
              host_settings::from_json(earlier).history_mb == host_settings::max_history_mb);
        earlier["removed_days"] = 60;
        CHECK(host_settings::from_json(earlier).removed_days == 60);
        j["history_mb"] = 100;
        j["removed_days"] = 90;
        j["drive_letter"] = "p";
        s = host_settings::from_json(j);
        CHECK(s.removed_days == 90 && s.drive_letter == "P");
        CHECK(host_settings::from_json(s.to_json()).removed_days == 90);
        // Les étiquettes par défaut : écrites sauf si le réglage dit le contraire, un fichier d'une version
        // antérieure les gardant.
        CHECK(host_settings().tags && host_settings::from_json(earlier).tags);
        s.tags = false;
        CHECK(!host_settings::from_json(s.to_json()).tags);
    }
    // Un settings.json endommagé garde les originaux sans limite de temps, dans le plus grand espace.
    {
        const std::string path = join_path(dir, "settings.json");
        CHECK(write_file_atomic(path, "{ not json"));
        settings_store damaged(path);
        CHECK(damaged.get().removed_days == 0 && damaged.get().history_mb == host_settings::max_history_mb &&
              file_exists(path + ".invalid"));
        host_settings changed;
        CHECK(damaged.update([](host_settings& s) { s.removed_days = 45; }, changed) && changed.removed_days == 45);
        settings_store again(path);
        CHECK(again.get().removed_days == 45 && again.get().tags);
        // Le réglage des étiquettes, enregistré, est relu quand le service redémarre.
        CHECK(again.update([](host_settings& s) { s.tags = false; }, changed) && !changed.tags);
        settings_store restarted(path);
        CHECK(!restarted.get().tags && restarted.get().removed_days == 45);
    }

    // Les termes imposés : une liste que le service accepte, ou la raison et l'endroit de son refus.
    {
        std::vector<imposed_term> terms;
        std::string message, code;
        long long index = -1;
        json list = json::parse_or_null("[{\"term\": \"  Projet Alpha \", \"type\": \"ORG\"}, "
                                        "{\"term\": \"ACME\", \"type\": \"ORG\", \"case_sensitive\": true}, "
                                        "{\"term\": \"acme\", \"type\": \"PERSON\"}]");
        CHECK(parse_imposed_terms(list, terms, message, code, index) && terms.size() == 3 &&
              terms[0].term == "Projet Alpha" && !terms[0].case_sensitive && terms[1].case_sensitive &&
              terms[2].type == "PERSON" && index == -1);
        const json canonical = imposed_terms_json(terms);
        std::vector<imposed_term> back;
        CHECK(parse_imposed_terms(canonical, back, message, code, index) && back.size() == 3 &&
              back[1].term == "ACME");
        CHECK(parse_imposed_terms(json::parse_or_null("[]"), terms, message, code, index) && terms.empty());
        auto refused = [&](const char* text, const char* expected, long long at) {
            std::vector<imposed_term> t;
            std::string m, c;
            long long i = -2;
            return !parse_imposed_terms(json::parse_or_null(text), t, m, c, i) && c == expected && i == at &&
                   !m.empty();
        };
        CHECK(refused("{}", "terms_invalid", -1));
        CHECK(refused("[{\"term\": \"   \"}]", "terms_invalid", 0));
        CHECK(refused("[{\"term\": \"Alpha\"}, {\"term\": \"---\"}]", "terms_invalid", 1));
        CHECK(refused("[{\"term\": \"Alpha\\nBeta\"}]", "terms_invalid", 0));
        CHECK(refused("[{\"term\": \"Alpha\", \"type\": \"DATE\"}]", "terms_type", 0));
        CHECK(refused("[{\"term\": \"Société\"}, {\"term\": \"SOCIETE\"}]", "terms_duplicate", 1));
        CHECK(parse_imposed_terms(json::parse_or_null("[{\"term\": \"Société\"}, {\"term\": \"SOCIETE\", "
                                                      "\"case_sensitive\": true}]"), terms, message, code, index));
        std::string many = "[";
        for (int k = 0; k <= static_cast<int>(max_imposed_terms); ++k)
            many += std::string(k ? "," : "") + "{\"term\": \"term " + std::to_string(k) + "\"}";
        CHECK(refused((many + "]").c_str(), "terms_too_many", -1));
        CHECK(refused(("[{\"term\": \"" + std::string(max_imposed_term_length + 1, 'a') + "\"}]").c_str(),
                      "terms_invalid", 0));
        CHECK(parse_imposed_terms(json::parse_or_null("[{\"term\": \"" + std::string(max_imposed_term_length, 'a') +
                                                      "\"}]"), terms, message, code, index));
    }

    // La base de données : les fiches, les images et l'arborescence du lecteur.
    {
        store db(join_path(dir, "pseudalis.db"));
        file_record r;
        r.source = "shell";
        r.original_name = "CV Jean Dupont.docx";
        r.output_name = "CV Jean Dupont.docx";
        r.original_sha256 = std::string(64, 'a');
        r.output_sha256 = std::string(64, 'b');
        r.original_blob = v.put("original");
        r.kind = "docx";
        r.original_size = 1000;
        const long long rec = db.add_file(r);
        CHECK(rec > 0 && db.count_files() == 1 && db.count_files("shell") == 1 && db.count_files("web") == 0);
        CHECK(db.originals_bytes() == 1000 && db.oldest_files(10).size() == 1 && db.oldest_files(10)[0].first == rec);
        file_record found;
        CHECK(db.find_by_output(std::string(64, 'b'), found) && found.id == rec
            && found.original_name == r.original_name);
        db.set_file_location(rec, std::string(32, 'c'), "C:\\docs\\cv.docx");
        CHECK(db.find_by_object(std::string(32, 'c'), found) && found.path == "C:\\docs\\cv.docx");
        // Une image, sous un identifiant calculé avec la clé que son SHA-256 ne donne pas, gardée d'abord
        // sans description.
        picture_record p;
        p.id = "0123456789abcdef";
        p.sha256 = std::string(64, 'e');
        p.media_type = "image/png";
        p.blob = v.put("png");
        db.add_picture(p);
        db.link_picture(rec, p.id);
        picture_record q;
        std::string text, before;
        CHECK(db.get_picture(p.id, q) && q.sha256 == p.sha256 && q.blob == p.blob && q.description.empty());
        // La description reçue plus tard, chiffrée dans le coffre ; le blob nommé auparavant le quitterait.
        p.description = v.put("a chart");
        CHECK(db.set_picture_description(p.id, p.description, before) && before.empty());
        CHECK(db.get_picture(p.id, q) && q.description == p.description && v.get(q.description, text) &&
              text == "a chart");
        CHECK(!db.set_picture_description("fedcba9876543210", p.description, before));
        // Une image qu'aucune fiche ne nomme, la protection qui l'a gardée ayant échoué, s'en va au
        // démarrage.
        picture_record lone = p;
        lone.id = "fedcba9876543210";
        lone.blob = v.put("gif");
        lone.description.clear();
        db.add_picture(lone);
        CHECK(db.remove_unlinked_pictures() == 1 && !db.get_picture(lone.id, q) && db.get_picture(p.id, q));
        CHECK(db.referenced_blobs().count(lone.blob) == 0 && db.remove_unlinked_pictures() == 0);
        // Les entrées d'une archive protégée : trouvées par ce qui a été écrit, celles d'une archive
        // contenue prises depuis l'intérieur de celle-ci.
        file_record archive = r;
        archive.original_name = "Dossier Jean Dupont.zip";
        archive.output_sha256 = std::string(64, '9');
        archive.original_blob = v.put("archive");
        archive.original_size = 2500;
        const long long zip = db.add_file(archive);
        // L'espace des originaux, et le plus ancien d'abord.
        CHECK(db.originals_bytes() == 3500 && db.oldest_files(1).size() == 1 && db.oldest_files(1)[0].first == rec &&
              db.oldest_files(5).size() == 2 && db.oldest_files(5)[1].first == zip &&
              db.oldest_files(5)[1].second == 2500);
        std::vector<entry_record> entries(3);
        entries[0].name = "Dossier X/lettre.txt";
        entries[0].original = "Dossier Jean Dupont/lettre.txt";
        entries[0].sha256 = std::string(64, '1');
        entries[0].kind = "txt";
        entries[1].name = "notes.tar.gz";
        entries[1].original = "notes.tar.gz";
        entries[1].sha256 = std::string(64, '2');
        entries[1].kind = "tar.gz";
        entries[2].name = "notes.tar.gz!Y/note.txt";
        entries[2].original = "notes.tar.gz!Marie Curie/note.txt";
        entries[2].sha256 = std::string(64, '3');
        entries[2].kind = "txt";
        db.add_entries(zip, entries);
        entry_record entry_found;
        CHECK(db.find_entry(std::string(64, '1'), entry_found) && entry_found.record == zip &&
              entry_found.original == "Dossier Jean Dupont/lettre.txt" && entry_found.kind == "txt");
        CHECK(!db.find_entry(std::string(64, '4'), entry_found));
        CHECK(db.find_entry(std::string(64, '2'), entry_found));
        const std::vector<entry_record> inside = db.entries_inside(entry_found);
        CHECK(inside.size() == 1 && inside[0].name == "Y/note.txt" && inside[0].original == "Marie Curie/note.txt" &&
              inside[0].sha256 == std::string(64, '3'));
        // Oubliée avec son archive : ses entrées s'en vont, et sont retenues comme oubliées.
        std::vector<std::string> zip_blobs;
        CHECK(db.remove_file(zip, zip_blobs, "forgotten") && zip_blobs.size() == 1);
        int64_t entry_time = 0;
        std::string entry_reason;
        CHECK(!db.find_entry(std::string(64, '1'), entry_found) && !db.find_entry(std::string(64, '3'), entry_found));
        CHECK(db.find_forgotten(std::string(64, '3'), std::string(), entry_time, entry_reason) &&
              entry_reason == "forgotten");

        // Une fiche dont le fichier a quitté le lecteur garde son original jusqu'à la fin de son délai.
        CHECK(found.removed == 0 && db.files_removed_before(now_unix()).empty());
        db.set_file_removed(rec, now_unix() - 31 * 86400);
        CHECK(db.get_file(rec, found) && found.removed > 0);
        CHECK(db.files_removed_before(now_unix() - 30 * 86400).size() == 1);
        CHECK(db.files_removed_before(now_unix() - 32 * 86400).empty());
        CHECK(db.referenced_blobs().count(r.original_blob) == 1 && db.referenced_blobs().count(p.blob) == 1 &&
              db.referenced_blobs().count(p.description) == 1);
        std::vector<std::string> blobs;
        int64_t when = 0;
        std::string reason;
        CHECK(!db.find_forgotten(std::string(64, 'b'), std::string(32, 'c'), when, reason));
        // Un original oublié : son fichier protégé est retenu, par son hash et son identifiant d'objet.
        CHECK(db.remove_file(rec, blobs, "forgotten") && blobs.size() == 3 && db.count_files() == 0);
        CHECK(db.find_forgotten(std::string(64, 'b'), std::string(), when, reason) && reason == "forgotten");
        CHECK(db.find_forgotten(std::string(64, 'f'), std::string(32, 'c'), when, reason) && when > 0);
        CHECK(!db.find_forgotten(std::string(64, 'f'), std::string(), when, reason));
        CHECK(!db.get_picture(p.id, q));
        // L'identifiant d'une fiche supprimée n'est jamais redonné.
        const long long next = db.add_file(r);
        // Une fiche qui s'en va sans raison (le lecteur remplace son fichier) ne laisse aucune trace.
        CHECK(next > rec && db.remove_file(next, blobs) && db.count_files() == 0);

        // Une base de données de la version 1 du schéma : les descriptions qu'elle gardait en clair sont
        // effacées.
        const std::string old_path = join_path(dir, "version1.db");
        {
            store fresh(old_path);
        }
        const std::string kept = v.put("a map");
        sqlite3* raw = nullptr;
        CHECK(sqlite3_open(old_path.c_str(), &raw) == SQLITE_OK);
        const std::string old_rows =
            "INSERT INTO pictures VALUES('aaaaaaaaaaaaaaaa', '" + std::string(64, 'a') + "', 'image/png', '"
            + v.put("old") + "', 'Portrait of Jean Dupont', 1);"
            "INSERT INTO pictures VALUES('bbbbbbbbbbbbbbbb', '" + std::string(64, 'b') + "', 'image/png', '"
            + v.put("new") + "', '" + kept + "', 1);"
            "PRAGMA user_version = 1;";
        CHECK(sqlite3_exec(raw, old_rows.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
        {
            store migrated(old_path);
            CHECK(migrated.get_picture("aaaaaaaaaaaaaaaa", q) && q.description.empty() && !q.blob.empty());
            CHECK(migrated.get_picture("bbbbbbbbbbbbbbbb", q) && q.description == kept);
        }
        // Une base de données de la version 2, telle que Pseudalis 1.0.4 l'a laissée : ses fichiers
        // gardent leurs étiquettes, et les substituts des fichiers protégés sans étiquettes sont
        // conservés à partir de là.
        const std::string v2_path = join_path(dir, "version2.db");
        {
            store fresh(v2_path);
            CHECK(fresh.add_file(r) > 0);
        }
        CHECK(sqlite3_open(v2_path.c_str(), &raw) == SQLITE_OK);
        CHECK(sqlite3_exec(raw, "ALTER TABLE files DROP COLUMN tags; ALTER TABLE forgotten DROP COLUMN tags;"
                                "DROP TABLE text_substitutes; PRAGMA user_version = 2;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
        {
            store migrated(v2_path);
            file_record tagged;
            CHECK(migrated.find_by_output(r.output_sha256, tagged) && tagged.tags);
            file_record bare = r;
            bare.output_sha256 = std::string(64, 'd');
            bare.tags = false;
            const long long id = migrated.add_file(bare);
            migrated.add_text_substitutes(id, { { "Xoydoeh", "[[PERSON:Xoydoeh#EKR]]" } });
            file_record back;
            CHECK(migrated.find_by_output(bare.output_sha256, back) && !back.tags &&
                  migrated.text_substitutes(id).size() == 1);
        }

        // Les fichiers du dossier du lecteur tels que le service les a laissés, et ce qu'ils pèsent.
        folder_file note;
        note.folder = "drive";
        note.path = "Dossier/Note.txt";
        note.size = 12;
        note.written = 7;
        note.sha256 = std::string(64, 'e');
        note.state = "protected";
        db.set_folder_file(note);
        folder_file folder = note;
        folder.path = "Dossier";
        folder.size = 0;
        folder.state = "folder";
        db.set_folder_file(folder);
        CHECK(db.folder_files("drive").size() == 2 && db.folder_files("other").empty());
        long long files = 0, bytes = 0;
        db.drive_usage(files, bytes);
        CHECK(files == 1 && bytes == 12);
        // Les chemins se comparent sans tenir compte de la casse, comme Windows nomme ses fichiers.
        note.path = "DOSSIER/note.TXT";
        note.size = 13;
        db.set_folder_file(note);
        db.drive_usage(files, bytes);
        CHECK(db.folder_files("drive").size() == 2 && files == 1 && bytes == 13);
        db.remove_folder_file("drive", "dossier/NOTE.txt");
        db.drive_usage(files, bytes);
        CHECK(files == 0 && bytes == 0 && db.folder_files("drive").size() == 1);
        // Un substitut revient par les étiquettes enregistrées pour lui, jamais par sa valeur.
        db.add_substitutes({ { "Xoydoeh Nuggwdfsorn", "[[PERSON:Xoydoeh Nuggwdfsorn#EKR]]" } });
        CHECK(db.tags_for("Xoydoeh Nuggwdfsorn").size() == 1 && db.tags_for("Jean Dupont").empty());
        db.add_event("protected", "\\Dossier\\Note.txt", "3 values");
        CHECK(db.events(0).size() == 1);
        db.set_value("k", "v");
        CHECK(db.value("k") == "v");
    }
    remove_tree(dir);
    if (failures) std::cerr << failures << " failure(s)" << std::endl;
    else std::cout << "test_host: all tests passed" << std::endl;
    return failures ? 1 : 0;
}
