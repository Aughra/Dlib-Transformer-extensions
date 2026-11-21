// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// Le portier TLS du serveur local, par Network.framework (fork pour macOS).  Compilé avec le comptage
// automatique des références : les objets de Network et de dispatch sont libérés par ARC.

#import <Foundation/Foundation.h>
#include <Network/Network.h>

#include "tls_gate.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../common/log.h"

namespace gulliver
{
    /*
        Les fonctions qui créent des blocs prennent leur shared_ptr par valeur : un bloc garde une
        référence C++ telle quelle, et non l'objet qu'elle désigne ; une référence à un shared_ptr
        tenu par un autre bloc, libéré entre-temps, pointerait dans le vide.
    */

    namespace
    {
        const int max_connections = 128;
        /*
            Le délai d'une poignée de main TLS.  Le serveur n'écoute que le Mac lui-même, où une poignée
            de main prend une ou deux millisecondes.  Network.framework laisse attendre une connexion
            qui envoie autre chose que du TLS (une requête HTTP en clair, par exemple) au lieu de la
            refuser, comme le faisait Mbed TLS : c'est ce délai qui la ferme.  Un « framer » qui lirait
            le premier octet sous TLS a été essayé (2026-11-20) : Network ne tient pas compte de son
            refus pendant la poignée de main.
        */
        const int handshake_seconds = 10;
        const size_t chunk_bytes = 64 * 1024;

        int connect_unix(const std::string& path)
        {
            sockaddr_un a{};
            if (path.size() >= sizeof(a.sun_path)) return -1;
            const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return -1;
            const int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
            a.sun_family = AF_UNIX;
            std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
            if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
            {
                close(fd);
                return -1;
            }
            // Non bloquante : les lectures suivent une source de dispatch, les écritures attendent par poll().
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            return fd;
        }

        // Écrit tout data dans fd, en attendant qu'il accepte la suite ; false quand fd est fermé.
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
    }

    /*
        Une connexion relayée : le TLS d'un côté (conn), la prise Unix du serveur de l'autre (fd).
        Les rappels de Network et la lecture de fd tournent sur q ; les écritures dans fd sur wq,
        pour qu'une écriture qui attend le serveur ne bloque pas la lecture de sa réponse.
    */
    struct gate_relay
    {
        nw_connection_t conn = nil;
        dispatch_queue_t q = nil;
        dispatch_queue_t wq = nil;
        dispatch_source_t source = nil;
        int fd = -1;
        bool ready = false;
        bool closed = false;
        bool suspended = false;
    };

    struct tls_gate::impl
    {
        sec_identity_t identity = nil;
        nw_listener_t listener = nil;
        dispatch_queue_t q = nil;
        std::string backend;
        std::atomic<int> count{ 0 };
        std::mutex m;
        std::set<std::shared_ptr<gate_relay>> open;

        void close(const std::shared_ptr<gate_relay>& r)
        {
            if (r->closed) return;
            r->closed = true;
            if (r->source)
            {
                if (r->suspended) { r->suspended = false; dispatch_resume(r->source); }
                dispatch_source_cancel(r->source);   // son gestionnaire d'annulation ferme fd
            }
            else if (r->fd >= 0)
            {
                const int fd = r->fd;
                dispatch_async(r->wq, ^{ ::close(fd); });
            }
            nw_connection_cancel(r->conn);
            --count;
            std::lock_guard<std::mutex> lock(m);
            open.erase(r);
        }

        // Du client vers le serveur : une lecture TLS, son écriture dans fd, puis la suivante.
        void upstream(std::shared_ptr<gate_relay> r)
        {
            nw_connection_receive(r->conn, 1, static_cast<uint32_t>(chunk_bytes),
                ^(dispatch_data_t content, nw_content_context_t, bool is_complete, nw_error_t error) {
                    if (r->closed) return;
                    const bool done = is_complete || error != nil;
                    if (content)
                    {
                        dispatch_async(r->wq, ^{
                            const bool ok = write_all(r->fd, content);
                            dispatch_async(r->q, ^{
                                if (r->closed) return;
                                if (!ok) { close(r); return; }
                                if (done) shutdown(r->fd, SHUT_WR);
                                else upstream(r);
                            });
                        });
                        return;
                    }
                    if (done)
                    {
                        dispatch_async(r->wq, ^{ dispatch_async(r->q, ^{ if (!r->closed) shutdown(r->fd, SHUT_WR); }); });
                        return;
                    }
                    upstream(r);
                });
        }

        // Du serveur vers le client : une lecture de fd, son envoi TLS, puis la suivante.
        void downstream(std::shared_ptr<gate_relay> r)
        {
            r->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, static_cast<uintptr_t>(r->fd), 0, r->q);
            const int fd = r->fd;
            dispatch_queue_t wq = r->wq;
            // La prise se ferme sur la file des écritures, après celles qui attendent : fermée plus tôt,
            // son numéro pourrait être redonné à un autre fichier pendant qu'une écriture s'en sert.
            dispatch_source_set_cancel_handler(r->source, ^{ dispatch_async(wq, ^{ ::close(fd); }); });
            dispatch_source_set_event_handler(r->source, ^{
                if (r->closed) return;
                char buffer[chunk_bytes];
                const ssize_t n = read(r->fd, buffer, sizeof(buffer));
                if (n > 0)
                {
                    dispatch_data_t data = dispatch_data_create(buffer, static_cast<size_t>(n), nil,
                        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                    r->suspended = true;
                    dispatch_suspend(r->source);
                    nw_connection_send(r->conn, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, false, ^(nw_error_t error) {
                        if (r->closed) return;
                        if (error) { close(r); return; }
                        r->suspended = false;
                        dispatch_resume(r->source);
                    });
                    return;
                }
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
                // Le serveur a fini (ou sa prise est fermée) : fin du flux TLS, puis fermeture.
                r->suspended = true;
                dispatch_suspend(r->source);
                nw_connection_send(r->conn, nil, NW_CONNECTION_FINAL_MESSAGE_CONTEXT, true, ^(nw_error_t) { close(r); });
            });
            dispatch_resume(r->source);
        }

        void accept(nw_connection_t c)
        {
            if (count.load() >= max_connections)
            {
                nw_connection_cancel(c);
                return;
            }
            ++count;
            auto r = std::make_shared<gate_relay>();
            r->conn = c;
            r->q = dispatch_queue_create("gulliver.tls.connection", DISPATCH_QUEUE_SERIAL);
            r->wq = dispatch_queue_create("gulliver.tls.write", DISPATCH_QUEUE_SERIAL);
            {
                std::lock_guard<std::mutex> lock(m);
                open.insert(r);
            }
            nw_connection_set_queue(c, r->q);
            const std::string path = backend;
            nw_connection_set_state_changed_handler(c, ^(nw_connection_state_t state, nw_error_t) {
                if (r->closed) return;
                if (state == nw_connection_state_ready)
                {
                    r->ready = true;
                    r->fd = connect_unix(path);
                    if (r->fd < 0) { close(r); return; }
                    downstream(r);
                    upstream(r);
                }
                else if (state == nw_connection_state_failed || state == nw_connection_state_cancelled)
                {
                    close(r);
                }
            });
            // Une poignée de main qui n'aboutit pas libère sa place.
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(handshake_seconds) * NSEC_PER_SEC), r->q, ^{
                if (!r->ready && !r->closed) close(r);
            });
            nw_connection_start(c);
        }
    };

    tls_gate::tls_gate(SecIdentityRef identity, CFArrayRef chain) : p_(new impl)
    {
        p_->identity = chain ? sec_identity_create_with_certificates(identity, chain) : sec_identity_create(identity);
        p_->q = dispatch_queue_create("gulliver.tls.listener", DISPATCH_QUEUE_SERIAL);
    }

    tls_gate::~tls_gate()
    {
        stop();
    }

    bool tls_gate::listen(const std::string& host, int port, const std::string& backend_path, std::string& error)
    {
        if (!p_->identity)
        {
            error = "the identity of the server cannot be used for TLS";
            return false;
        }
        p_->backend = backend_path;
        sec_identity_t identity = p_->identity;
        nw_parameters_t params = nw_parameters_create_secure_tcp(^(nw_protocol_options_t tls) {
            sec_protocol_options_t options = nw_tls_copy_sec_protocol_options(tls);
            sec_protocol_options_set_local_identity(options, identity);
            sec_protocol_options_set_min_tls_protocol_version(options, tls_protocol_version_TLSv12);
            sec_protocol_options_add_tls_application_protocol(options, "http/1.1");
        }, NW_PARAMETERS_DEFAULT_CONFIGURATION);
        nw_parameters_set_local_endpoint(params, nw_endpoint_create_host(host.c_str(), std::to_string(port).c_str()));
        nw_listener_t listener = nw_listener_create(params);
        if (!listener)
        {
            error = "the listener of " + host + ":" + std::to_string(port) + " cannot be created";
            return false;
        }
        nw_listener_set_queue(listener, p_->q);
        impl* self = p_.get();
        nw_listener_set_new_connection_handler(listener, ^(nw_connection_t c) { self->accept(c); });
        dispatch_semaphore_t settled = dispatch_semaphore_create(0);
        __block int outcome = 0;   // 1 : prêt ; -1 : échec
        __block int code = 0;
        nw_listener_set_state_changed_handler(listener, ^(nw_listener_state_t state, nw_error_t e) {
            if (outcome != 0) return;
            if (state == nw_listener_state_ready) outcome = 1;
            else if (state == nw_listener_state_failed || state == nw_listener_state_waiting)
            {
                outcome = -1;
                code = e ? nw_error_get_error_code(e) : 0;
            }
            else return;
            dispatch_semaphore_signal(settled);
        });
        nw_listener_start(listener);
        if (dispatch_semaphore_wait(settled, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0 || outcome != 1)
        {
            nw_listener_cancel(listener);
            error = "the port " + std::to_string(port) + " of " + host + " cannot be opened"
                + (code ? std::string(" (") + std::strerror(code) + ")" : std::string());
            return false;
        }
        p_->listener = listener;
        return true;
    }

    void tls_gate::stop()
    {
        if (p_->listener)
        {
            nw_listener_cancel(p_->listener);
            p_->listener = nil;
        }
        std::set<std::shared_ptr<gate_relay>> open;
        {
            std::lock_guard<std::mutex> lock(p_->m);
            open = p_->open;
        }
        impl* self = p_.get();
        for (const auto& r : open)
            dispatch_sync(r->q, ^{ self->close(r); });
    }

    std::string private_socket_path(const std::string& name)
    {
        char tmp[PATH_MAX];
        if (confstr(_CS_DARWIN_USER_TEMP_DIR, tmp, sizeof(tmp)) == 0) return std::string();
        std::string dir = std::string(tmp) + "gulliver-XXXXXX";
        if (!mkdtemp(&dir[0])) return std::string();
        chmod(dir.c_str(), 0700);
        const std::string path = dir + "/" + name;
        if (path.size() >= sizeof(sockaddr_un{}.sun_path))
        {
            rmdir(dir.c_str());
            return std::string();
        }
        return path;
    }

    void remove_private_socket(const std::string& path)
    {
        if (path.empty()) return;
        unlink(path.c_str());
        const size_t slash = path.rfind('/');
        if (slash != std::string::npos) rmdir(path.substr(0, slash).c_str());
    }

}
