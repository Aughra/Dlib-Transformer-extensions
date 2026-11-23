// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "mailer.h"

#include "certificates.h"
#include "crypto.h"
#include "../common/platform.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <ctime>

namespace gulliver
{
    namespace
    {
        // Une connexion au serveur SMTP, fermée par le destructeur.
        class smtp_link
        {
        public:
            explicit smtp_link(int port)
            {
                fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
                if (fd_ < 0) return;
                timeval tv{ 20, 0 };
                ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                int on = 1;
                ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
                sockaddr_in a{};
                a.sin_family = AF_INET;
                a.sin_port = htons(static_cast<uint16_t>(port));
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
                {
                    ::close(fd_);
                    fd_ = -1;
                }
            }
            ~smtp_link() { if (fd_ >= 0) ::close(fd_); }
            smtp_link(const smtp_link&) = delete;
            smtp_link& operator=(const smtp_link&) = delete;

            bool open() const { return fd_ >= 0; }

            bool send(const std::string& s)
            {
                size_t done = 0;
                while (done < s.size())
                {
                    const ssize_t n = ::send(fd_, s.data() + done, s.size() - done, 0);
                    if (n <= 0) return false;
                    done += static_cast<size_t>(n);
                }
                return true;
            }

            // Lit une réponse entière (les lignes « 250-… » puis « 250 … ») ; rend son code, ou 0.
            int reply(std::string& text)
            {
                text.clear();
                for (;;)
                {
                    std::string line;
                    if (!read_line(line)) return 0;
                    text += line + "\n";
                    if (line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[0]))) return 0;
                    if (line.size() == 3 || line[3] == ' ') return std::atoi(line.substr(0, 3).c_str());
                    if (text.size() > 64 * 1024) return 0;
                }
            }

        private:
            bool read_line(std::string& line)
            {
                for (;;)
                {
                    const size_t eol = buffer_.find("\r\n");
                    if (eol != std::string::npos)
                    {
                        line = buffer_.substr(0, eol);
                        buffer_.erase(0, eol + 2);
                        return true;
                    }
                    if (buffer_.size() > 8192) return false;
                    char chunk[1024];
                    const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
                    if (n <= 0) return false;
                    buffer_.append(chunk, static_cast<size_t>(n));
                }
            }

            int fd_ = -1;
            std::string buffer_;
        };

        // Le sujet en UTF-8, encodé selon la RFC 2047 quand il sort de l'ASCII.
        std::string encoded_subject(const std::string& subject)
        {
            const bool ascii = std::all_of(subject.begin(), subject.end(),
                [](char c) { return static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F; });
            return ascii ? subject : "=?UTF-8?B?" + base64_encode(subject) + "?=";
        }

        // Le corps en base64, en lignes de 76 caractères.
        std::string encoded_body(const std::string& body)
        {
            const std::string b = base64_encode(body);
            std::string out;
            for (size_t i = 0; i < b.size(); i += 76) out += b.substr(i, 76) + "\r\n";
            return out;
        }

        // La date au format de la RFC 5322, en temps universel.
        std::string mail_date()
        {
            const std::time_t t = std::time(nullptr);
            std::tm u{};
            gmtime_r(&t, &u);
            char text[64];
            std::strftime(text, sizeof(text), "%a, %d %b %Y %H:%M:%S +0000", &u);
            return text;
        }

        // Une ligne de l'en-tête ou du sujet sans retour à la ligne (qui ajouterait un en-tête).
        bool one_line(const std::string& s)
        {
            return s.find('\r') == std::string::npos && s.find('\n') == std::string::npos;
        }
    }

    bool valid_mail_address(const std::string& address)
    {
        if (address.size() < 3 || address.size() > 254) return false;
        for (const char c : address)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u <= 0x20 || u == 0x7F || c == '<' || c == '>' || c == '"' || c == '(' || c == ')' || c == ','
                || c == ';' || c == ':' || c == '\\' || c == '[' || c == ']')
                return false;
        }
        const size_t at = address.find('@');
        if (at == 0 || at == std::string::npos || address.find('@', at + 1) != std::string::npos) return false;
        const std::string domain = address.substr(at + 1);
        const size_t dot = domain.find('.');
        return dot != std::string::npos && dot > 0 && domain.back() != '.' && domain.find("..") == std::string::npos;
    }

    std::string normalized_mail_address(const std::string& address)
    {
        std::string a = address;
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        a.erase(a.begin(), std::find_if(a.begin(), a.end(), not_space));
        a.erase(std::find_if(a.rbegin(), a.rend(), not_space).base(), a.end());
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a;
    }

    bool send_mail(const mail_server& server, const std::string& to, const std::string& subject,
        const std::string& body, std::string& error)
    {
        if (!valid_mail_address(server.from)) { error = "no valid sending address (mail_from)"; return false; }
        if (!valid_mail_address(to)) { error = "invalid recipient"; return false; }
        if (!one_line(subject)) { error = "invalid subject"; return false; }
        if (server.port < 1 || server.port > 65535) { error = "invalid port"; return false; }
        smtp_link link(server.port);
        if (!link.open())
        {
            error = "the mail server 127.0.0.1:" + std::to_string(server.port) + " does not answer (is Proton Mail Bridge running?)";
            return false;
        }
        std::string text;
        // Une étape : envoie la commande (sauf vide) et attend l'un des codes permis.
        auto step = [&](const std::string& command, int expected, int also, const char* what) {
            if (!command.empty() && !link.send(command + "\r\n")) { error = std::string("lost during ") + what; return false; }
            const int code = link.reply(text);
            if (code == expected || (also && code == also)) return true;
            std::string answer = text.substr(0, text.find('\n'));
            if (answer.size() > 200) answer.resize(200);
            error = std::string("the mail server refused ") + what + " (" + (code ? answer : std::string("no answer")) + ")";
            return false;
        };
        if (!step("", 220, 0, "the connection") || !step("EHLO gulliver.local", 250, 0, "EHLO")) return false;
        if (!server.secret.empty())
        {
            std::string plain = std::string(1, '\0') + server.from + std::string(1, '\0') + server.secret;
            std::string command = "AUTH PLAIN " + base64_encode(plain);
            wipe_secret(plain);
            const bool ok = step(command, 235, 0, "the login");
            wipe_secret(command);
            if (!ok) return false;
        }
        if (!step("MAIL FROM:<" + server.from + ">", 250, 0, "the sender")
            || !step("RCPT TO:<" + to + ">", 250, 251, "the recipient")
            || !step("DATA", 354, 0, "DATA"))
            return false;
        const std::string domain = server.from.substr(server.from.find('@') + 1);
        std::string message = "From: Gulliver <" + server.from + ">\r\n"
            "To: <" + to + ">\r\n"
            "Subject: " + encoded_subject(subject) + "\r\n"
            "Date: " + mail_date() + "\r\n"
            "Message-ID: <" + random_hex(16) + "@" + domain + ">\r\n"
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: base64\r\n"
            "\r\n" + encoded_body(body) + ".\r\n";
        // Le base64 ne commence jamais une ligne par un point : aucun doublement n'est nécessaire.
        if (!link.send(message)) { error = "lost while sending the message"; return false; }
        if (!step("", 250, 0, "the message")) return false;
        link.send("QUIT\r\n");
        return true;
    }
}
