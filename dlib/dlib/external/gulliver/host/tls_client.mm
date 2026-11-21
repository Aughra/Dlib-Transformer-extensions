// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// Le pont TLS des connexions sortantes, par Network.framework (fork pour macOS).  Compilé avec le
// comptage automatique des références : les objets de Network et de dispatch sont libérés par ARC.

#import <Foundation/Foundation.h>
#include <Network/Network.h>
#include <Security/Security.h>

#include <httplib.h>

#include "tls_client.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "crypto.h"
#include "tls_gate.h"
#include "../common/platform.h"

namespace gulliver
{
    /*
        Les fonctions qui créent des blocs prennent leur shared_ptr par valeur : un bloc garde une
        référence C++ telle quelle, et non l'objet qu'elle désigne ; une référence à un shared_ptr
        tenu par un autre bloc, libéré entre-temps, pointerait dans le vide.
    */

    namespace
    {
        const size_t chunk_bytes = 64 * 1024;
        const size_t max_header_bytes = 64 * 1024;

        enum class failure { none, connection, handshake, certificate, hostname };

        bool write_all(int fd, dispatch_data_t data)
        {
            __block bool ok = true;
            dispatch_data_apply(data, ^bool(dispatch_data_t, size_t, const void* buffer, size_t size) {
                const char* p = static_cast<const char*>(buffer);
                while (size > 0)
                {
                    const ssize_t n = write(fd, p, size);
                    if (n > 0) { p += n; size -= static_cast<size_t>(n); continue; }
                    if (n < 0 && (errno == EAGAIN || errno == EINTR))
                    {
                        pollfd w = { fd, POLLOUT, 0 };
                        poll(&w, 1, 1000);
                        continue;
                    }
                    ok = false;
                    return false;
                }
                return true;
            });
            return ok;
        }

        // Les certificats d'un fichier PEM, pour les approuver seuls ; nil quand il n'en contient aucun.
        CFArrayRef read_anchors(const std::string& path)
        {
            std::string pem;
            if (!read_file(path, pem)) return nullptr;
            CFMutableArrayRef out = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
            const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
            for (size_t a = pem.find(begin); a != std::string::npos; a = pem.find(begin, a + 1))
            {
                const size_t b = pem.find(end, a);
                if (b == std::string::npos) break;
                const std::string der = base64_decode(pem.substr(a + begin.size(), b - a - begin.size()));
                CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(der.data()),
                    static_cast<CFIndex>(der.size()));
                SecCertificateRef cert = data ? SecCertificateCreateWithData(nullptr, data) : nullptr;
                if (data) CFRelease(data);
                if (cert)
                {
                    CFArrayAppendValue(out, cert);
                    CFRelease(cert);
                }
            }
            if (CFArrayGetCount(out) == 0) { CFRelease(out); return nullptr; }
            return out;
        }

        // La requête avec route.host dans sa ligne Host (remplacée, ou ajoutée après la ligne de requête).
        std::string with_host(const std::string& head, const std::string& host_value)
        {
            const size_t first = head.find("\r\n");
            if (first == std::string::npos) return head;
            std::string out = head.substr(0, first + 2);
            bool done = false;
            size_t at = first + 2;
            while (at < head.size())
            {
                const size_t eol = head.find("\r\n", at);
                const size_t stop = eol == std::string::npos ? head.size() : eol + 2;
                const std::string line = head.substr(at, stop - at);
                if (!done && line.size() > 5 && strncasecmp(line.c_str(), "host:", 5) == 0)
                {
                    out += "Host: " + host_value + "\r\n";
                    done = true;
                }
                else if (!done && line == "\r\n")
                {
                    out += "Host: " + host_value + "\r\n\r\n";
                    done = true;
                }
                else out += line;
                at = stop;
            }
            return out;
        }
    }

    // Une connexion du client relayée : la prise Unix (fd) d'un côté, la connexion TLS de l'autre.
    struct bridge_link
    {
        nw_connection_t conn = nil;
        dispatch_queue_t q = nil;
        dispatch_queue_t wq = nil;
        dispatch_source_t source = nil;
        int fd = -1;
        std::string head;          // l'en-tête de la requête, lu avant d'ouvrir la connexion
        bool closed = false;
        bool suspended = false;
        bool handed_off = false;   // l'en-tête lu, la prise passe à upstream() : la source de l'en-tête ne la ferme plus
    };

    struct tls_bridge
    {
        tls_route route;
        CFArrayRef anchors = nullptr;
        std::string path;
        int listen_fd = -1;
        dispatch_queue_t q = nil;
        dispatch_source_t accept_source = nil;
        // La cause du dernier échec, partagée avec les blocs de vérification qui peuvent survivre au pont.
        std::shared_ptr<std::atomic<int>> last = std::make_shared<std::atomic<int>>(static_cast<int>(failure::none));
        std::mutex m;
        std::set<std::shared_ptr<bridge_link>> open;

        ~tls_bridge()
        {
            shut();
            if (anchors) CFRelease(anchors);
        }

        void shut()
        {
            if (accept_source)
            {
                dispatch_source_cancel(accept_source);
                accept_source = nil;
                dispatch_sync(q, ^{});   // un accept_one() en cours se termine avant la suite
            }
            std::set<std::shared_ptr<bridge_link>> links;
            {
                std::lock_guard<std::mutex> lock(m);
                links = open;
            }
            for (const auto& l : links)
                dispatch_sync(l->q, ^{ close(l); });
            if (!path.empty()) { remove_private_socket(path); path.clear(); }
        }

        void close(const std::shared_ptr<bridge_link>& l)
        {
            if (l->closed) return;
            l->closed = true;
            if (l->source)
            {
                if (l->suspended) { l->suspended = false; dispatch_resume(l->source); }
                dispatch_source_cancel(l->source);
            }
            else if (l->fd >= 0)
            {
                const int fd = l->fd;
                dispatch_async(l->wq, ^{ ::close(fd); });
            }
            if (l->conn) nw_connection_cancel(l->conn);
            std::lock_guard<std::mutex> lock(m);
            open.erase(l);
        }

        nw_parameters_t parameters()
        {
            const std::string host = route.host;
            // Un NSArray, que le bloc retient (ARC) même si le pont disparaît pendant une vérification.
            NSArray* pinned = (__bridge NSArray*)anchors;
            std::shared_ptr<std::atomic<int>> cause = last;
            const int connect_seconds = route.connect_seconds;
            dispatch_queue_t verify_queue = q;
            nw_parameters_t params = nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls) {
                sec_protocol_options_t options = nw_tls_copy_sec_protocol_options(tls);
                sec_protocol_options_set_tls_server_name(options, host.c_str());
                sec_protocol_options_set_min_tls_protocol_version(options, tls_protocol_version_TLSv12);
                sec_protocol_options_add_tls_application_protocol(options, "http/1.1");
                // La vérification du certificat : par le nom du serveur, contre les autorités de macOS
                // ou celles du fichier donné seules ; la cause d'un refus est retenue pour le message.
                sec_protocol_options_set_verify_block(options,
                    ^(sec_protocol_metadata_t, sec_trust_t trust_ref, sec_protocol_verify_complete_t complete) {
                        SecTrustRef trust = sec_trust_copy_ref(trust_ref);
                        CFStringRef name = CFStringCreateWithCString(nullptr, host.c_str(), kCFStringEncodingUTF8);
                        SecPolicyRef policy = SecPolicyCreateSSL(true, name);
                        SecTrustSetPolicies(trust, policy);
                        if (pinned)
                        {
                            SecTrustSetAnchorCertificates(trust, (__bridge CFArrayRef)pinned);
                            SecTrustSetAnchorCertificatesOnly(trust, true);
                        }
                        CFErrorRef error = nullptr;
                        const bool ok = SecTrustEvaluateWithError(trust, &error);
                        if (!ok)
                        {
                            const CFIndex code = error ? CFErrorGetCode(error) : 0;
                            *cause = static_cast<int>(code == errSecHostNameMismatch ? failure::hostname
                                : failure::certificate);
                        }
                        if (error) CFRelease(error);
                        CFRelease(policy);
                        CFRelease(name);
                        CFRelease(trust);
                        complete(ok);
                    }, verify_queue);
            }, ^(nw_protocol_options_t tcp) {
                nw_tcp_options_set_connection_timeout(tcp, static_cast<uint32_t>(connect_seconds));
            });
            if (!route.proxy_host.empty())
            {
                nw_endpoint_t proxy = nw_endpoint_create_host(route.proxy_host.c_str(),
                    std::to_string(route.proxy_port).c_str());
                nw_proxy_config_t config = nw_proxy_config_create_http_connect(proxy, nil);
                if (!route.proxy_user.empty())
                    nw_proxy_config_set_username_and_password(config, route.proxy_user.c_str(),
                        route.proxy_password.c_str());
                nw_privacy_context_t privacy = nw_privacy_context_create("gulliver-proxy");
                nw_privacy_context_add_proxy(privacy, config);
                nw_parameters_set_privacy_context(params, privacy);
            }
            return params;
        }

        // Du client vers le serveur : la suite de la requête (son corps), après son en-tête.
        void upstream(std::shared_ptr<bridge_link> l)
        {
            l->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>(l->fd), 0, l->q);
            const int fd = l->fd;
            dispatch_queue_t wq = l->wq;
            dispatch_source_set_cancel_handler(l->source, ^{ dispatch_async(wq, ^{ ::close(fd); }); });
            dispatch_source_set_event_handler(l->source, ^{
                if (l->closed) return;
                char buffer[chunk_bytes];
                const ssize_t n = read(l->fd, buffer, sizeof(buffer));
                if (n > 0)
                {
                    dispatch_data_t data = dispatch_data_create(buffer, static_cast<size_t>(n), nil,
                        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                    l->suspended = true;
                    dispatch_suspend(l->source);
                    nw_connection_send(l->conn, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, false, ^(nw_error_t error) {
                        if (l->closed) return;
                        if (error) { close(l); return; }
                        l->suspended = false;
                        dispatch_resume(l->source);
                    });
                    return;
                }
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
                // Le client a fini d'écrire : fin du flux vers le serveur, la réponse continue d'arriver.
                l->suspended = true;
                dispatch_suspend(l->source);
                nw_connection_send(l->conn, nil, NW_CONNECTION_FINAL_MESSAGE_CONTEXT, true, ^(nw_error_t) {});
            });
            dispatch_resume(l->source);
        }

        // Du serveur vers le client : une lecture TLS, son écriture dans fd, puis la suivante.
        void downstream(std::shared_ptr<bridge_link> l)
        {
            nw_connection_receive(l->conn, 1, static_cast<uint32_t>(chunk_bytes),
                ^(dispatch_data_t content, nw_content_context_t, bool is_complete, nw_error_t error) {
                    if (l->closed) return;
                    const bool done = is_complete || error != nil;
                    if (content)
                    {
                        dispatch_async(l->wq, ^{
                            const bool ok = write_all(l->fd, content);
                            dispatch_async(l->q, ^{
                                if (l->closed) return;
                                if (!ok || done) close(l);
                                else downstream(l);
                            });
                        });
                        return;
                    }
                    if (done)
                    {
                        dispatch_async(l->wq, ^{ dispatch_async(l->q, ^{ close(l); }); });
                        return;
                    }
                    downstream(l);
                });
        }

        // La connexion TLS vers le serveur, une fois l'en-tête de la requête lu.
        void connect(std::shared_ptr<bridge_link> l)
        {
            const std::string where = route.address.empty() ? route.host : route.address;
            nw_endpoint_t endpoint = nw_endpoint_create_host(where.c_str(), std::to_string(route.port).c_str());
            l->conn = nw_connection_create(endpoint, parameters());
            nw_connection_set_queue(l->conn, l->q);
            *last = static_cast<int>(failure::none);
            nw_connection_set_state_changed_handler(l->conn, ^(nw_connection_state_t state, nw_error_t error) {
                if (l->closed) return;
                if (state == nw_connection_state_ready)
                {
                    dispatch_data_t head = dispatch_data_create(l->head.data(), l->head.size(), nil,
                        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                    nw_connection_send(l->conn, head, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, false, ^(nw_error_t e) {
                        if (e && !l->closed) close(l);
                    });
                    upstream(l);
                    downstream(l);
                }
                else if (state == nw_connection_state_failed || state == nw_connection_state_waiting)
                {
                    // Une cause déjà retenue par la vérification du certificat reste la bonne.
                    if (*last == static_cast<int>(failure::none))
                    {
                        const bool tls_level = error && nw_error_get_error_domain(error) == nw_error_domain_tls;
                        *last = static_cast<int>(tls_level ? failure::handshake : failure::connection);
                    }
                    close(l);
                }
                else if (state == nw_connection_state_cancelled)
                {
                    close(l);
                }
            });
            nw_connection_start(l->conn);
        }

        // Lit l'en-tête de la requête du client, le corrige, puis ouvre la connexion.
        void read_head(std::shared_ptr<bridge_link> l)
        {
            l->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>(l->fd), 0, l->q);
            dispatch_source_t s = l->source;
            const int fd = l->fd;
            dispatch_queue_t wq = l->wq;
            /* Fermée pendant la lecture de l'en-tête, la prise se ferme avec la source ; passée à la suite du
               relais, elle reste ouverte.  Un drapeau, et non un autre gestionnaire posé au moment de
               l'annulation : celui qui était posé s'exécuterait quand même. */
            dispatch_source_set_cancel_handler(s, ^{
                if (!l->handed_off) dispatch_async(wq, ^{ ::close(fd); });
            });
            dispatch_source_set_event_handler(s, ^{
                if (l->closed) return;
                char buffer[4096];
                const ssize_t n = read(l->fd, buffer, sizeof(buffer));
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
                if (n <= 0) { close(l); return; }
                l->head.append(buffer, static_cast<size_t>(n));
                const size_t end = l->head.find("\r\n\r\n");
                if (end == std::string::npos)
                {
                    if (l->head.size() > max_header_bytes) close(l);
                    return;
                }
                // L'en-tête complet : la source de lecture s'arrête, la suite passera par upstream().
                l->handed_off = true;
                dispatch_source_cancel(s);
                l->source = nil;
                const std::string default_port = route.port == 443 ? "" : ":" + std::to_string(route.port);
                const std::string written = route.host.find(':') != std::string::npos ? "[" + route.host + "]" : route.host;
                l->head = with_host(l->head.substr(0, end + 4), written + default_port) + l->head.substr(end + 4);
                connect(l);
            });
            dispatch_resume(s);
        }

        void accept_one()
        {
            const int fd = ::accept(listen_fd, nullptr, nullptr);
            if (fd < 0) return;
            const int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            auto l = std::make_shared<bridge_link>();
            l->fd = fd;
            l->q = dispatch_queue_create("gulliver.tls.client", DISPATCH_QUEUE_SERIAL);
            l->wq = dispatch_queue_create("gulliver.tls.client.write", DISPATCH_QUEUE_SERIAL);
            {
                std::lock_guard<std::mutex> lock(m);
                open.insert(l);
            }
            dispatch_async(l->q, ^{ read_head(l); });
        }

        bool start(std::string& error)
        {
            path = private_socket_path("client.sock");
            if (path.empty()) { error = "the private socket of the TLS client cannot be created"; return false; }
            listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
            if (listen_fd < 0 || bind(listen_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0
                || listen(listen_fd, 16) != 0)
            {
                if (listen_fd >= 0) ::close(listen_fd);
                listen_fd = -1;
                remove_private_socket(path);
                path.clear();
                error = "the private socket of the TLS client cannot be opened";
                return false;
            }
            fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL) | O_NONBLOCK);
            q = dispatch_queue_create("gulliver.tls.client.accept", DISPATCH_QUEUE_SERIAL);
            accept_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>(listen_fd), 0, q);
            tls_bridge* self = this;
            const int lfd = listen_fd;
            dispatch_source_set_event_handler(accept_source, ^{ self->accept_one(); });
            dispatch_source_set_cancel_handler(accept_source, ^{ ::close(lfd); });
            dispatch_resume(accept_source);
            return true;
        }
    };

    namespace
    {
        // Le pont de chaque client de https_client(), pour tls_error().
        std::mutex registry_m;
        std::map<const httplib::Client*, std::weak_ptr<tls_bridge>> registry;
    }

    std::shared_ptr<httplib::Client> https_client(const tls_route& route, std::string& error)
    {
        auto bridge = std::make_shared<tls_bridge>();
        bridge->route = route;
        if (!route.ca_path.empty())
        {
            bridge->anchors = read_anchors(route.ca_path);
            if (!bridge->anchors)
            {
                error = "the certificate authority " + route.ca_path + " cannot be read";
                return nullptr;
            }
        }
        if (!bridge->start(error)) return nullptr;
        auto* raw = new httplib::Client(bridge->path, 1);
        raw->set_address_family(AF_UNIX);
        {
            std::lock_guard<std::mutex> lock(registry_m);
            registry[raw] = bridge;
        }
        return std::shared_ptr<httplib::Client>(raw, [bridge](httplib::Client* c) {
            {
                std::lock_guard<std::mutex> lock(registry_m);
                registry.erase(c);
            }
            delete c;
            bridge->shut();
        });
    }

    httplib::Error tls_error(const httplib::Client& c, httplib::Error e)
    {
        if (e == httplib::Error::Success) return e;
        std::shared_ptr<tls_bridge> bridge;
        {
            std::lock_guard<std::mutex> lock(registry_m);
            const auto it = registry.find(&c);
            if (it != registry.end()) bridge = it->second.lock();
        }
        if (!bridge) return e;
        switch (static_cast<failure>(bridge->last->load()))
        {
        case failure::certificate: return httplib::Error::SSLServerVerification;
        case failure::hostname: return httplib::Error::SSLServerHostnameVerification;
        case failure::handshake: return httplib::Error::SSLConnection;
        case failure::connection: return httplib::Error::Connection;
        default: return e;
        }
    }

}
