// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le test du mail à l'administrateur (bin/mail_admin, Gulliver 7.20.0).

        build/macos-gulliver/tests/test_mail_admin

    Sur un faux dossier de données (build/mail-admin-test, effacé à la fin) et un faux facteur (tools/faux_smtp.h),
    jamais le vrai Bridge.  Vérifie : le mail part vers l'adresse du premier administrateur de la base, avec l'objet et
    le texte donnés, par l'adresse d'envoi et le mot de passe des réglages ; il ne part pas quand mail.secret a été
    donné pour une autre adresse ou un autre port, ni quand l'administrateur n'a pas d'adresse.
*/

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sqlite3.h>

#include "../common/platform.h"
#include "../tools/faux_smtp.h"

namespace
{
    int failures = 0;
    std::string work, program;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s%s\n", ok ? "ok   " : "FAIL ", what.c_str());
        std::fflush(stdout);
        if (!ok) ++failures;
    }

    bool sql(const std::string& db, const std::string& statements)
    {
        sqlite3* h = nullptr;
        if (sqlite3_open(db.c_str(), &h) != SQLITE_OK) return false;
        const bool ok = sqlite3_exec(h, statements.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(h);
        return ok;
    }

    void write(const std::string& name, const std::string& text)
    {
        if (!gulliver::write_file_atomic(gulliver::join_path(work, name), text)) std::printf("cannot write %s\n", name.c_str());
    }

    int send(const std::string& subject, const std::string& body)
    {
        const std::string input = gulliver::join_path(work, "body.txt");
        if (!gulliver::write_file_atomic(input, body)) return -1;
        const std::string command = "'" + program + "' '" + work + "' '" + subject + "' < '" + input + "' 2> '" +
            gulliver::join_path(work, "stderr.txt") + "'";
        const int st = std::system(command.c_str());
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
}

int main(int argc, char** argv)
{
    const std::string here = gulliver::parent_path(gulliver::executable_path());   // build/macos-gulliver/tests
    const std::string build = gulliver::parent_path(here);
    const std::string root = gulliver::parent_path(gulliver::parent_path(build));
    work = gulliver::join_path(root, "build/mail-admin-test");
    program = argc > 1 ? argv[1] : gulliver::join_path(build, "bin/mail_admin");
    if (std::system(("rm -rf '" + work + "'").c_str()) != 0) std::printf("cannot empty %s\n", work.c_str());
    gulliver::make_dirs(work);

    gulliver::essai::fake_smtp smtp;
    const std::string port = std::to_string(smtp.port);
    const std::string db = gulliver::join_path(work, "gulliver.db");
    sql(db, "CREATE TABLE profiles (id TEXT, name TEXT, email TEXT, admin INTEGER, created INTEGER);"
            "INSERT INTO profiles VALUES ('a', 'autre', 'autre@essai.invalid', 0, 1);"
            "INSERT INTO profiles VALUES ('b', 'papy', 'papy@essai.invalid', 1, 2);"
            "INSERT INTO profiles VALUES ('c', 'secours', 'secours@essai.invalid', 1, 3);");
    write("settings.json", "{\"port\": 18443, \"mail_from\": \"gulliver@essai.invalid\", \"mail_port\": " + port + "}");
    write("mail.secret", "{\"scope\": \"gulliver@essai.invalid|" + port + "\", \"secret\": \"mot-de-passe-essai\"}");

    check(send("Gulliver : essai é", "Bonjour,\nune ligne accentuée : ça marche.\n") == 0, "le mail part");
    gulliver::essai::mail m;
    check(smtp.last("papy@essai.invalid", m, 0, 5), "il va au premier administrateur");
    check(smtp.count("secours@essai.invalid") == 0 && smtp.count("autre@essai.invalid") == 0, "et à lui seul");
    check(m.subject == "Gulliver : essai é", "avec l'objet donné");
    check(m.body.find("une ligne accentuée : ça marche.") != std::string::npos, "et le texte donné");
    check(m.from == "gulliver@essai.invalid", "par l'adresse d'envoi des réglages");
    check(smtp.logins.size() == 1 && smtp.logins[0].first == "gulliver@essai.invalid" &&
              smtp.logins[0].second == "mot-de-passe-essai", "avec le mot de passe de mail.secret");

    write("mail.secret", "{\"scope\": \"gulliver@essai.invalid|1\", \"secret\": \"mot-de-passe-essai\"}");
    check(send("refus", "x") == 1 && smtp.all().size() == 1, "pas de mail quand mail.secret vise un autre port");
    std::string err;
    gulliver::read_file(gulliver::join_path(work, "stderr.txt"), err);
    check(err.find("mot-de-passe-essai") == std::string::npos, "le mot de passe n'est jamais écrit");

    write("mail.secret", "{\"scope\": \"gulliver@essai.invalid|" + port + "\", \"secret\": \"mot-de-passe-essai\"}");
    sql(db, "UPDATE profiles SET email = '' WHERE admin = 1;");
    check(send("refus", "x") == 1 && smtp.all().size() == 1, "pas de mail quand l'administrateur n'a pas d'adresse");

    if (std::system(("rm -rf '" + work + "'").c_str()) != 0) std::printf("cannot empty %s\n", work.c_str());
    std::printf("test_mail_admin: %s\n", failures == 0 ? "all checks passed" : (std::to_string(failures) + " checks failed").c_str());
    return failures == 0 ? 0 : 1;
}
