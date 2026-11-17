// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "store.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>

#include <dlib/sqlite.h>

#include "../common/log.h"
#include "../common/platform.h"

namespace gulliver
{

    namespace
    {
        // Les colonnes d'un modèle, dans l'ordre où model_of() les lit.
        const char* const model_columns = "id, name, file, size, sha256, source, installed_at, verified_at, "
            "descriptor, projector, projector_size, projector_sha256";

        installed_model model_of(dlib::statement& st)
        {
            installed_model m;
            m.id = st.get_column_as_text(0);
            m.name = st.get_column_as_text(1);
            m.file = st.get_column_as_text(2);
            m.size = static_cast<uint64_t>(st.get_column_as_int64(3));
            m.sha256 = st.get_column_as_text(4);
            m.source = st.get_column_as_text(5);
            m.installed_at = st.get_column_as_int64(6);
            m.verified_at = st.get_column_as_int64(7);
            m.descriptor = json::parse_or_null(st.get_column_as_text(8));
            m.projector = st.get_column_as_text(9);
            m.projector_size = static_cast<uint64_t>(st.get_column_as_int64(10));
            m.projector_sha256 = st.get_column_as_text(11);
            return m;
        }
    }

    store::store(const std::string& path) : path_(path)
    {
        make_dirs(parent_path(path));
        connect();
        /*
            Chaque page de la base est vérifiée une fois au démarrage : une base abîmée au-delà
            de son en-tête échoue ici, avec "malformed" dans son erreur, là où le service la met
            de côté et en commence une nouvelle, plutôt que dans les requêtes qui suivent.
        */
        {
            dlib::statement st(*db_, "PRAGMA quick_check");
            st.exec();
            const std::string result = st.move_next() ? st.get_column_as_text(0) : std::string("no answer");
            if (result != "ok") throw std::runtime_error("database disk image is malformed (" + result + ")");
        }
        db_->exec(
            "CREATE TABLE IF NOT EXISTS models ("
            " id TEXT PRIMARY KEY, name TEXT NOT NULL, file TEXT NOT NULL, size INTEGER NOT NULL,"
            " sha256 TEXT NOT NULL, source TEXT NOT NULL, installed_at INTEGER NOT NULL,"
            " verified_at INTEGER NOT NULL DEFAULT 0, descriptor TEXT NOT NULL DEFAULT '{}',"
            " projector TEXT NOT NULL DEFAULT '', projector_size INTEGER NOT NULL DEFAULT 0,"
            " projector_sha256 TEXT NOT NULL DEFAULT '')");
        // Les colonnes des projecteurs, qui manquent aux bases des versions antérieures.
        std::vector<std::string> columns;
        {
            dlib::statement st(*db_, "PRAGMA table_info(models)");
            st.exec();
            while (st.move_next()) columns.push_back(st.get_column_as_text(1));
        }
        const char* const added[][2] = { { "projector", "TEXT NOT NULL DEFAULT ''" },
            { "projector_size", "INTEGER NOT NULL DEFAULT 0" }, { "projector_sha256", "TEXT NOT NULL DEFAULT ''" } };
        for (const auto& c : added)
            if (std::find(columns.begin(), columns.end(), c[0]) == columns.end())
            {
                db_->exec(std::string("ALTER TABLE models ADD COLUMN ") + c[0] + " " + c[1]);
                log_info(std::string("store: the column ") + c[0] + " is added to the models of " + path_);
            }
        db_->exec(
            "CREATE TABLE IF NOT EXISTS projectors ("
            " sha256 TEXT PRIMARY KEY, file TEXT NOT NULL, size INTEGER NOT NULL, name TEXT NOT NULL,"
            " descriptor TEXT NOT NULL DEFAULT '{}', received_at INTEGER NOT NULL)");
        db_->exec(
            "CREATE TABLE IF NOT EXISTS downloads ("
            " id TEXT PRIMARY KEY, state TEXT NOT NULL, updated_at INTEGER NOT NULL)");
        db_->exec(
            "CREATE TABLE IF NOT EXISTS usage ("
            " day TEXT PRIMARY KEY, prompt_tokens INTEGER NOT NULL DEFAULT 0,"
            " generated_tokens INTEGER NOT NULL DEFAULT 0, requests INTEGER NOT NULL DEFAULT 0)");
        db_->exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        // Les profils : l'espace de chaque utilisateur.  Le nom est unique ; les données d'un profil
        // (tables profile_*) s'y rattachent par une clé étrangère ON DELETE CASCADE, active grâce au
        // PRAGMA foreign_keys=ON de connect().
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profiles ("
            " id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
            " created INTEGER NOT NULL, last_used INTEGER NOT NULL DEFAULT 0)");
        // Les données d'un profil.  Chaque table porte la colonne profile en tête de clé et une clé
        // étrangère ON DELETE CASCADE : supprimer un profil emporte toutes ses données.  Le document
        // (doc) est le JSON entier que la page gère déjà (une discussion avec ses messages, un projet
        // avec ses fichiers) ; `updated` sert à la révision optimiste et au tri.
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_conversations ("
            " profile TEXT NOT NULL, id TEXT NOT NULL, project TEXT NOT NULL DEFAULT '',"
            " updated INTEGER NOT NULL DEFAULT 0, doc TEXT NOT NULL,"
            " PRIMARY KEY (profile, id),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        db_->exec("CREATE INDEX IF NOT EXISTS ix_conv_project ON profile_conversations(profile, project)");
        db_->exec("CREATE INDEX IF NOT EXISTS ix_conv_updated ON profile_conversations(profile, updated)");
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_projects ("
            " profile TEXT NOT NULL, id TEXT NOT NULL, name TEXT NOT NULL DEFAULT '', doc TEXT NOT NULL,"
            " PRIMARY KEY (profile, id),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        // Les réglages de la page, par profil, en clé/valeur (thème, nom, prompt système, mémoire…).
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_page_settings ("
            " profile TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
            " PRIMARY KEY (profile, key),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        // Les passages indexés (RAG) : le texte et, quand il est calculé, le vecteur d'embedding en
        // BLOB.  where_, start_, end_ : les mots réservés de SQL prennent un souligné.
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_chunks ("
            " profile TEXT NOT NULL, id TEXT NOT NULL, owner TEXT NOT NULL, doc TEXT NOT NULL,"
            " ordinal INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL DEFAULT '',"
            " where_ TEXT NOT NULL DEFAULT '', start_ INTEGER NOT NULL DEFAULT 0, end_ INTEGER NOT NULL DEFAULT 0,"
            " text TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '', vector BLOB,"
            " PRIMARY KEY (profile, id),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        db_->exec("CREATE INDEX IF NOT EXISTS ix_chunk_owner ON profile_chunks(profile, owner)");
        db_->exec("CREATE INDEX IF NOT EXISTS ix_chunk_doc ON profile_chunks(profile, doc)");
    }

    store::~store() {}

    void store::connect()
    {
        db_.reset(new dlib::database(path_));
        /*
            Une base qu'un autre programme tient un moment, une sauvegarde ou une copie du
            fichier par exemple, est attendue jusqu'à cinq secondes avant qu'une opération
            échoue, et les opérations suivantes la retrouvent libre.
        */
        db_->exec("PRAGMA busy_timeout=5000");
        db_->exec("PRAGMA journal_mode=WAL");
        db_->exec("PRAGMA synchronous=NORMAL");
        db_->exec("PRAGMA foreign_keys=ON");
    }

    void store::writing(const std::function<void()>& work)
    {
        try
        {
            work();
        }
        catch (const std::exception& e)
        {
            // Une connexion que SQLite a ouverte en lecture seule, une fois que le fichier peut de
            // nouveau être écrit.
            if (std::string(e.what()).find("readonly") == std::string::npos) throw;
            connect();
            work();
            log_info("store: " + path_ + " is written again");
        }
    }

    std::vector<installed_model> store::models()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<installed_model> out;
        dlib::statement st(*db_, std::string("SELECT ") + model_columns + " FROM models ORDER BY name");
        st.exec();
        while (st.move_next()) out.push_back(model_of(st));
        return out;
    }

    bool store::find_model(const std::string& id, installed_model& m)
    {
        // La ligne du seul modèle, par sa clé, plutôt que tous les modèles avec leur description.
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, std::string("SELECT ") + model_columns + " FROM models WHERE id = ?");
        st.bind(1, id);
        st.exec();
        if (!st.move_next()) return false;
        m = model_of(st);
        return true;
    }

    void store::put_model(const installed_model& m)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO models (id, name, file, size, sha256, source, "
                "installed_at, verified_at, descriptor, projector, projector_size, projector_sha256) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            st.bind(1, m.id);
            st.bind(2, m.name);
            st.bind(3, m.file);
            st.bind_int64(4, static_cast<dlib::int64>(m.size));
            st.bind(5, m.sha256);
            st.bind(6, m.source);
            st.bind_int64(7, m.installed_at);
            st.bind_int64(8, m.verified_at);
            st.bind(9, m.descriptor.dump());
            st.bind(10, m.projector);
            st.bind_int64(11, static_cast<dlib::int64>(m.projector_size));
            st.bind(12, m.projector_sha256);
            st.exec();
        });
    }

    std::vector<pending_projector> store::projectors()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<pending_projector> out;
        dlib::statement st(*db_, "SELECT sha256, file, size, name, descriptor, received_at FROM projectors "
            "ORDER BY received_at");
        st.exec();
        while (st.move_next())
        {
            pending_projector p;
            p.sha256 = st.get_column_as_text(0);
            p.file = st.get_column_as_text(1);
            p.size = static_cast<uint64_t>(st.get_column_as_int64(2));
            p.name = st.get_column_as_text(3);
            p.descriptor = json::parse_or_null(st.get_column_as_text(4));
            p.received_at = st.get_column_as_int64(5);
            out.push_back(p);
        }
        return out;
    }

    void store::put_projector(const pending_projector& p)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO projectors (sha256, file, size, name, descriptor, "
                "received_at) VALUES (?, ?, ?, ?, ?, ?)");
            st.bind(1, p.sha256);
            st.bind(2, p.file);
            st.bind_int64(3, static_cast<dlib::int64>(p.size));
            st.bind(4, p.name);
            st.bind(5, p.descriptor.dump());
            st.bind_int64(6, p.received_at);
            st.exec();
        });
    }

    void store::remove_projector(const std::string& sha256)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM projectors WHERE sha256 = ?");
            st.bind(1, sha256);
            st.exec();
        });
    }

    void store::remove_model(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM models WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    void store::set_verified(const std::string& id, int64_t when)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "UPDATE models SET verified_at = ? WHERE id = ?");
            st.bind_int64(1, when);
            st.bind(2, id);
            st.exec();
        });
    }

    json store::download_state(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, "SELECT state FROM downloads WHERE id = ?");
        st.bind(1, id);
        st.exec();
        if (st.move_next()) return json::parse_or_null(st.get_column_as_text(0));
        return json();
    }

    void store::set_download_state(const std::string& id, const json& state)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO downloads (id, state, updated_at) VALUES (?, ?, ?)");
            st.bind(1, id);
            st.bind(2, state.dump());
            st.bind_int64(3, now_unix());
            st.exec();
        });
    }

    void store::remove_download_state(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM downloads WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    std::vector<std::string> store::pending_downloads()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> out;
        dlib::statement st(*db_, "SELECT id FROM downloads ORDER BY updated_at");
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    void store::add_usage(int64_t prompt_tokens, int64_t generated_tokens, int64_t requests)
    {
        const std::string day = iso_time(now_unix()).substr(0, 10);
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_,
                "INSERT INTO usage (day, prompt_tokens, generated_tokens, requests) VALUES (?, ?, ?, ?) "
                "ON CONFLICT(day) DO UPDATE SET prompt_tokens = prompt_tokens + excluded.prompt_tokens, "
                "generated_tokens = generated_tokens + excluded.generated_tokens, "
                "requests = requests + excluded.requests");
            st.bind(1, day);
            st.bind_int64(2, prompt_tokens);
            st.bind_int64(3, generated_tokens);
            st.bind_int64(4, requests);
            st.exec();
        });
    }

    json store::usage(int days)
    {
        std::lock_guard<std::mutex> lock(m_);
        json out = json::array();
        dlib::statement st(*db_,
            "SELECT day, prompt_tokens, generated_tokens, requests FROM usage ORDER BY day DESC LIMIT ?");
        st.bind(1, days);
        st.exec();
        while (st.move_next())
        {
            json d = json::object();
            d["day"] = st.get_column_as_text(0);
            d["prompt_tokens"] = static_cast<long long>(st.get_column_as_int64(1));
            d["generated_tokens"] = static_cast<long long>(st.get_column_as_int64(2));
            d["requests"] = static_cast<long long>(st.get_column_as_int64(3));
            out.push_back(d);
        }
        return out;
    }

    std::string store::value(const std::string& key, const std::string& def)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, "SELECT value FROM settings WHERE key = ?");
        st.bind(1, key);
        st.exec();
        if (st.move_next()) return st.get_column_as_text(0);
        return def;
    }

    void store::set_value(const std::string& key, const std::string& v)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
            st.bind(1, key);
            st.bind(2, v);
            st.exec();
        });
    }

    // ----------------------------------------------------------------------------------------
    //  Profils

    namespace
    {
        // Une ligne de la table profiles lue dans l'ordre des colonnes de profile_columns.
        profile profile_of(dlib::statement& st)
        {
            profile p;
            p.id = st.get_column_as_text(0);
            p.name = st.get_column_as_text(1);
            p.created = st.get_column_as_int64(2);
            p.last_used = st.get_column_as_int64(3);
            return p;
        }
        const char* const profile_columns = "id, name, created, last_used";
    }

    std::vector<profile> store::profiles()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<profile> out;
        dlib::statement st(*db_, std::string("SELECT ") + profile_columns +
            " FROM profiles ORDER BY last_used DESC, name");
        st.exec();
        while (st.move_next()) out.push_back(profile_of(st));
        return out;
    }

    bool store::find_profile(const std::string& id, profile& p)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, std::string("SELECT ") + profile_columns + " FROM profiles WHERE id = ?");
        st.bind(1, id);
        st.exec();
        if (!st.move_next()) return false;
        p = profile_of(st);
        return true;
    }

    bool store::find_profile_by_name(const std::string& name, profile& p)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, std::string("SELECT ") + profile_columns + " FROM profiles WHERE name = ?");
        st.bind(1, name);
        st.exec();
        if (!st.move_next()) return false;
        p = profile_of(st);
        return true;
    }

    void store::put_profile(const profile& p)
    {
        // Sur un id déjà présent (renommage ou mise à jour de l'usage), les champs sont mis à jour.
        // Un conflit sur le nom unique, lui, laisse l'erreur remonter : on ne remplace jamais le profil
        // d'un autre, ce qui emporterait ses données.  (INSERT OR REPLACE aurait supprimé la ligne en
        // conflit de nom, donc effacé ce profil-là ; on ne l'utilise pas pour cette raison.)
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT INTO profiles (id, name, created, last_used) VALUES (?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET name = excluded.name, last_used = excluded.last_used");
            st.bind(1, p.id);
            st.bind(2, p.name);
            st.bind_int64(3, p.created);
            st.bind_int64(4, p.last_used);
            st.exec();
        });
    }

    void store::remove_profile(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM profiles WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    void store::touch_profile(const std::string& id, int64_t when)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "UPDATE profiles SET last_used = ? WHERE id = ?");
            st.bind_int64(1, when);
            st.bind(2, id);
            st.exec();
        });
    }

    // ----------------------------------------------------------------------------------------
    //  Discussions et projets d'un profil

    std::vector<std::string> store::conversation_docs(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> out;
        dlib::statement st(*db_, "SELECT doc FROM profile_conversations WHERE profile = ? ORDER BY updated DESC");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    bool store::get_conversation(const std::string& profile, const std::string& id, std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, "SELECT doc FROM profile_conversations WHERE profile = ? AND id = ?");
        st.bind(1, profile);
        st.bind(2, id);
        st.exec();
        if (!st.move_next()) return false;
        doc = st.get_column_as_text(0);
        return true;
    }

    bool store::put_conversation(const std::string& profile, const std::string& id, const std::string& project,
                                 int64_t updated, const std::string& doc, int64_t expected_updated)
    {
        std::lock_guard<std::mutex> lock(m_);
        bool ok = true;
        writing([&]() {
            if (expected_updated >= 0)
            {
                // La révision optimiste : la valeur en base doit être celle qu'on croyait avoir lue,
                // ou la ligne doit être absente et expected_updated == 0.
                dlib::statement cur(*db_, "SELECT updated FROM profile_conversations WHERE profile = ? AND id = ?");
                cur.bind(1, profile);
                cur.bind(2, id);
                cur.exec();
                if (cur.move_next())
                {
                    if (cur.get_column_as_int64(0) != expected_updated) { ok = false; return; }
                }
                else if (expected_updated != 0) { ok = false; return; }
            }
            dlib::statement st(*db_, "INSERT OR REPLACE INTO profile_conversations (profile, id, project, updated, doc) "
                "VALUES (?, ?, ?, ?, ?)");
            st.bind(1, profile);
            st.bind(2, id);
            st.bind(3, project);
            st.bind_int64(4, updated);
            st.bind(5, doc);
            st.exec();
        });
        return ok;
    }

    void store::delete_conversation(const std::string& profile, const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM profile_conversations WHERE profile = ? AND id = ?");
            st.bind(1, profile);
            st.bind(2, id);
            st.exec();
        });
    }

    std::vector<std::string> store::project_docs(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> out;
        dlib::statement st(*db_, "SELECT doc FROM profile_projects WHERE profile = ? ORDER BY name");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    bool store::get_project(const std::string& profile, const std::string& id, std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        dlib::statement st(*db_, "SELECT doc FROM profile_projects WHERE profile = ? AND id = ?");
        st.bind(1, profile);
        st.bind(2, id);
        st.exec();
        if (!st.move_next()) return false;
        doc = st.get_column_as_text(0);
        return true;
    }

    void store::put_project(const std::string& profile, const std::string& id, const std::string& name,
                            const std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO profile_projects (profile, id, name, doc) "
                "VALUES (?, ?, ?, ?)");
            st.bind(1, profile);
            st.bind(2, id);
            st.bind(3, name);
            st.bind(4, doc);
            st.exec();
        });
    }

    void store::delete_project(const std::string& profile, const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            // Les discussions du projet sont détachées (project vide), puis le projet est supprimé.
            dlib::statement detach(*db_, "UPDATE profile_conversations SET project = '' WHERE profile = ? AND project = ?");
            detach.bind(1, profile);
            detach.bind(2, id);
            detach.exec();
            dlib::statement st(*db_, "DELETE FROM profile_projects WHERE profile = ? AND id = ?");
            st.bind(1, profile);
            st.bind(2, id);
            st.exec();
        });
    }

    json store::page_settings(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        json out = json::object();
        dlib::statement st(*db_, "SELECT key, value FROM profile_page_settings WHERE profile = ?");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out[st.get_column_as_text(0)] = st.get_column_as_text(1);
        return out;
    }

    void store::set_page_setting(const std::string& profile, const std::string& key, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "INSERT OR REPLACE INTO profile_page_settings (profile, key, value) "
                "VALUES (?, ?, ?)");
            st.bind(1, profile);
            st.bind(2, key);
            st.bind(3, value);
            st.exec();
        });
    }

    // ----------------------------------------------------------------------------------------
    //  Passages indexés (RAG)

    namespace
    {
        const char* const chunk_columns = "id, owner, doc, ordinal, name, where_, start_, end_, text, model, vector";

        chunk_record chunk_of(dlib::statement& st)
        {
            chunk_record c;
            c.id = st.get_column_as_text(0);
            c.owner = st.get_column_as_text(1);
            c.doc = st.get_column_as_text(2);
            c.ordinal = st.get_column_as_int64(3);
            c.name = st.get_column_as_text(4);
            c.where = st.get_column_as_text(5);
            c.start = st.get_column_as_int64(6);
            c.end = st.get_column_as_int64(7);
            c.text = st.get_column_as_text(8);
            c.model = st.get_column_as_text(9);
            c.vector = st.get_column_as_blob(10);
            return c;
        }
    }

    void store::replace_chunks(const std::string& profile, const std::string& doc,
                               const std::vector<chunk_record>& records)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::transaction t(*db_);
            {
                dlib::statement del(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND doc = ?");
                del.bind(1, profile);
                del.bind(2, doc);
                del.exec();
            }
            for (const chunk_record& c : records)
            {
                dlib::statement st(*db_, "INSERT INTO profile_chunks "
                    "(profile, id, owner, doc, ordinal, name, where_, start_, end_, text, model, vector) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
                st.bind(1, profile);
                st.bind(2, c.id);
                st.bind(3, c.owner);
                st.bind(4, c.doc);
                st.bind_int64(5, c.ordinal);
                st.bind(6, c.name);
                st.bind(7, c.where);
                st.bind_int64(8, c.start);
                st.bind_int64(9, c.end);
                st.bind(10, c.text);
                st.bind(11, c.model);
                st.bind_blob(12, c.vector);
                st.exec();
            }
            t.commit();
        });
    }

    std::vector<chunk_record> store::chunks_of(const std::string& profile, const std::string& owner)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_record> out;
        dlib::statement st(*db_, std::string("SELECT ") + chunk_columns +
            " FROM profile_chunks WHERE profile = ? AND owner = ? ORDER BY doc, ordinal");
        st.bind(1, profile);
        st.bind(2, owner);
        st.exec();
        while (st.move_next()) out.push_back(chunk_of(st));
        return out;
    }

    std::vector<chunk_record> store::chunks_of_document(const std::string& profile, const std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_record> out;
        dlib::statement st(*db_, std::string("SELECT ") + chunk_columns +
            " FROM profile_chunks WHERE profile = ? AND doc = ? ORDER BY ordinal");
        st.bind(1, profile);
        st.bind(2, doc);
        st.exec();
        while (st.move_next()) out.push_back(chunk_of(st));
        return out;
    }

    std::vector<chunk_status_row> store::chunk_status(const std::string& profile, const std::string& owner,
                                                      const std::string& model)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_status_row> out;
        // Par document : le nombre de passages, ceux dont le vecteur est du modèle demandé, et un
        // modèle représentatif.  Le comptage se fait en SQL, sans transférer les vecteurs.
        dlib::statement st(*db_,
            "SELECT doc, COUNT(*), SUM(CASE WHEN vector IS NOT NULL AND model = ? THEN 1 ELSE 0 END), MAX(model) "
            "FROM profile_chunks WHERE profile = ? AND owner = ? GROUP BY doc");
        st.bind(1, model);
        st.bind(2, profile);
        st.bind(3, owner);
        st.exec();
        while (st.move_next())
        {
            chunk_status_row r;
            r.doc = st.get_column_as_text(0);
            r.passages = st.get_column_as_int64(1);
            r.vectors = st.get_column_as_int64(2);
            r.model = st.get_column_as_text(3);
            out.push_back(r);
        }
        return out;
    }

    std::vector<std::pair<std::string, std::string>> store::chunk_documents(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::pair<std::string, std::string>> out;
        dlib::statement st(*db_, "SELECT DISTINCT owner, doc FROM profile_chunks WHERE profile = ?");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.emplace_back(st.get_column_as_text(0), st.get_column_as_text(1));
        return out;
    }

    void store::delete_chunks_of_owner(const std::string& profile, const std::string& owner)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND owner = ?");
            st.bind(1, profile);
            st.bind(2, owner);
            st.exec();
        });
    }

    void store::delete_chunks_of_document(const std::string& profile, const std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            dlib::statement st(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND doc = ?");
            st.bind(1, profile);
            st.bind(2, doc);
            st.exec();
        });
    }

}
