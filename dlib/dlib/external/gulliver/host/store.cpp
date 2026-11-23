// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "store.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>

#include "sqlite_db.h"

#include "../common/log.h"
#include "../common/platform.h"

namespace gulliver
{

    namespace
    {
        // Les colonnes d'un modèle, dans l'ordre où model_of() les lit.
        const char* const model_columns = "id, name, file, size, sha256, source, installed_at, verified_at, "
            "descriptor, projector, projector_size, projector_sha256";

        installed_model model_of(gulliver::statement& st)
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
            gulliver::statement st(*db_, "PRAGMA quick_check");
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
            gulliver::statement st(*db_, "PRAGMA table_info(models)");
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
        // Les comptes (fork pour macOS, 2026-11-22) : l'empreinte du mot de passe et le rôle
        // d'administrateur, ajoutés aux bases d'avant ; puis les sessions.
        {
            std::vector<std::string> cols;
            gulliver::statement st(*db_, "PRAGMA table_info(profiles)");
            st.exec();
            while (st.move_next()) cols.push_back(st.get_column_as_text(1));
            const char* const added[][2] = { { "password", "TEXT NOT NULL DEFAULT ''" },
                { "admin", "INTEGER NOT NULL DEFAULT 0" }, { "email", "TEXT NOT NULL DEFAULT ''" } };
            for (const auto& c : added)
                if (std::find(cols.begin(), cols.end(), c[0]) == cols.end())
                {
                    db_->exec(std::string("ALTER TABLE profiles ADD COLUMN ") + c[0] + " " + c[1]);
                    log_info(std::string("store: the column ") + c[0] + " is added to the profiles of " + path_);
                }
        }
        // L'adresse de connexion (2026-11-23) : unique parmi les profils qui en ont une.
        db_->exec("CREATE UNIQUE INDEX IF NOT EXISTS profiles_email ON profiles(email) WHERE email <> ''");
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_sessions ("
            " key TEXT PRIMARY KEY, profile TEXT NOT NULL,"
            " created INTEGER NOT NULL, last_used INTEGER NOT NULL,"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
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
        // Les index nommés d'un profil : chaque combinaison {modèle d'embedding, découpage} est un index
        // à part, nommé, pour garder plusieurs indexations et basculer de l'une à l'autre sans réindexer.
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_indexes ("
            " profile TEXT NOT NULL, id TEXT NOT NULL, name TEXT NOT NULL,"
            " embed_model TEXT NOT NULL DEFAULT '', passage_chars INTEGER NOT NULL DEFAULT 1200,"
            " overlap_chars INTEGER NOT NULL DEFAULT 240, created INTEGER NOT NULL DEFAULT 0,"
            " active INTEGER NOT NULL DEFAULT 0,"
            " PRIMARY KEY (profile, id),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        // La colonne index_id des passages, qui manque aux bases créées avant les index nommés : chaque
        // passage appartient à un index (vide = l'index par défaut, pour les passages d'avant).
        {
            std::vector<std::string> cols;
            gulliver::statement st(*db_, "PRAGMA table_info(profile_chunks)");
            st.exec();
            while (st.move_next()) cols.push_back(st.get_column_as_text(1));
            if (std::find(cols.begin(), cols.end(), "index_id") == cols.end())
                db_->exec("ALTER TABLE profile_chunks ADD COLUMN index_id TEXT NOT NULL DEFAULT ''");
        }
        db_->exec("CREATE INDEX IF NOT EXISTS ix_chunk_index ON profile_chunks(profile, index_id)");
        // Les images d'un profil : les octets (image pleine et vignette) vont dans des fichiers sous
        // <data>/profiles/<id>/images/ ; cette table n'en garde que les métadonnées, pour la liste par
        // propriétaire et le total d'octets.
        db_->exec(
            "CREATE TABLE IF NOT EXISTS profile_images ("
            " profile TEXT NOT NULL, id TEXT NOT NULL, owner TEXT NOT NULL, name TEXT NOT NULL DEFAULT '',"
            " type TEXT NOT NULL DEFAULT '', size INTEGER NOT NULL DEFAULT 0,"
            " width INTEGER NOT NULL DEFAULT 0, height INTEGER NOT NULL DEFAULT 0, added INTEGER NOT NULL DEFAULT 0,"
            " blob_bytes INTEGER NOT NULL DEFAULT 0, thumb_bytes INTEGER NOT NULL DEFAULT 0,"
            " PRIMARY KEY (profile, id),"
            " FOREIGN KEY (profile) REFERENCES profiles(id) ON DELETE CASCADE)");
        db_->exec("CREATE INDEX IF NOT EXISTS ix_image_owner ON profile_images(profile, owner)");
    }

    store::~store() {}

    void store::connect()
    {
        db_.reset(new gulliver::database(path_));
        /*
            Une base qu'un autre programme tient un moment, une sauvegarde ou une copie du
            fichier par exemple, est attendue jusqu'à cinq secondes avant qu'une opération
            échoue, et les opérations suivantes la retrouvent libre.
        */
        db_->exec("PRAGMA busy_timeout=5000");
        db_->exec("PRAGMA journal_mode=WAL");
        db_->exec("PRAGMA synchronous=NORMAL");
        db_->exec("PRAGMA foreign_keys=ON");
        /*
            Deux réglages que la copie de SQLite de Gulliver avait à la compilation, et que le
            SQLite de macOS n'a pas (2026-11-20) : les données effacées sont recouvertes de zéros,
            et les tables temporaires restent en mémoire.
        */
        db_->exec("PRAGMA secure_delete=ON");
        db_->exec("PRAGMA temp_store=MEMORY");
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
        gulliver::statement st(*db_, std::string("SELECT ") + model_columns + " FROM models ORDER BY name");
        st.exec();
        while (st.move_next()) out.push_back(model_of(st));
        return out;
    }

    bool store::find_model(const std::string& id, installed_model& m)
    {
        // La ligne du seul modèle, par sa clé, plutôt que tous les modèles avec leur description.
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + model_columns + " FROM models WHERE id = ?");
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
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO models (id, name, file, size, sha256, source, "
                "installed_at, verified_at, descriptor, projector, projector_size, projector_sha256) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            st.bind(1, m.id);
            st.bind(2, m.name);
            st.bind(3, m.file);
            st.bind_int64(4, static_cast<std::int64_t>(m.size));
            st.bind(5, m.sha256);
            st.bind(6, m.source);
            st.bind_int64(7, m.installed_at);
            st.bind_int64(8, m.verified_at);
            st.bind(9, m.descriptor.dump());
            st.bind(10, m.projector);
            st.bind_int64(11, static_cast<std::int64_t>(m.projector_size));
            st.bind(12, m.projector_sha256);
            st.exec();
        });
    }

    std::vector<pending_projector> store::projectors()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<pending_projector> out;
        gulliver::statement st(*db_, "SELECT sha256, file, size, name, descriptor, received_at FROM projectors "
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
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO projectors (sha256, file, size, name, descriptor, "
                "received_at) VALUES (?, ?, ?, ?, ?, ?)");
            st.bind(1, p.sha256);
            st.bind(2, p.file);
            st.bind_int64(3, static_cast<std::int64_t>(p.size));
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
            gulliver::statement st(*db_, "DELETE FROM projectors WHERE sha256 = ?");
            st.bind(1, sha256);
            st.exec();
        });
    }

    void store::remove_model(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM models WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    void store::set_verified(const std::string& id, int64_t when)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "UPDATE models SET verified_at = ? WHERE id = ?");
            st.bind_int64(1, when);
            st.bind(2, id);
            st.exec();
        });
    }

    json store::download_state(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, "SELECT state FROM downloads WHERE id = ?");
        st.bind(1, id);
        st.exec();
        if (st.move_next()) return json::parse_or_null(st.get_column_as_text(0));
        return json();
    }

    void store::set_download_state(const std::string& id, const json& state)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO downloads (id, state, updated_at) VALUES (?, ?, ?)");
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
            gulliver::statement st(*db_, "DELETE FROM downloads WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    std::vector<std::string> store::pending_downloads()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> out;
        gulliver::statement st(*db_, "SELECT id FROM downloads ORDER BY updated_at");
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    void store::add_usage(int64_t prompt_tokens, int64_t generated_tokens, int64_t requests)
    {
        const std::string day = iso_time(now_unix()).substr(0, 10);
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_,
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
        gulliver::statement st(*db_,
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
        gulliver::statement st(*db_, "SELECT value FROM settings WHERE key = ?");
        st.bind(1, key);
        st.exec();
        if (st.move_next()) return st.get_column_as_text(0);
        return def;
    }

    void store::set_value(const std::string& key, const std::string& v)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
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
        profile profile_of(gulliver::statement& st)
        {
            profile p;
            p.id = st.get_column_as_text(0);
            p.name = st.get_column_as_text(1);
            p.created = st.get_column_as_int64(2);
            p.last_used = st.get_column_as_int64(3);
            p.password = st.get_column_as_text(4);
            p.admin = st.get_column_as_int64(5) != 0;
            p.email = st.get_column_as_text(6);
            return p;
        }
        const char* const profile_columns = "id, name, created, last_used, password, admin, email";
    }

    std::vector<profile> store::profiles()
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<profile> out;
        gulliver::statement st(*db_, std::string("SELECT ") + profile_columns +
            " FROM profiles ORDER BY last_used DESC, name");
        st.exec();
        while (st.move_next()) out.push_back(profile_of(st));
        return out;
    }

    bool store::find_profile(const std::string& id, profile& p)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + profile_columns + " FROM profiles WHERE id = ?");
        st.bind(1, id);
        st.exec();
        if (!st.move_next()) return false;
        p = profile_of(st);
        return true;
    }

    bool store::find_profile_by_name(const std::string& name, profile& p)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + profile_columns + " FROM profiles WHERE name = ?");
        st.bind(1, name);
        st.exec();
        if (!st.move_next()) return false;
        p = profile_of(st);
        return true;
    }

    bool store::find_profile_by_email(const std::string& email, profile& p)
    {
        if (email.empty()) return false;
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + profile_columns + " FROM profiles WHERE email = ?");
        st.bind(1, email);
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
            gulliver::statement st(*db_, "INSERT INTO profiles (id, name, created, last_used, password, admin, email) "
                "VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, "
                "last_used = excluded.last_used, password = excluded.password, admin = excluded.admin, "
                "email = excluded.email");
            st.bind(1, p.id);
            st.bind(2, p.name);
            st.bind_int64(3, p.created);
            st.bind_int64(4, p.last_used);
            st.bind(5, p.password);
            st.bind_int64(6, p.admin ? 1 : 0);
            st.bind(7, p.email);
            st.exec();
        });
    }

    void store::remove_profile(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profiles WHERE id = ?");
            st.bind(1, id);
            st.exec();
        });
    }

    void store::add_session(const std::string& key, const std::string& profile, int64_t now)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT INTO profile_sessions (key, profile, created, last_used) "
                "VALUES (?, ?, ?, ?)");
            st.bind(1, key);
            st.bind(2, profile);
            st.bind_int64(3, now);
            st.bind_int64(4, now);
            st.exec();
        });
    }

    std::string store::session_profile(const std::string& key, int64_t now, int64_t max_idle)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::string profile;
        int64_t last = 0;
        {
            gulliver::statement st(*db_, "SELECT profile, last_used FROM profile_sessions WHERE key = ?");
            st.bind(1, key);
            st.exec();
            if (!st.move_next()) return std::string();
            profile = st.get_column_as_text(0);
            last = st.get_column_as_int64(1);
        }
        const bool expired = now - last > max_idle;
        // L'usage n'est noté qu'une fois par minute au plus, pour ne pas écrire à chaque requête.
        if (expired || now - last >= 60)
            writing([&]() {
                gulliver::statement st(*db_, expired ? "DELETE FROM profile_sessions WHERE key = ?"
                                                     : "UPDATE profile_sessions SET last_used = ? WHERE key = ?");
                if (expired) st.bind(1, key);
                else { st.bind_int64(1, now); st.bind(2, key); }
                st.exec();
            });
        return expired ? std::string() : profile;
    }

    void store::remove_session(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_sessions WHERE key = ?");
            st.bind(1, key);
            st.exec();
        });
    }

    void store::remove_sessions_of(const std::string& profile, const std::string& keep)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_sessions WHERE profile = ? AND key <> ?");
            st.bind(1, profile);
            st.bind(2, keep);
            st.exec();
        });
    }

    void store::touch_profile(const std::string& id, int64_t when)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "UPDATE profiles SET last_used = ? WHERE id = ?");
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
        gulliver::statement st(*db_, "SELECT doc FROM profile_conversations WHERE profile = ? ORDER BY updated DESC");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    bool store::get_conversation(const std::string& profile, const std::string& id, std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, "SELECT doc FROM profile_conversations WHERE profile = ? AND id = ?");
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
                gulliver::statement cur(*db_, "SELECT updated FROM profile_conversations WHERE profile = ? AND id = ?");
                cur.bind(1, profile);
                cur.bind(2, id);
                cur.exec();
                if (cur.move_next())
                {
                    if (cur.get_column_as_int64(0) != expected_updated) { ok = false; return; }
                }
                else if (expected_updated != 0) { ok = false; return; }
            }
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO profile_conversations (profile, id, project, updated, doc) "
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
            gulliver::statement st(*db_, "DELETE FROM profile_conversations WHERE profile = ? AND id = ?");
            st.bind(1, profile);
            st.bind(2, id);
            st.exec();
        });
    }

    std::vector<std::string> store::project_docs(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> out;
        gulliver::statement st(*db_, "SELECT doc FROM profile_projects WHERE profile = ? ORDER BY name");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.push_back(st.get_column_as_text(0));
        return out;
    }

    bool store::get_project(const std::string& profile, const std::string& id, std::string& doc)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, "SELECT doc FROM profile_projects WHERE profile = ? AND id = ?");
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
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO profile_projects (profile, id, name, doc) "
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
            gulliver::statement detach(*db_, "UPDATE profile_conversations SET project = '' WHERE profile = ? AND project = ?");
            detach.bind(1, profile);
            detach.bind(2, id);
            detach.exec();
            gulliver::statement st(*db_, "DELETE FROM profile_projects WHERE profile = ? AND id = ?");
            st.bind(1, profile);
            st.bind(2, id);
            st.exec();
        });
    }

    json store::page_settings(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        json out = json::object();
        gulliver::statement st(*db_, "SELECT key, value FROM profile_page_settings WHERE profile = ?");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out[st.get_column_as_text(0)] = st.get_column_as_text(1);
        return out;
    }

    void store::set_page_setting(const std::string& profile, const std::string& key, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO profile_page_settings (profile, key, value) "
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

        chunk_record chunk_of(gulliver::statement& st)
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
                               const std::vector<chunk_record>& records, const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::transaction t(*db_);
            {
                gulliver::statement del(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND doc = ? AND index_id = ?");
                del.bind(1, profile);
                del.bind(2, doc);
                del.bind(3, index_id);
                del.exec();
            }
            for (const chunk_record& c : records)
            {
                gulliver::statement st(*db_, "INSERT INTO profile_chunks "
                    "(profile, id, owner, doc, ordinal, name, where_, start_, end_, text, model, vector, index_id) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
                st.bind(13, index_id);
                st.exec();
            }
            t.commit();
        });
    }

    std::vector<chunk_record> store::chunks_of(const std::string& profile, const std::string& owner,
                                               const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_record> out;
        gulliver::statement st(*db_, std::string("SELECT ") + chunk_columns +
            " FROM profile_chunks WHERE profile = ? AND owner = ? AND index_id = ? ORDER BY doc, ordinal");
        st.bind(1, profile);
        st.bind(2, owner);
        st.bind(3, index_id);
        st.exec();
        while (st.move_next()) out.push_back(chunk_of(st));
        return out;
    }

    std::vector<chunk_record> store::chunks_of_document(const std::string& profile, const std::string& doc,
                                                        const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_record> out;
        gulliver::statement st(*db_, std::string("SELECT ") + chunk_columns +
            " FROM profile_chunks WHERE profile = ? AND doc = ? AND index_id = ? ORDER BY ordinal");
        st.bind(1, profile);
        st.bind(2, doc);
        st.bind(3, index_id);
        st.exec();
        while (st.move_next()) out.push_back(chunk_of(st));
        return out;
    }

    std::vector<chunk_status_row> store::chunk_status(const std::string& profile, const std::string& owner,
                                                      const std::string& model, const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<chunk_status_row> out;
        // Par document : le nombre de passages, ceux dont le vecteur est du modèle demandé, et un
        // modèle représentatif.  Le comptage se fait en SQL, sans transférer les vecteurs.
        gulliver::statement st(*db_,
            "SELECT doc, COUNT(*), SUM(CASE WHEN vector IS NOT NULL AND model = ? THEN 1 ELSE 0 END), MAX(model) "
            "FROM profile_chunks WHERE profile = ? AND owner = ? AND index_id = ? GROUP BY doc");
        st.bind(1, model);
        st.bind(2, profile);
        st.bind(3, owner);
        st.bind(4, index_id);
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

    std::vector<std::pair<std::string, std::string>> store::chunk_documents(const std::string& profile,
                                                                            const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::pair<std::string, std::string>> out;
        gulliver::statement st(*db_, "SELECT DISTINCT owner, doc FROM profile_chunks WHERE profile = ? AND index_id = ?");
        st.bind(1, profile);
        st.bind(2, index_id);
        st.exec();
        while (st.move_next()) out.emplace_back(st.get_column_as_text(0), st.get_column_as_text(1));
        return out;
    }

    void store::delete_chunks_of_owner(const std::string& profile, const std::string& owner,
                                       const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND owner = ? AND index_id = ?");
            st.bind(1, profile);
            st.bind(2, owner);
            st.bind(3, index_id);
            st.exec();
        });
    }

    void store::delete_chunks_of_document(const std::string& profile, const std::string& doc,
                                          const std::string& index_id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND doc = ? AND index_id = ?");
            st.bind(1, profile);
            st.bind(2, doc);
            st.bind(3, index_id);
            st.exec();
        });
    }

    // ----------------------------------------------------------------------------------------
    //  Index nommés d'un profil

    namespace
    {
        index_record index_of(gulliver::statement& st)
        {
            index_record r;
            r.id = st.get_column_as_text(0);
            r.name = st.get_column_as_text(1);
            r.embed_model = st.get_column_as_text(2);
            r.passage_chars = st.get_column_as_int64(3);
            r.overlap_chars = st.get_column_as_int64(4);
            r.created = st.get_column_as_int64(5);
            r.active = st.get_column_as_int64(6) != 0;
            return r;
        }
        const char* const index_columns = "id, name, embed_model, passage_chars, overlap_chars, created, active";
    }

    std::vector<index_record> store::indexes(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<index_record> out;
        gulliver::statement st(*db_, std::string("SELECT ") + index_columns +
            " FROM profile_indexes WHERE profile = ? ORDER BY created");
        st.bind(1, profile);
        st.exec();
        while (st.move_next()) out.push_back(index_of(st));
        return out;
    }

    bool store::find_index(const std::string& profile, const std::string& id, index_record& out)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + index_columns +
            " FROM profile_indexes WHERE profile = ? AND id = ?");
        st.bind(1, profile);
        st.bind(2, id);
        st.exec();
        if (!st.move_next()) return false;
        out = index_of(st);
        return true;
    }

    std::string store::active_index(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, "SELECT id FROM profile_indexes WHERE profile = ? AND active = 1 LIMIT 1");
        st.bind(1, profile);
        st.exec();
        return st.move_next() ? st.get_column_as_text(0) : std::string();
    }

    void store::put_index(const std::string& profile, const index_record& r)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO profile_indexes "
                "(profile, id, name, embed_model, passage_chars, overlap_chars, created, active) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            st.bind(1, profile);
            st.bind(2, r.id);
            st.bind(3, r.name);
            st.bind(4, r.embed_model);
            st.bind_int64(5, r.passage_chars);
            st.bind_int64(6, r.overlap_chars);
            st.bind_int64(7, r.created);
            st.bind(8, r.active ? 1 : 0);
            st.exec();
        });
    }

    void store::set_active_index(const std::string& profile, const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::transaction t(*db_);
            {
                gulliver::statement off(*db_, "UPDATE profile_indexes SET active = 0 WHERE profile = ?");
                off.bind(1, profile);
                off.exec();
            }
            {
                gulliver::statement on(*db_, "UPDATE profile_indexes SET active = 1 WHERE profile = ? AND id = ?");
                on.bind(1, profile);
                on.bind(2, id);
                on.exec();
            }
            t.commit();
        });
    }

    void store::remove_index(const std::string& profile, const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::transaction t(*db_);
            {
                gulliver::statement chunks(*db_, "DELETE FROM profile_chunks WHERE profile = ? AND index_id = ?");
                chunks.bind(1, profile);
                chunks.bind(2, id);
                chunks.exec();
            }
            {
                gulliver::statement idx(*db_, "DELETE FROM profile_indexes WHERE profile = ? AND id = ?");
                idx.bind(1, profile);
                idx.bind(2, id);
                idx.exec();
            }
            t.commit();
        });
    }

    // ----------------------------------------------------------------------------------------
    //  Métadonnées des images (les octets sont des fichiers à part)

    namespace
    {
        const char* const image_columns = "id, owner, name, type, size, width, height, added, blob_bytes, thumb_bytes";

        image_record image_of(gulliver::statement& st)
        {
            image_record r;
            r.id = st.get_column_as_text(0);
            r.owner = st.get_column_as_text(1);
            r.name = st.get_column_as_text(2);
            r.type = st.get_column_as_text(3);
            r.size = st.get_column_as_int64(4);
            r.width = st.get_column_as_int64(5);
            r.height = st.get_column_as_int64(6);
            r.added = st.get_column_as_int64(7);
            r.blob_bytes = st.get_column_as_int64(8);
            r.thumb_bytes = st.get_column_as_int64(9);
            return r;
        }
    }

    std::vector<image_record> store::images_of(const std::string& profile, const std::string& owner)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<image_record> out;
        gulliver::statement st(*db_, std::string("SELECT ") + image_columns +
            " FROM profile_images WHERE profile = ? AND owner = ? ORDER BY added");
        st.bind(1, profile);
        st.bind(2, owner);
        st.exec();
        while (st.move_next()) out.push_back(image_of(st));
        return out;
    }

    bool store::find_image(const std::string& profile, const std::string& id, image_record& out)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, std::string("SELECT ") + image_columns + " FROM profile_images WHERE profile = ? AND id = ?");
        st.bind(1, profile);
        st.bind(2, id);
        st.exec();
        if (!st.move_next()) return false;
        out = image_of(st);
        return true;
    }

    void store::put_image(const std::string& profile, const image_record& r)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "INSERT OR REPLACE INTO profile_images "
                "(profile, id, owner, name, type, size, width, height, added, blob_bytes, thumb_bytes) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
            st.bind(1, profile);
            st.bind(2, r.id);
            st.bind(3, r.owner);
            st.bind(4, r.name);
            st.bind(5, r.type);
            st.bind_int64(6, r.size);
            st.bind_int64(7, r.width);
            st.bind_int64(8, r.height);
            st.bind_int64(9, r.added);
            st.bind_int64(10, r.blob_bytes);
            st.bind_int64(11, r.thumb_bytes);
            st.exec();
        });
    }

    void store::delete_image(const std::string& profile, const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_images WHERE profile = ? AND id = ?");
            st.bind(1, profile);
            st.bind(2, id);
            st.exec();
        });
    }

    std::vector<std::string> store::delete_images_of_owner(const std::string& profile, const std::string& owner)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::vector<std::string> ids;
        {
            gulliver::statement st(*db_, "SELECT id FROM profile_images WHERE profile = ? AND owner = ?");
            st.bind(1, profile);
            st.bind(2, owner);
            st.exec();
            while (st.move_next()) ids.push_back(st.get_column_as_text(0));
        }
        writing([&]() {
            gulliver::statement st(*db_, "DELETE FROM profile_images WHERE profile = ? AND owner = ?");
            st.bind(1, profile);
            st.bind(2, owner);
            st.exec();
        });
        return ids;
    }

    void store::image_usage(const std::string& profile, int64_t& count, int64_t& bytes)
    {
        std::lock_guard<std::mutex> lock(m_);
        gulliver::statement st(*db_, "SELECT COUNT(*), COALESCE(SUM(blob_bytes + thumb_bytes), 0) "
            "FROM profile_images WHERE profile = ?");
        st.bind(1, profile);
        st.exec();
        count = 0;
        bytes = 0;
        if (st.move_next())
        {
            count = st.get_column_as_int64(0);
            bytes = st.get_column_as_int64(1);
        }
    }

}
