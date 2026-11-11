// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.


#include "web_access.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

#include <dlib/external/httplib/httplib.h>

#include "../common/log.h"
#include "../common/version.h"
#include "crypto.h"
#include "http_fetch.h"
#include "public_link.h"

namespace albireo
{

    namespace
    {
        using clock_type = std::chrono::steady_clock;

        // Les limites d'une requête : chaque attente de données et la requête entière (connect_seconds borne la
        // connexion).
        const int read_seconds = 15;
        const int total_seconds = 30;
        // Les octets lus d'un texte, coupé au-delà, d'un fichier PDF, refusé au-delà, et de la réponse d'un
        // fournisseur.
        const size_t max_text_bytes = size_t(2) << 20;
        const size_t max_pdf_bytes = size_t(20) << 20;
        const size_t max_answer_bytes = size_t(2) << 20;
        const int max_redirections = 5;
        /*
            Les requêtes de l'accès au web qui tournent à la fois, trois comme la politique de Wikimedia
            le demande à ses clients.  La réponse pour un fichier PDF de 20 Mo contient ses octets, leur
            base64 et le texte de son document JSON, environ 75 Mo, jusqu'à ce que le serveur l'ait écrite.
        */
        const int max_requests = 3;
        std::atomic<int> running_requests{0};

        // Ce que règlent les fonctions réservées aux tests : les adresses de bouclage lues, l'origine du fournisseur.
        std::atomic<bool> loopback_allowed{false};
        std::mutex provider_m;
        std::string provider_origin;

        // Levée depuis la socket d'un client sur le point de se connecter une fois les requêtes du service annulées.
        struct connection_cancelled
        {
        };

        // Les 32 bits de l'adresse IPv4 a.b.c.d.
        constexpr uint32_t ipv4(unsigned a, unsigned b, unsigned c, unsigned d)
        {
            return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
        }

        struct ipv4_range
        {
            uint32_t base;
            int bits;
        };

        // Les plages IPv4 qui ne sont pas publiques.
        const ipv4_range private_ipv4[] = {
            {ipv4(0, 0, 0, 0), 8}, {ipv4(10, 0, 0, 0), 8}, {ipv4(100, 64, 0, 0), 10}, {ipv4(127, 0, 0, 0), 8},
            {ipv4(169, 254, 0, 0), 16}, {ipv4(172, 16, 0, 0), 12}, {ipv4(192, 0, 0, 0), 24},
            {ipv4(192, 0, 2, 0), 24}, {ipv4(192, 88, 99, 0), 24}, {ipv4(192, 168, 0, 0), 16},
            {ipv4(198, 18, 0, 0), 15}, {ipv4(198, 51, 100, 0), 24}, {ipv4(203, 0, 113, 0), 24},
            {ipv4(224, 0, 0, 0), 4}, {ipv4(240, 0, 0, 0), 4}
        };

        bool public_ipv4(uint32_t a)
        {
            for (const auto& r : private_ipv4)
                if ((a & (0xffffffffu << (32 - r.bits))) == r.base) return false;
            return true;
        }

        struct ipv6_range
        {
            unsigned char prefix[16];
            int bits;
        };

        /*
            Les plages IPv6 qui ne sont pas publiques : ::/96 (::, ::1 et les adresses compatibles IPv4,
            que RFC 4291 déclare obsolètes), 64:ff9b:1::/48 (le préfixe de RFC 8215 pour le NAT64 d'un
            réseau propre), 100::/64, 2001::/32 (Teredo), 2001:db8::/32, fc00::/7, fe80::/10, fec0::/10
            et ff00::/8.
        */
        const ipv6_range private_ipv6[] = {
            {{0}, 96},
            {{0x00, 0x64, 0xff, 0x9b, 0x00, 0x01}, 48},
            {{0x01, 0x00}, 64},
            {{0x20, 0x01, 0x00, 0x00}, 32},
            {{0x20, 0x01, 0x0d, 0xb8}, 32},
            {{0xfc}, 7},
            {{0xfe, 0x80}, 10},
            {{0xfe, 0xc0}, 10},
            {{0xff}, 8}
        };

        // Les plages qui portent une adresse IPv4 : dans leurs 32 derniers bits, ou dans les bits 16 à 47 pour
        // 2002::/16.
        const ipv6_range mapped_ipv4 = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}, 96};
        const ipv6_range nat64 = {{0x00, 0x64, 0xff, 0x9b}, 96};
        const ipv6_range six_to_four = {{0x20, 0x02}, 16};

        bool in_range(const unsigned char* a, const ipv6_range& r)
        {
            int bits = r.bits;
            for (int i = 0; i < 16 && bits > 0; ++i, bits -= 8)
            {
                const unsigned mask = bits >= 8 ? 0xffu : (0xffu << (8 - bits)) & 0xffu;
                if ((a[i] & mask) != (r.prefix[i] & mask)) return false;
            }
            return true;
        }

        uint32_t ipv4_at(const unsigned char* b)
        {
            return ipv4(b[0], b[1], b[2], b[3]);
        }

        bool public_ipv6(const unsigned char* a)
        {
            if (in_range(a, mapped_ipv4) || in_range(a, nat64)) return public_ipv4(ipv4_at(a + 12));
            if (in_range(a, six_to_four)) return public_ipv4(ipv4_at(a + 2));
            for (const auto& r : private_ipv6)
                if (in_range(a, r)) return false;
            return true;
        }

        // Une adresse IP : sa famille, 4 ou 6, ses octets, et le texte auquel se connecte le client d'httplib.
        struct ip_address
        {
            int family = 0;
            unsigned char bytes[16] = {};
            std::string text;
        };

        // Une adresse IPv6 en huit groupes hexadécimaux, sans partie pointée, la forme que lit le client d'httplib.
        std::string ipv6_text(const unsigned char* b)
        {
            std::string out;
            for (int g = 0; g < 8; ++g)
            {
                char group[8];
                std::snprintf(group, sizeof(group), "%x", (unsigned(b[2 * g]) << 8) | unsigned(b[2 * g + 1]));
                if (g) out += ':';
                out += group;
            }
            return out;
        }

        // Le texte d'une adresse IP tel que le donne le résolveur du système.
        bool parse_ip(const std::string& text, ip_address& ip)
        {
            ip = ip_address();
            if (inet_pton(AF_INET, text.c_str(), ip.bytes) == 1)
            {
                ip.family = 4;
                ip.text = text;
                return true;
            }
            if (inet_pton(AF_INET6, text.c_str(), ip.bytes) == 1)
            {
                ip.family = 6;
                ip.text = ipv6_text(ip.bytes);
                return true;
            }
            return false;
        }

        // Indique si web_fetch() se connecte à ip : une adresse publique, ou une adresse de bouclage tant que les
        // tests le permettent.
        bool readable_address(const ip_address& ip)
        {
            static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
            if (ip.family == 4) return public_ipv4(ipv4_at(ip.bytes)) || (loopback_allowed && ip.bytes[0] == 127);
            return ip.family == 6
                && (public_ipv6(ip.bytes) || (loopback_allowed && std::memcmp(ip.bytes, loopback, 16) == 0));
        }

        std::string lower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        std::string trim(const std::string& s)
        {
            size_t from = 0, to = s.size();
            while (from < to && is_space(s[from])) ++from;
            while (to > from && is_space(s[to - 1])) --to;
            return s.substr(from, to - from);
        }

        int hex_value(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // Une adresse du web, telle que ce module la lit.
        struct web_address
        {
            std::string scheme;     // "http" ou "https"
            std::string host;       // un nom en minuscules, ou le texte d'une adresse IP
            int port = 0;
            std::string target;     // le chemin et la requête, encodés pour la ligne de requête
            bool literal = false;   // l'hôte est l'adresse IP ip
            ip_address ip;
        };

        // L'hôte tel qu'une adresse l'écrit, une adresse IPv6 entre crochets.
        std::string host_part(const web_address& a)
        {
            return a.literal && a.ip.family == 6 ? "[" + a.host + "]" : a.host;
        }

        // L'origine du client d'httplib, "https://host:port".
        std::string origin_of(const web_address& a)
        {
            const std::string host = a.literal && a.ip.family == 6 ? "[" + a.ip.text + "]" : a.host;
            return a.scheme + "://" + host + ":" + std::to_string(a.port);
        }

        // L'origine telle qu'une adresse l'écrit, sans le port de son schéma.
        std::string written_origin(const web_address& a)
        {
            const bool usual = a.port == (a.scheme == "https" ? 443 : 80);
            return a.scheme + "://" + host_part(a) + (usual ? std::string() : ":" + std::to_string(a.port));
        }

        // Le chemin et la requête pour la ligne de requête : les octets qu'une adresse ne peut pas contenir en %XX,
        // les échappements gardés.
        std::string request_target(const std::string& s)
        {
            static const char digits[] = "0123456789ABCDEF";
            std::string out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const bool escape = c == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0
                    && hex_value(s[i + 2]) >= 0;
                const bool plain = c > 0x20 && c < 0x7f && c != '%' && !std::strchr("\"<>\\^`{|}", c);
                if (escape || plain)
                {
                    out += static_cast<char>(c);
                }
                else
                {
                    out += '%';
                    out += digits[c >> 4];
                    out += digits[c & 15];
                }
            }
            return out;
        }

        // Une partie d'une adresse IPv4 en décimal, en octal après un 0, ou en hexadécimal après 0x.
        bool ipv4_number(const std::string& p, uint64_t& v)
        {
            if (p.empty()) return false;
            unsigned base = 10;
            size_t i = 0;
            if (p.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            {
                base = 16;
                i = 2;
            }
            else if (p.size() >= 2 && p[0] == '0')
            {
                base = 8;
                i = 1;
            }
            v = 0;
            for (; i < p.size(); ++i)
            {
                const int d = hex_value(p[i]);
                if (d < 0 || static_cast<unsigned>(d) >= base) return false;
                v = v * base + static_cast<unsigned>(d);
                if (v > 0xffffffffull) return false;
            }
            return true;
        }

        // Indique si le dernier label d'un nom est un nombre, ce qui fait du nom une adresse IPv4 (2130706433,
        // 0x7f.1).
        bool ends_in_number(const std::string& name)
        {
            const std::string last = name.substr(name.rfind('.') + 1);
            if (last.empty()) return false;
            if (last.find_first_not_of("0123456789") == std::string::npos) return true;
            return last.size() >= 2 && last[0] == '0' && (last[1] == 'x' || last[1] == 'X')
                && last.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
        }

        // L'adresse IPv4 d'un nom qui se termine par un nombre : une à quatre parties, la dernière remplissant les
        // bits restants.
        bool parse_ipv4_name(const std::string& name, uint32_t& address)
        {
            std::vector<uint64_t> parts;
            size_t start = 0;
            for (;;)
            {
                const size_t dot = name.find('.', start);
                uint64_t v = 0;
                if (!ipv4_number(name.substr(start, dot == std::string::npos ? std::string::npos : dot - start), v))
                    return false;
                parts.push_back(v);
                if (dot == std::string::npos) break;
                start = dot + 1;
            }
            if (parts.size() > 4) return false;
            uint64_t value = 0;
            for (size_t i = 0; i + 1 < parts.size(); ++i)
            {
                if (parts[i] > 255) return false;
                value |= parts[i] << (8 * (3 - i));
            }
            if (parts.back() >= (uint64_t(1) << (8 * (5 - parts.size())))) return false;
            address = static_cast<uint32_t>(value | parts.back());
            return true;
        }

        // Indique si un nom est fait de labels de lettres, chiffres, tirets et soulignés, de 63 caractères chacun
        // et de 253 en tout.
        bool valid_name(const std::string& name)
        {
            if (name.empty() || name.size() > 253) return false;
            size_t label = 0;
            for (const char c : name)
            {
                if (c == '.')
                {
                    if (label == 0) return false;
                    label = 0;
                    continue;
                }
                const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
                if (!allowed || ++label > 63) return false;
            }
            return label > 0;
        }

        // Indique si un nom désigne cet ordinateur ou un réseau local : un nom d'un seul label, localhost
        // compris, et les noms des domaines localhost, local, internal et home.arpa.
        bool local_name(const std::string& name)
        {
            if (name.find('.') == std::string::npos) return true;
            for (const char* domain : { ".localhost", ".local", ".internal", ".home.arpa" })
            {
                const size_t n = std::strlen(domain);
                if (name.size() > n && name.compare(name.size() - n, n, domain) == 0) return true;
            }
            return false;
        }

        /*
            Lit text comme une adresse du web : http ou https, un hôte, un port quand ce n'est pas
            celui du schéma, puis le chemin et la requête, le fragment laissé de côté.  L'hôte est
            un nom de lettres, chiffres, tirets, soulignés et points, mis en minuscules, une
            adresse IPv6 entre crochets, ou une adresse IPv4 sous l'une des formes que lit un
            résolveur (8.8.8.8, 134744072, 0x8.0x8.0x8.0x8, 8.526344).  Renvoie false avec why
            sinon.
        */
        bool parse_address(const std::string& text, web_address& a, std::string& why)
        {
            a = web_address();
            why = "the address is not an http or https address";
            if (text.empty() || text.size() > 8192) return false;
            for (const char c : text)
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
            const size_t scheme_end = text.find("://");
            if (scheme_end == std::string::npos) return false;
            a.scheme = lower(text.substr(0, scheme_end));
            if (a.scheme != "http" && a.scheme != "https") return false;
            const size_t start = scheme_end + 3;
            const size_t end = std::min(text.find_first_of("/?#", start), text.size());
            const std::string authority = text.substr(start, end - start);
            if (authority.find('@') != std::string::npos)
            {
                why = "an address with a user name or a password is refused";
                return false;
            }
            why = "the address names no valid host";
            std::string port;
            if (!authority.empty() && authority[0] == '[')
            {
                const size_t close = authority.find(']');
                if (close == std::string::npos) return false;
                const std::string inner = authority.substr(1, close - 1);
                const std::string rest = authority.substr(close + 1);
                if (!rest.empty() && rest[0] != ':') return false;
                if (!rest.empty()) port = rest.substr(1);
                char display[INET6_ADDRSTRLEN] = {};
                if (inet_pton(AF_INET6, inner.c_str(), a.ip.bytes) != 1
                    || !inet_ntop(AF_INET6, a.ip.bytes, display, sizeof(display)))
                    return false;
                a.literal = true;
                a.ip.family = 6;
                a.ip.text = ipv6_text(a.ip.bytes);
                a.host = display;
            }
            else
            {
                const size_t colon = authority.find(':');
                std::string host = lower(authority.substr(0, colon));
                if (colon != std::string::npos) port = authority.substr(colon + 1);
                if (!host.empty() && host.back() == '.') host.pop_back();
                if (std::any_of(host.begin(), host.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; }))
                {
                    why = "the name of a host is written in ASCII, an internationalized name in its xn-- form";
                    return false;
                }
                if (!valid_name(host)) return false;
                if (ends_in_number(host))
                {
                    uint32_t v = 0;
                    if (!parse_ipv4_name(host, v)) return false;
                    a.literal = true;
                    a.ip.family = 4;
                    for (int i = 0; i < 4; ++i) a.ip.bytes[i] = static_cast<unsigned char>(v >> (24 - 8 * i));
                    a.ip.text = std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "."
                        + std::to_string((v >> 8) & 255) + "." + std::to_string(v & 255);
                    host = a.ip.text;
                }
                a.host = host;
            }
            a.port = a.scheme == "https" ? 443 : 80;
            if (!port.empty())
            {
                why = "the port of the address is not a number from 1 to 65535";
                if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return false;
                a.port = std::atoi(port.c_str());
                if (a.port < 1 || a.port > 65535) return false;
            }
            std::string target = text.substr(end);
            target = target.substr(0, target.find('#'));
            if (target.empty() || target[0] != '/') target = "/" + target;
            a.target = request_target(target);
            why.clear();
            return true;
        }

        // Le chemin d'une adresse sans ses segments "." et ".." (RFC 3986, 5.2.4).
        std::string without_dot_segments(std::string in)
        {
            std::string out;
            auto drop_last = [&out]() {
                const size_t slash = out.rfind('/');
                out.erase(slash == std::string::npos ? 0 : slash);
            };
            while (!in.empty())
            {
                if (in.compare(0, 3, "../") == 0) in.erase(0, 3);
                else if (in.compare(0, 2, "./") == 0) in.erase(0, 2);
                else if (in.compare(0, 3, "/./") == 0) in.erase(0, 2);
                else if (in == "/.") in = "/";
                else if (in.compare(0, 4, "/../") == 0)
                {
                    in.erase(0, 3);
                    drop_last();
                }
                else if (in == "/..")
                {
                    in = "/";
                    drop_last();
                }
                else if (in == "." || in == "..")
                {
                    in.clear();
                }
                else
                {
                    const size_t next = in.find('/', 1);
                    out += in.substr(0, next);
                    in.erase(0, next == std::string::npos ? in.size() : next);
                }
            }
            return out;
        }

        // L'adresse vers laquelle mène une redirection : location, absolue ou relative à l'adresse from
        // (RFC 3986, 5.2).
        std::string resolve_location(const web_address& from, const std::string& value)
        {
            std::string location = trim(value);
            location = location.substr(0, location.find('#'));
            // Un schéma : une lettre, puis des lettres, chiffres, "+", "-" et ".", avant un deux-points.
            const size_t colon = location.find(':');
            if (colon != std::string::npos && colon > 0 && colon < location.find_first_of("/?")
                && std::isalpha(static_cast<unsigned char>(location[0]))
                && location.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.")
                    == colon)
                return location;
            if (location.compare(0, 2, "//") == 0) return from.scheme + ":" + location;
            const std::string origin = written_origin(from);
            const std::string path = from.target.substr(0, from.target.find('?'));
            if (location.empty()) return origin + from.target;
            if (location[0] == '?') return origin + path + location;
            const size_t q = location.find('?');
            const std::string own_path = location.substr(0, q);
            const std::string own_query = q == std::string::npos ? std::string() : location.substr(q);
            if (location[0] == '/') return origin + without_dot_segments(own_path) + own_query;
            return origin + without_dot_segments(path.substr(0, path.rfind('/') + 1) + own_path) + own_query;
        }

        // Un token d'un en-tête, un type de média ou un charset, vide quand il contient d'autres caractères ou
        // plus de 100.
        std::string header_token(const std::string& s)
        {
            if (s.size() > 100) return std::string();
            for (const char c : s)
                if (c == '\0' || (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr("!#$%&'*+-.^_`|~/", c)))
                    return std::string();
            return s;
        }

        // Le type de média d'un en-tête Content-Type en minuscules, et la valeur de son paramètre charset.
        void read_content_type(const std::string& header, std::string& type, std::string& charset)
        {
            type = header_token(lower(trim(header.substr(0, header.find(';')))));
            charset.clear();
            size_t at = header.find(';');
            while (at != std::string::npos)
            {
                const size_t next = header.find(';', at + 1);
                const std::string parameter = header.substr(at + 1, next == std::string::npos ? next : next - at - 1);
                const size_t eq = parameter.find('=');
                if (eq != std::string::npos && lower(trim(parameter.substr(0, eq))) == "charset")
                {
                    std::string value = trim(parameter.substr(eq + 1));
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                        value = value.substr(1, value.size() - 2);
                    charset = header_token(lower(value));
                }
                at = next;
            }
        }

        /*
            Indique si l'accès au web lit un type de média : text/<subtype>, application/xhtml+xml,
            application/xml, application/<name>+xml, application/json, application/<name>+json
            et application/pdf.
        */
        bool readable_type(const std::string& type)
        {
            auto ends_with = [&type](const std::string& suffix) {
                return type.size() > suffix.size()
                    && type.compare(type.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            if (type.size() > 5 && type.compare(0, 5, "text/") == 0) return true;
            if (type == "application/xml" || type == "application/json" || type == "application/pdf") return true;
            return type.size() > 16 && type.compare(0, 12, "application/") == 0
                && (ends_with("+xml") || ends_with("+json"));
        }

        // Ajoute les octets UTF-8 d'un point de code, U+FFFD pour une valeur qui n'est pas un caractère.
        void append_utf8(std::string& out, unsigned long cp)
        {
            if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xc0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xe0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
            else
            {
                out += static_cast<char>(0xf0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
        }

        // Le texte d'une entité HTML : les entités nommées du balisage et de l'espace, et les entités numériques.
        bool entity_text(const std::string& name, std::string& out)
        {
            static const char* const named[][2] = {
                {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}
            };
            for (const auto& e : named)
                if (name == e[0])
                {
                    out = e[1];
                    return true;
                }
            if (name.size() < 2 || name[0] != '#') return false;
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const size_t from = hex ? 2 : 1;
            if (from >= name.size()) return false;
            unsigned long cp = 0;
            for (size_t i = from; i < name.size(); ++i)
            {
                const int d = hex_value(name[i]);
                if (d < 0 || (!hex && d > 9)) return false;
                cp = cp * (hex ? 16 : 10) + static_cast<unsigned long>(d);
                if (cp > 0x10ffff) return false;
            }
            out.clear();
            append_utf8(out, cp);
            return true;
        }

        // Le texte, ses suites d'espaces, tabulations et fins de ligne réduites à une espace, et aucune à ses bouts.
        std::string tidy(const std::string& s)
        {
            std::string out;
            bool space = false;
            for (const char c : s)
            {
                if (is_space(c))
                {
                    space = !out.empty();
                    continue;
                }
                if (space) out += ' ';
                space = false;
                out += c;
            }
            return out;
        }

        // Le texte d'un fragment HTML que donne un fournisseur : ses balises retirées, ses entités décodées, tidy().
        std::string plain_text(const std::string& html)
        {
            std::string text;
            for (size_t i = 0; i < html.size();)
            {
                const char c = html[i];
                const char next = i + 1 < html.size() ? html[i + 1] : '\0';
                if (c == '<' && (std::isalpha(static_cast<unsigned char>(next)) || next == '/' || next == '!'))
                {
                    const size_t close = html.find('>', i);
                    i = close == std::string::npos ? html.size() : close + 1;
                    continue;
                }
                const size_t semicolon = c == '&' ? html.find(';', i + 1) : std::string::npos;
                std::string decoded;
                if (semicolon != std::string::npos && semicolon - i <= 12
                    && entity_text(html.substr(i + 1, semicolon - i - 1), decoded))
                {
                    text += decoded;
                    i = semicolon + 1;
                    continue;
                }
                text += c;
                ++i;
            }
            return tidy(text);
        }

        // Une valeur d'une requête : chaque octet sauf les caractères non réservés de RFC 3986 en %XX.
        std::string query_component(const std::string& s)
        {
            static const char digits[] = "0123456789ABCDEF";
            std::string out;
            for (const char ch : s)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                {
                    out += ch;
                }
                else
                {
                    out += '%';
                    out += digits[c >> 4];
                    out += digits[c & 15];
                }
            }
            return out;
        }

        // Le chemin d'un article de Wikipédia d'après sa clé : les caractères d'un chemin gardés, les autres en %XX.
        std::string article_path(const std::string& key)
        {
            static const char digits[] = "0123456789ABCDEF";
            std::string out;
            for (const char ch : key)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x80 && (std::isalnum(c) || (c != 0 && std::strchr("-._~!$&'()*+,;=:@/", ch))))
                {
                    out += ch;
                }
                else
                {
                    out += '%';
                    out += digits[c >> 4];
                    out += digits[c & 15];
                }
            }
            return out;
        }

        // Le User-Agent des requêtes : le programme, sa version et l'adresse de son projet.
        std::string user_agent()
        {
            return std::string(ALBIREO_PRODUCT_NAME) + "/" + ALBIREO_VERSION + " (+" + ALBIREO_PROJECT_URL + ")";
        }

        // Le User-Agent que demande la politique de Wikimedia, avec un contact.
        std::string wikimedia_user_agent()
        {
            return std::string(ALBIREO_PRODUCT_NAME) + "/" + ALBIREO_VERSION + " (" + ALBIREO_PROJECT_URL + "; "
                + ALBIREO_AUTHOR_EMAIL + ")";
        }

        web_reply failure(int status, const std::string& code, const std::string& message,
            const std::string& param = std::string())
        {
            web_reply r;
            r.status = status;
            r.code = code;
            r.message = message;
            r.param = param;
            return r;
        }

        web_reply refused(const std::string& host)
        {
            return failure(403, "address_refused", "the address of " + host
                + " is local or private, which the web access of Albireo does not read", "url");
        }

        web_reply timed_out(const std::string& host)
        {
            return failure(504, "timeout", host + " did not answer within " + std::to_string(total_seconds) + " s");
        }

        web_reply cancelled_reply()
        {
            return failure(503, "cancelled", "the service stops or goes offline: the web access opens no connection");
        }

        // Indique si l'accès au web est disponible avec s ; le refus sinon.
        bool available(const host_settings& s, web_reply& refusal)
        {
            if (s.offline)
                refusal = failure(403, "offline", "the service works offline: the web access opens no connection");
            else if (!s.web_access)
                refusal = failure(403, "web_access_off", "the administrator turned the web access off");
            return !s.offline && s.web_access;
        }

        // Une place parmi les requêtes de l'accès au web qui tournent à la fois, rendue à sa fin.
        class request_place
        {
        public:
            request_place() : held_(running_requests.fetch_add(1) < max_requests)
            {
                if (!held_) --running_requests;
            }
            ~request_place()
            {
                if (held_) --running_requests;
            }
            request_place(const request_place&) = delete;
            request_place& operator=(const request_place&) = delete;
            bool held() const { return held_; }

        private:
            bool held_;
        };

        web_reply busy_reply()
        {
            web_reply r = failure(503, "busy", "the web access runs " + std::to_string(max_requests)
                + " requests already: try again in a moment");
            r.retry_after = 2;
            return r;
        }

        // Une langue des requêtes : [a-z]{2,3}, puis une région de deux lettres ou trois chiffres ("pt-BR",
        // "es-419").
        bool read_language(const json& request, std::string& language, web_reply& refusal)
        {
            const json& value = request["language"];
            language.clear();
            if (value.is_null() || (value.is_string() && value.as_string().empty())) return true;
            const std::string text = value.as_string();
            const size_t dash = text.find('-');
            const std::string primary = lower(text.substr(0, dash));
            std::string region = dash == std::string::npos ? std::string() : text.substr(dash + 1);
            for (char& c : region)
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            const bool letters = (primary.size() == 2 || primary.size() == 3)
                && primary.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            const bool place = dash == std::string::npos
                || (region.size() == 2 && region.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos)
                || (region.size() == 3 && region.find_first_not_of("0123456789") == std::string::npos);
            if (value.is_string() && letters && place)
            {
                language = primary + (region.empty() ? std::string() : "-" + region);
                return true;
            }
            refusal = failure(400, "invalid_language", "language is a code of two or three letters, with a region "
                "after a hyphen (fr, pt-BR)", "language");
            return false;
        }

        // Ce qu'une réponse a donné : son statut, ses en-têtes, son corps.
        struct received
        {
            int status = 0;
            std::string content_type;    // le type de média, en minuscules
            std::string charset;
            std::string location;        // l'adresse que donne une redirection
            long long content_length = -1;
            int retry_after = 0;
            std::string body;
            bool truncated = false;
        };

        // Les octets d'un corps à lire, et si une réponse au-delà est refusée avec beyond, ou coupée.
        struct body_limit
        {
            size_t bytes = 0;
            bool refuse_beyond = false;
            web_reply beyond;
        };

        /*
            Décide d'après le statut et les en-têtes d'une réponse les octets de son corps à lire,
            ou refuse la réponse avec sa réplique.
        */
        typedef std::function<bool(const received&, body_limit&, web_reply&)> answer_check;

        // Le statut et les en-têtes d'une réponse que lit le module.
        void read_head(const httplib::Response& res, received& got)
        {
            got.status = res.status;
            read_content_type(res.get_header_value("Content-Type"), got.content_type, got.charset);
            got.location = res.get_header_value("Location");
            const std::string length = res.get_header_value("Content-Length");
            const bool number = !length.empty() && length.size() <= 18
                && length.find_first_not_of("0123456789") == std::string::npos;
            got.content_length = number ? std::atoll(length.c_str()) : -1;
            const std::string after = res.get_header_value("Retry-After");
            const bool seconds = !after.empty() && after.size() <= 5
                && after.find_first_not_of("0123456789") == std::string::npos;
            got.retry_after = seconds ? std::min(std::atoi(after.c_str()), 3600) : 0;
        }

        struct proxy_route
        {
            std::string host;
            int port = 0;
            std::string user;
            std::string password;
        };

        // Une connexion d'exchange() : à l'adresse pin, ou au proxy quand pin est nul.
        bool attempt(const web_address& a, const ip_address* pin, const proxy_route& proxy,
            const httplib::Headers& headers, clock_type::time_point deadline, const answer_check& accept,
            received& got, web_reply& fail, bool& unreachable)
        {
            unreachable = false;
            got = received();
            const std::string host = host_part(a);
            const long long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline
                - clock_type::now()).count();
            if (left <= 0)
            {
                fail = timed_out(host);
                return false;
            }
            const auto c = std::make_shared<httplib::Client>(origin_of(a));
            if (!c->is_valid())
            {
                fail = failure(502, "unreachable", host + " cannot be reached by the client of the web access");
                return false;
            }
            if (pin)
            {
                c->set_hostname_addr_map({ { c->host(), pin->text } });
            }
            else
            {
                c->set_proxy(proxy.host, proxy.port);
                if (!proxy.user.empty()) c->set_proxy_basic_auth(proxy.user, proxy.password);
            }
            c->set_follow_location(false);
            c->set_keep_alive(false);
            // La cible est déjà encodée (request_target()) et va telle quelle dans la ligne de requête.
            c->set_path_encode(false);
            // La connexion et la négociation TLS, chacune dans la moitié du temps restant et connect_seconds.
            const long long connect_ms = std::min<long long>(connect_seconds * 1000LL, left / 2);
            c->set_connection_timeout(static_cast<time_t>(connect_ms / 1000),
                static_cast<time_t>(connect_ms % 1000 * 1000));
            c->set_read_timeout(read_seconds, 0);
            c->set_write_timeout(read_seconds, 0);
            // Les attentes de données se terminent avec le temps restant.
            c->set_max_timeout(static_cast<time_t>(left));
            c->set_socket_options([](socket_t sock) {
                if (!http_cancelled()) return;
                httplib::detail::close_socket(sock);
                throw connection_cancelled();
            });
            http_registration registered(c);
            if (http_cancelled())
            {
                fail = cancelled_reply();
                return false;
            }
            body_limit limit;
            bool redirect = false, refused_answer = false, too_large = false, late = false;
            auto last = clock_type::now();
            httplib::Result result;
            try
            {
                result = c->Get(a.target, headers,
                    [&](const httplib::Response& res) {
                        last = clock_type::now();
                        read_head(res, got);
                        if (res.status >= 300 && res.status < 400 && !got.location.empty())
                        {
                            redirect = true;
                            return false;
                        }
                        if (!accept(got, limit, fail))
                        {
                            refused_answer = true;
                            return false;
                        }
                        return !http_cancelled();
                    },
                    [&](const char* data, size_t n) {
                        last = clock_type::now();
                        if (http_cancelled()) return false;
                        if (last >= deadline)
                        {
                            late = true;
                            return false;
                        }
                        if (got.body.size() + n <= limit.bytes)
                        {
                            got.body.append(data, n);
                            return true;
                        }
                        if (limit.refuse_beyond)
                        {
                            too_large = true;
                        }
                        else
                        {
                            got.body.append(data, limit.bytes - got.body.size());
                            got.truncated = true;
                        }
                        return false;
                    });
            }
            catch (const connection_cancelled&)
            {
            }
            if (http_cancelled())
            {
                fail = cancelled_reply();
                return false;
            }
            if (redirect || got.truncated) return true;
            if (refused_answer) return false;
            if (too_large)
            {
                fail = limit.beyond;
                return false;
            }
            const auto now = clock_type::now();
            if (late || (!result && now + std::chrono::milliseconds(500) >= deadline))
            {
                fail = timed_out(host);
                return false;
            }
            if (result)
            {
                // Une réponse sans corps (204) n'atteint aucun gestionnaire : son statut et ses en-têtes viennent du
                // résultat.
                if (got.status == 0) read_head(*result, got);
                return true;
            }
            switch (result.error())
            {
            case httplib::Error::Connection:
                unreachable = true;
                fail = failure(502, "unreachable", host + " cannot be reached");
                break;
            case httplib::Error::ConnectionTimeout:
                unreachable = true;
                fail = failure(504, "timeout", host + " did not accept the connection within "
                    + std::to_string((connect_ms + 999) / 1000) + " s");
                break;
            case httplib::Error::SSLServerVerification:
                fail = failure(502, "tls_failed", "the certificate of " + host + " is not valid");
                break;
            case httplib::Error::SSLServerHostnameVerification:
                fail = failure(502, "tls_failed", "the certificate of " + host + " names another host");
                break;
            case httplib::Error::SSLConnection:
            case httplib::Error::SSLLoadingCerts:
                fail = failure(502, "tls_failed", "the TLS connection to " + host + " failed");
                break;
            case httplib::Error::ProxyConnection:
                fail = failure(502, "proxy_failed", "the proxy " + proxy.host + " did not open the connection to "
                    + host);
                break;
            default:
                if (now - last + std::chrono::milliseconds(500) >= std::chrono::seconds(read_seconds))
                    fail = failure(504, "timeout", host + " sent no data during " + std::to_string(read_seconds)
                        + " s");
                else
                    fail = failure(502, "unreachable", "the answer of " + host + " cannot be read ("
                        + lower(httplib::to_string(result.error())) + ")");
                break;
            }
            return false;
        }

        /*
            Envoie une requête GET pour a et lit sa réponse avant deadline.  Avec check, les
            adresses de l'hôte sont refusées sauf si readable_address() les accepte.  La
            connexion va aux adresses de l'hôte dans leur ordre, chacune épinglée, la suivante
            prise tant qu'une adresse est injoignable et qu'il reste du temps ; derrière un
            proxy, au proxy seul.  Le corps d'une redirection n'est pas lu ; toute autre réponse
            passe par accept().  Renvoie false avec la réplique de l'échec.
        */
        bool exchange(const web_address& a, const httplib::Headers& headers, bool check,
            clock_type::time_point deadline, const answer_check& accept, received& got, web_reply& fail)
        {
            if (http_cancelled())
            {
                fail = cancelled_reply();
                return false;
            }
            proxy_route proxy;
            const bool via_proxy = http_proxy_for(origin_of(a), proxy.host, proxy.port, proxy.user, proxy.password);
            std::vector<ip_address> addresses;
            if (a.literal)
            {
                addresses.push_back(a.ip);
            }
            else if (check || !via_proxy)
            {
                std::vector<std::string> found;
                std::string error;
                if (!http_resolve(a.host, found, error))
                {
                    fail = http_cancelled() ? cancelled_reply()
                        : failure(502, "unreachable", "the name " + a.host + " cannot be resolved");
                    return false;
                }
                for (const auto& text : found)
                {
                    ip_address ip;
                    if (parse_ip(text, ip)) addresses.push_back(ip);
                }
            }
            if (check)
                for (const auto& ip : addresses)
                    if (!readable_address(ip))
                    {
                        fail = refused(host_part(a));
                        return false;
                    }
            if (via_proxy)
            {
                // Le nom du proxy est résolu dans les limites de http_resolve() avant que le client le prenne.
                std::vector<std::string> found;
                std::string error;
                ip_address literal;
                if (!parse_ip(proxy.host, literal) && !http_resolve(proxy.host, found, error))
                {
                    fail = http_cancelled() ? cancelled_reply()
                        : failure(502, "proxy_failed", "the proxy " + proxy.host + " cannot be reached: " + error);
                    return false;
                }
                addresses.assign(1, ip_address());
            }
            if (addresses.empty())
            {
                fail = failure(502, "unreachable", "the name " + a.host + " has no address");
                return false;
            }
            for (size_t i = 0; i < addresses.size(); ++i)
            {
                bool unreachable = false;
                if (attempt(a, via_proxy ? nullptr : &addresses[i], proxy, headers, deadline, accept, got, fail,
                        unreachable))
                    return true;
                if (!unreachable || clock_type::now() >= deadline) return false;
            }
            return false;
        }

        // Lit une page pour web_fetch(), en suivant ses redirections.
        web_reply fetch(const std::string& url, web_address a, const std::string& language)
        {
            request_place place;
            if (!place.held()) return busy_reply();
            const auto deadline = clock_type::now() + std::chrono::seconds(total_seconds);
            httplib::Headers headers;
            headers.emplace("User-Agent", user_agent());
            headers.emplace("Accept", "text/html, application/xhtml+xml, application/xml;q=0.9, text/*;q=0.8, "
                "application/pdf;q=0.8, application/json;q=0.8, */*;q=0.1");
            // httplib ne décompresse aucune réponse ici : les corps arrivent tels quels.
            headers.emplace("Accept-Encoding", "identity");
            if (!language.empty()) headers.emplace("Accept-Language", language);
            // Un texte est coupé à max_text_bytes, un fichier PDF refusé au-delà de max_pdf_bytes, tout autre type
            // refusé.
            const answer_check accept = [&a](const received& got, body_limit& limit, web_reply& refusal) {
                const std::string host = host_part(a);
                const web_reply too_large = failure(413, "too_large", "the PDF file of " + host + " is larger than "
                    + std::to_string(max_pdf_bytes >> 20) + " MB, which the web access of Albireo does not read");
                if (!readable_type(got.content_type))
                {
                    refusal = failure(415, "unsupported_type", (got.content_type.empty()
                        ? "the page of " + host + " gives no type, and the web access of Albireo reads"
                        : "the page of " + host + " has the type " + got.content_type + ", while the web access of "
                            "Albireo reads") + " text, HTML, XML, JSON and PDF only");
                    return false;
                }
                const bool pdf = got.content_type == "application/pdf";
                if (pdf && got.content_length > static_cast<long long>(max_pdf_bytes))
                {
                    refusal = too_large;
                    return false;
                }
                limit.bytes = pdf ? max_pdf_bytes : max_text_bytes;
                limit.refuse_beyond = pdf;
                limit.beyond = too_large;
                return true;
            };
            received got;
            web_reply fail;
            for (int redirections = 0;; ++redirections)
            {
                if (!a.literal && local_name(a.host)) return refused(a.host);
                if (!exchange(a, headers, true, deadline, accept, got, fail)) return fail;
                if (got.location.empty()) break;
                if (redirections == max_redirections)
                    return failure(502, "too_many_redirects", host_part(a) + " redirects more than "
                        + std::to_string(max_redirections) + " times");
                web_address next;
                std::string why;
                if (!parse_address(resolve_location(a, got.location), next, why))
                    return failure(502, "invalid_redirect", host_part(a) + " redirects to an address that the web "
                        "access of Albireo does not read (" + why + ")");
                a = next;
            }
            json body = json::object();
            body["url"] = url;
            body["final_url"] = written_origin(a) + a.target;
            body["status"] = got.status;
            body["content_type"] = got.content_type;
            body["charset"] = got.charset;
            body["size"] = static_cast<unsigned long long>(got.body.size());
            body["truncated"] = got.truncated;
            body["data"] = base64_encode(got.body);
            log_info("web: " + host_part(a) + " read (" + std::to_string(got.status) + ", "
                + std::to_string(got.body.size()) + " bytes)");
            web_reply reply;
            reply.body = std::move(body);
            return reply;
        }

        // Le texte recherché : 1 à 400 caractères, ses espaces aux deux bouts retirés.
        bool read_query(const json& request, std::string& query, web_reply& refusal)
        {
            query = trim(request["query"].as_string());
            size_t characters = 0;
            for (const char c : query)
                if ((static_cast<unsigned char>(c) & 0xc0) != 0x80) ++characters;
            if (request["query"].is_string() && characters >= 1 && characters <= 400) return true;
            refusal = failure(400, "invalid_query", "query must hold 1 to 400 characters", "query");
            return false;
        }

        // Le nombre de résultats : 1 à 10, 5 quand la requête n'en donne pas.
        bool read_count(const json& request, int& count, web_reply& refusal)
        {
            const json& value = request["count"];
            count = 5;
            if (value.is_null()) return true;
            if (value.is_integer() && value.as_int() >= 1 && value.as_int() <= 10)
            {
                count = static_cast<int>(value.as_int());
                return true;
            }
            refusal = failure(400, "invalid_count", "count must be an integer from 1 to 10", "count");
            return false;
        }

        // La réplique à une réponse de Wikipédia dont le statut n'est pas 200.
        web_reply provider_refusal(const received& got)
        {
            if (got.status == 429)
            {
                web_reply r = failure(429, "rate_limited",
                    "Wikipedia limits the rate of the anonymous requests: wait before searching again");
                r.retry_after = got.retry_after;
                return r;
            }
            return failure(502, "search_failed", "Wikipedia answers with the status " + std::to_string(got.status));
        }

        // Recherche query par la recherche de Wikipédia pour web_search().
        web_reply search(const std::string& query, int count, const std::string& language)
        {
            request_place place;
            if (!place.held()) return busy_reply();
            std::string origin;
            {
                std::lock_guard<std::mutex> lock(provider_m);
                origin = provider_origin;
            }
            httplib::Headers headers;
            headers.emplace("Accept", "application/json");
            headers.emplace("Accept-Encoding", "identity");
            headers.emplace("User-Agent", wikimedia_user_agent());
            // La langue de Wikipédia, qui nomme son site : la langue de la requête sans sa région.
            const std::string wiki = language.empty() ? std::string("en") : language.substr(0, language.find('-'));
            const std::string url = (origin.empty() ? "https://" + wiki + ".wikipedia.org" : origin)
                + "/w/rest.php/v1/search/page?q=" + query_component(query) + "&limit=" + std::to_string(count);
            web_address a;
            std::string why;
            if (!parse_address(url, a, why))
                return failure(502, "search_failed", "the address of Wikipedia is invalid");
            const answer_check accept = [](const received&, body_limit& limit, web_reply&) {
                limit.bytes = max_answer_bytes;
                limit.refuse_beyond = true;
                limit.beyond = failure(502, "search_failed", "the answer of Wikipedia is larger than "
                    + std::to_string(max_answer_bytes >> 20) + " MB");
                return true;
            };
            received got;
            web_reply fail;
            const auto deadline = clock_type::now() + std::chrono::seconds(total_seconds);
            if (!exchange(a, headers, false, deadline, accept, got, fail)) return fail;
            if (got.status != 200) return provider_refusal(got);
            json document;
            json results = json::array();
            auto add = [&results, count](const std::string& title, const std::string& address,
                const std::string& text) {
                const std::string scheme = lower(address.substr(0, 8));
                if (results.size() >= static_cast<size_t>(count)
                    || (scheme.compare(0, 7, "http://") != 0 && scheme != "https://"))
                    return;
                json r = json::object();
                r["title"] = title;
                r["url"] = address;
                r["snippet"] = text;
                results.push_back(r);
            };
            const json& d = document;
            const bool valid = json::parse(got.body, document) && d["pages"].is_array();
            if (!valid)
                return failure(502, "search_failed", "the answer of Wikipedia is not the JSON of its interface");
            for (const auto& page : d["pages"].elements())
            {
                const std::string key = page["key"].as_string();
                if (key.empty()) continue;
                std::string snippet = plain_text(page["excerpt"].as_string());
                if (snippet.empty()) snippet = tidy(page["description"].as_string());
                add(tidy(page["title"].as_string()), "https://" + wiki + ".wikipedia.org/wiki/" + article_path(key),
                    snippet);
            }
            json body = json::object();
            body["provider"] = "wikipedia";
            body["results"] = results;
            log_info("web: search through wikipedia (" + std::to_string(results.size()) + " results)");
            web_reply reply;
            reply.body = std::move(body);
            return reply;
        }
    }

    bool public_address(const std::string& numeric)
    {
        ip_address ip;
        if (!parse_ip(numeric, ip)) return false;
        return ip.family == 4 ? public_ipv4(ipv4_at(ip.bytes)) : public_ipv6(ip.bytes);
    }

    std::string web_search_provider(const host_settings& s)
    {
        return s.web_search == "none" ? "none" : "wikipedia";
    }

    json web_capability(const host_settings& s)
    {
        const bool available = s.web_access && !s.offline;
        json web = json::object();
        web["available"] = available;
        web["search"] = available ? web_search_provider(s) : std::string("none");
        return web;
    }

    web_reply web_fetch(const json& request, const host_settings& s)
    {
        web_reply reply;
        std::string language, why;
        web_address a;
        if (!available(s, reply))
        {
        }
        else if (!request["url"].is_string())
        {
            reply = failure(400, "invalid_url", "url must be an http or https address", "url");
        }
        else if (read_language(request, language, reply))
        {
            if (!parse_address(request["url"].as_string(), a, why)) reply = failure(400, "invalid_url", why, "url");
            else reply = fetch(request["url"].as_string(), a, language);
        }
        if (reply.status != 200) log_warning("web: " + without_queries(reply.message));
        return reply;
    }

    web_reply web_search(const json& request, const host_settings& s)
    {
        web_reply reply;
        std::string query, language;
        int count = 5;
        if (available(s, reply) && read_query(request, query, reply) && read_count(request, count, reply)
            && read_language(request, language, reply))
        {
            if (web_search_provider(s) == "wikipedia") reply = search(query, count, language);
            else reply = failure(403, "no_search_provider", "no search provider is set");
        }
        if (reply.status != 200) log_warning("web: " + without_queries(reply.message));
        return reply;
    }

    void web_allow_loopback_for_tests(bool allow)
    {
        loopback_allowed = allow;
    }

    void web_provider_origin_for_tests(const std::string& origin)
    {
        std::lock_guard<std::mutex> lock(provider_m);
        provider_origin = origin;
    }

}
