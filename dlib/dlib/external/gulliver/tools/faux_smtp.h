// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_TOOLS_FAUX_SMTP_H_
#define GULLIVER_TOOLS_FAUX_SMTP_H_

/*
    Le faux facteur des tests (à la place de tools/faux_smtp.py, Gulliver 7.20.0) : un serveur SMTP sur 127.0.0.1,
    sur un port libre, qui accepte tout message et le garde en mémoire au lieu de l'envoyer.  Il comprend EHLO, AUTH
    PLAIN (l'utilisateur et le mot de passe reçus sont gardés dans logins), MAIL, RCPT, DATA, RSET, NOOP et QUIT, ce
    dont se sert host/mailer.cpp, et lit le sujet (encodé =?UTF-8?B?…?= quand il le faut) et le corps en base64.
    Jamais le vrai Bridge : les tests du service s'en servent pour lire les codes et les liens des mails.
*/

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../host/crypto.h"

namespace gulliver::essai
{
    inline std::string lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    inline void pause_for(double seconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
    }

    struct mail
    {
        std::string from, to, subject, body;
    };

    class fake_smtp
    {
    public:
        int port = 0;
        std::vector<std::pair<std::string, std::string>> logins;

        fake_smtp()
        {
            fd_ = socket(AF_INET, SOCK_STREAM, 0);
            int one = 1;
            setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = 0;
            bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            listen(fd_, 16);
            socklen_t n = sizeof(a);
            getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &n);
            port = ntohs(a.sin_port);
            accepter_ = std::thread([this]() {
                for (;;)
                {
                    const int c = accept(fd_, nullptr, nullptr);
                    if (c < 0) return;
                    std::thread([this, c]() { serve(c); }).detach();
                }
            });
        }

        ~fake_smtp()
        {
            shutdown(fd_, SHUT_RDWR);
            close(fd_);
            if (accepter_.joinable()) accepter_.join();
        }

        fake_smtp(const fake_smtp&) = delete;
        fake_smtp& operator=(const fake_smtp&) = delete;

        // Tous les mails reçus jusqu'ici, dans l'ordre d'arrivée.
        std::vector<mail> all()
        {
            std::lock_guard<std::mutex> lock(m_);
            return mails_;
        }

        int count(const std::string& to)
        {
            std::lock_guard<std::mutex> lock(m_);
            return static_cast<int>(std::count_if(mails_.begin(), mails_.end(), [&](const mail& m) { return m.to == lower(to); }));
        }

        // Le dernier mail vers to, parmi ceux reçus après les `after` premiers ; attend jusqu'à `seconds` secondes.
        bool last(const std::string& to, mail& out, int after = 0, double seconds = 10)
        {
            for (int i = 0; i < static_cast<int>(seconds * 10); ++i)
            {
                {
                    std::lock_guard<std::mutex> lock(m_);
                    std::vector<mail> mine;
                    for (const mail& m : mails_)
                        if (m.to == lower(to)) mine.push_back(m);
                    if (static_cast<int>(mine.size()) > after)
                    {
                        out = mine.back();
                        return true;
                    }
                }
                pause_for(0.1);
            }
            return false;
        }

        // Le code à 8 chiffres du dernier mail vers to (après les `after` premiers), ou "".
        std::string code_for(const std::string& to, int after = 0)
        {
            mail m;
            std::smatch r;
            return last(to, m, after) && std::regex_search(m.body, r, std::regex("\\b([0-9]{8})\\b")) ? r[1].str() : "";
        }

        // Le jeton du lien « choisir son mot de passe » du dernier mail vers to, ou "".
        std::string reset_for(const std::string& to, int after = 0)
        {
            mail m;
            std::smatch r;
            return last(to, m, after) && std::regex_search(m.body, r, std::regex("#reset=([0-9a-f]{64})")) ? r[1].str() : "";
        }

    private:
        int fd_ = -1;
        std::thread accepter_;
        std::mutex m_;
        std::vector<mail> mails_;

        static bool read_line(int c, std::string& line)
        {
            line.clear();
            char ch;
            while (recv(c, &ch, 1, 0) == 1)
            {
                if (ch == '\n')
                {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }
                line += ch;
            }
            return false;
        }

        static void reply(int c, const std::string& line)
        {
            const std::string s = line + "\r\n";
            send(c, s.data(), s.size(), 0);
        }

        // Un sujet en clair, ou encodé =?UTF-8?B?…?= par host/mailer.cpp.
        static std::string decode_subject(const std::string& s)
        {
            const std::string head = "=?UTF-8?B?";
            if (s.compare(0, head.size(), head) == 0 && s.size() > head.size() + 2 && s.compare(s.size() - 2, 2, "?=") == 0)
                return gulliver::base64_decode(s.substr(head.size(), s.size() - head.size() - 2));
            return s;
        }

        static std::string strip_brackets(std::string s)
        {
            s.erase(std::remove(s.begin(), s.end(), '<'), s.end());
            s.erase(std::remove(s.begin(), s.end(), '>'), s.end());
            return s;
        }

        void serve(int c)
        {
            reply(c, "220 faux-smtp ready");
            std::string sender;
            std::vector<std::string> rcpts;
            std::string line;
            while (read_line(c, line))
            {
                const std::string verb = lower(line.substr(0, line.find(' ')));
                if (verb == "ehlo" || verb == "helo")
                {
                    const std::string s = "250-faux-smtp\r\n250-8BITMIME\r\n250 AUTH PLAIN\r\n";
                    send(c, s.data(), s.size(), 0);
                }
                else if (verb == "auth")
                {
                    const std::string plain = gulliver::base64_decode(line.substr(line.rfind(' ') + 1));
                    const size_t a = plain.find('\0'), b = a == std::string::npos ? a : plain.find('\0', a + 1);
                    if (b == std::string::npos) reply(c, "535 bad auth");
                    else
                    {
                        std::lock_guard<std::mutex> lock(m_);
                        logins.push_back({plain.substr(a + 1, b - a - 1), plain.substr(b + 1)});
                        reply(c, "235 ok");
                    }
                }
                else if (verb == "mail")
                {
                    sender = strip_brackets(line.size() > 10 ? line.substr(10) : "");
                    rcpts.clear();
                    reply(c, "250 ok");
                }
                else if (verb == "rcpt")
                {
                    rcpts.push_back(lower(strip_brackets(line.size() > 8 ? line.substr(8) : "")));
                    reply(c, "250 ok");
                }
                else if (verb == "data")
                {
                    reply(c, "354 go");
                    std::string subject, body;
                    bool in_body = false;
                    while (read_line(c, line) && line != ".")
                    {
                        if (in_body) body += line;
                        else if (line.empty()) in_body = true;
                        else if (line.compare(0, 9, "Subject: ") == 0) subject = decode_subject(line.substr(9));
                    }
                    const std::string text = gulliver::base64_decode(body);
                    {
                        std::lock_guard<std::mutex> lock(m_);
                        for (const std::string& r : rcpts) mails_.push_back({sender, r, subject, text});
                    }
                    reply(c, "250 queued");
                }
                else if (verb == "rset" || verb == "noop") reply(c, "250 ok");
                else if (verb == "quit")
                {
                    reply(c, "221 bye");
                    break;
                }
                else reply(c, "502 unknown");
            }
            close(c);
        }
    };
}

#endif // GULLIVER_TOOLS_FAUX_SMTP_H_
