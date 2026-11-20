// Copyright (C) 2011  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
// Repris de dlib (dlib/sqlite/sqlite.h et sqlite_tools.h) le 2026-11-20 (fil rouge, morceau 3) :
// l'enveloppe de SQLite de Gulliver, sur la bibliothèque SQLite de macOS.  Retirés : la classe
// d'erreurs, noncopyable, les assertions et la sérialisation de dlib (bind_object,
// get_column_as_object, query_object), dont Gulliver ne se sert pas.  La base s'ouvre en mode
// « serialized » (SQLITE_OPEN_FULLMUTEX) : le SQLite de macOS est compilé en THREADSAFE=2, où une
// connexion ne doit servir qu'à un fil à la fois, et la copie qu'il remplace l'était en THREADSAFE=1.
#ifndef GULLIVER_SQLITE_DB_H_
#define GULLIVER_SQLITE_DB_H_

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <sqlite3.h>

namespace gulliver
{

// --------------------------------------------------------------------------------------------

    struct sqlite_error : public std::runtime_error
    {
        explicit sqlite_error(const std::string& message): std::runtime_error(message) {}
    };

// --------------------------------------------------------------------------------------------

    namespace impl
    {
        struct db_deleter
        {
            void operator()(
                sqlite3* db
            )const 
            { 
                sqlite3_close(db);
            }
        };
    }

// --------------------------------------------------------------------------------------------

    class database
    {
    public:
        database(const database&) = delete;
        database& operator=(const database&) = delete;

        database(
        ) 
        {
        }

        database (
            const std::string& file
        ) 
        {
            open(file);
        }

        bool is_open (
        ) const
        {
            return db.get() != 0;
        }

        void open (
            const std::string& file
        )
        {
            filename = file;
            sqlite3* ptr = 0;
            int status = sqlite3_open_v2(file.c_str(), &ptr, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
            if (status != SQLITE_OK)
            {
                // sqlite3_open() hands back a connection even when it fails, so that the
                // reason can be read from it.  It is closed here rather than kept, so that
                // a failed open leaves this object closed.
                const std::string message = ptr ? sqlite3_errmsg(ptr) : "unable to allocate a database connection";
                sqlite3_close(ptr);
                db.reset();
                throw sqlite_error(message);
            }
            db.reset(ptr, impl::db_deleter());
        }

        const std::string& get_database_filename (
        ) const
        {
            // make sure requires clause is not broken

            return filename;
        }

        inline void exec (
            const std::string& sql_statement
        );

        std::int64_t last_insert_rowid (
        ) const
        {
            return sqlite3_last_insert_rowid(db.get());
        }

    private:

        friend class statement;

        std::string filename;
        std::shared_ptr<sqlite3> db;
    };

// --------------------------------------------------------------------------------------------

    class statement
    {
    public:
        statement(const statement&) = delete;
        statement& operator=(const statement&) = delete;

        statement (
            database& db_,
            const std::string sql_statement
        ) : 
            needs_reset(false),
            step_status(SQLITE_DONE),
            at_first_step(true),
            db(db_.db),
            stmt(0),
            sql_string(sql_statement)
        {
            // make sure requires clause is not broken

            int status = sqlite3_prepare_v2(db.get(), 
                                         sql_string.c_str(),
                                         sql_string.size()+1,
                                         &stmt,
                                         NULL);

            if (status != SQLITE_OK)
            {
                sqlite3_finalize(stmt);
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
            if (stmt == 0)
            {
                throw sqlite_error("Invalid SQL statement");
            }
        }

        ~statement(
        )
        {
            sqlite3_finalize(stmt);
        }

        void exec(
        )
        {
            reset();

            step_status = sqlite3_step(stmt);
            needs_reset = true;
            if (step_status != SQLITE_DONE && step_status != SQLITE_ROW)
            {
                // Statements prepared with sqlite3_prepare_v2() report the specific error
                // code from sqlite3_step(), a constraint violation or a busy database for
                // instance, and its message from sqlite3_errmsg().
                if (step_status == SQLITE_BUSY)
                    throw sqlite_error(std::string("statement::exec() failed.  SQLITE_BUSY returned: ") + sqlite3_errmsg(db.get()));
                else
                    throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

        bool move_next (
        )
        {
            if (step_status == SQLITE_ROW)
            {
                if (at_first_step)
                {
                    at_first_step = false;
                    return true;
                }
                else
                {
                    step_status = sqlite3_step(stmt);
                    if (step_status == SQLITE_DONE)
                    {
                        return false;
                    }
                    else if (step_status == SQLITE_ROW)
                    {
                        return true;
                    }
                    else
                    {
                        throw sqlite_error(sqlite3_errmsg(db.get()));
                    }
                }
            }
            else
            {
                return false;
            }
        }

        unsigned long get_num_columns(
        ) const
        {
            if( (at_first_step==false) && (step_status==SQLITE_ROW))
            {
                return sqlite3_column_count(stmt);
            }
            else
            {
                return 0;
            }
        }

        const std::string& get_sql_string (
        ) const
        {
            return sql_string;
        }

        template <typename T>
        typename std::enable_if<std::numeric_limits<T>::is_integer>::type get_column (
            unsigned long idx,
            T& item
        ) const
        {
            // unsigned ints won't fit into int all the time so put those into 64bit ints.
            if (sizeof(T) < sizeof(int) || (sizeof(T)==sizeof(int) && std::is_signed<T>::value))
                item = get_column_as_int(idx);
            else
                item = get_column_as_int64(idx);
        }

        void get_column(unsigned long idx, std::string& item) const { item = get_column_as_text(idx); }
        void get_column(unsigned long idx, float& item      ) const { item = get_column_as_double(idx); }
        void get_column(unsigned long idx, double& item     ) const { item = get_column_as_double(idx); }
        void get_column(unsigned long idx, long double& item) const { item = get_column_as_double(idx); }

        const std::vector<char> get_column_as_blob (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            const char* data = static_cast<const char*>(sqlite3_column_blob(stmt, idx));
            const int size = sqlite3_column_bytes(stmt, idx);

            // A NULL column and an empty BLOB both come back as a null pointer.
            if (data == 0 || size <= 0)
                return std::vector<char>();
            return std::vector<char>(data, data+size);
        }


        const std::string get_column_as_text (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            // sqlite3_column_bytes() is called after sqlite3_column_text() so that it
            // reports the size of the UTF-8 text, which may contain NUL characters.
            const char* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
            const int size = sqlite3_column_bytes(stmt, idx);
            if (data != 0 && size > 0)
                return std::string(data, size);
            else
                return std::string();
        }

        double get_column_as_double (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            return sqlite3_column_double(stmt, idx);
        }

        int get_column_as_int (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            return sqlite3_column_int(stmt, idx);
        }

        std::int64_t get_column_as_int64 (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            return sqlite3_column_int64(stmt, idx);
        }

        const std::string get_column_name (
            unsigned long idx
        ) const
        {
            // make sure requires clause is not broken

            return std::string(sqlite3_column_name(stmt,idx));
        }

        unsigned long get_max_parameter_id (
        ) const
        {
            return sqlite3_limit(db.get(), SQLITE_LIMIT_VARIABLE_NUMBER, -1);
        }

        unsigned long get_parameter_id (
            const std::string& name
        ) const
        {
            return sqlite3_bind_parameter_index(stmt, name.c_str());
        }

        template <typename T>
        typename std::enable_if<std::numeric_limits<T>::is_integer>::type bind (
            unsigned long idx,
            const T& item
        ) 
        {
            // unsigned ints won't fit into int all the time so put those into 64bit ints.
            if (sizeof(T) < sizeof(int) || (sizeof(T)==sizeof(int) && std::is_signed<T>::value))
                bind_int(idx, item);
            else
                bind_int64(idx, item);
        }

        void bind(unsigned long idx, const std::string& item) { bind_text(idx, item); }
        void bind(unsigned long idx, const float& item      ) { bind_double(idx, item); }
        void bind(unsigned long idx, const double& item     ) { bind_double(idx, item); }
        void bind(unsigned long idx, const long double& item) { bind_double(idx, item); }


        void bind_blob (
            unsigned long parameter_id,
            const std::vector<char>& item
        )
        {
            // make sure requires clause is not broken

            reset();
            // A null pointer given to sqlite3_bind_blob() binds NULL, so an empty vector is
            // bound as a BLOB of length zero explicitly.
            int status = item.empty() ? sqlite3_bind_zeroblob(stmt, parameter_id, 0)
                : sqlite3_bind_blob64(stmt, parameter_id, item.data(), static_cast<sqlite3_uint64>(item.size()), SQLITE_TRANSIENT);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }


        void bind_double (
            unsigned long parameter_id,
            const double& item
        )
        {
            // make sure requires clause is not broken

            reset();
            int status = sqlite3_bind_double(stmt, parameter_id, item);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

        void bind_int (
            unsigned long parameter_id,
            const int& item
        )
        {
            // make sure requires clause is not broken

            reset();
            int status = sqlite3_bind_int(stmt, parameter_id, item);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

        void bind_int64 (
            unsigned long parameter_id,
            const std::int64_t& item
        )
        {
            // make sure requires clause is not broken

            reset();
            int status = sqlite3_bind_int64(stmt, parameter_id, item);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

        void bind_null (
            unsigned long parameter_id
        )
        {
            // make sure requires clause is not broken

            reset();
            int status = sqlite3_bind_null(stmt, parameter_id);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

        void bind_text (
            unsigned long parameter_id,
            const std::string& item
        )
        {
            // make sure requires clause is not broken

            reset();
            // The length is given explicitly so that text holding NUL characters is bound
            // whole rather than cut at the first of them.
            int status = sqlite3_bind_text64(stmt, parameter_id, item.c_str(), static_cast<sqlite3_uint64>(item.size()),
                                             SQLITE_TRANSIENT, SQLITE_UTF8);

            if (status != SQLITE_OK)
            {
                throw sqlite_error(sqlite3_errmsg(db.get()));
            }
        }

    private:

        void reset()
        {
            if (needs_reset)
            {
                if (sqlite3_reset(stmt) != SQLITE_OK)
                {
                    step_status = SQLITE_DONE;
                    throw sqlite_error(sqlite3_errmsg(db.get()));
                }
                needs_reset = false;
                step_status = SQLITE_DONE;
                at_first_step = true;
            }
        }

        bool needs_reset; // true if sqlite3_step() has been called more recently than sqlite3_reset() 
        int step_status;
        bool at_first_step;

        std::shared_ptr<sqlite3> db;
        sqlite3_stmt* stmt;
        std::string sql_string;
    };

// --------------------------------------------------------------------------------------------

    void database::
    exec (
        const std::string& sql_statement
    )
    {
        // make sure requires clause is not broken

        statement(*this, sql_statement).exec();
    }

// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
    namespace impl
    {
        inline bool exception_in_flight (
        )
        {
#if __cplusplus >= 201703L
            return std::uncaught_exceptions() > 0;
#else
            return std::uncaught_exception();
#endif
        }
    }

// ----------------------------------------------------------------------------------------

    class transaction
    {
    public:
        transaction(const transaction&) = delete;
        transaction& operator=(const transaction&) = delete;

        transaction (
            database& db_
        ) :
            db(db_),
            committed(false)
        {
            db.exec("begin transaction");
        }

        void commit ()
        {
            if (!committed)
            {
                // Marked as committed only once the commit succeeded: a commit that throws,
                // on a busy database for instance, leaves the transaction open, and the
                // destructor then rolls it back.
                db.exec("commit");
                committed = true;
            }
        }

        ~transaction() noexcept(false)
        {
            if (!committed)
            {
                // A second exception thrown while another one propagates would terminate the
                // program, so a failed rollback is only reported when nothing else is.
                if (impl::exception_in_flight())
                {
                    try { db.exec("rollback"); } catch (...) {}
                }
                else
                {
                    db.exec("rollback");
                }
            }
        }

    private:
        database& db;
        bool committed;

    };

// ----------------------------------------------------------------------------------------
}

#endif // GULLIVER_SQLITE_DB_H_
