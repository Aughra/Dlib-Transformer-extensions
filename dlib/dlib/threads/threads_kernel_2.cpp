// Copyright (C) 2003  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_THREADS_KERNEL_2_CPp_
#define DLIB_THREADS_KERNEL_2_CPp_

#include "../platform.h"

#ifdef DLIB_POSIX

#include "threads_kernel_2.h"


namespace dlib
{
    namespace threads_kernel_shared_helpers
    {

    // -----------------------------------------------------------------------------------

        struct info
        {
            void* param;
            void (*funct)(void*);
        };

    // -----------------------------------------------------------------------------------

        void* thread_starter (
            void* param
        )
        {
            info* alloc_p = static_cast<info*>(param);
            info p = *alloc_p;
            delete alloc_p;

            // detach self
            pthread_detach(pthread_self());

            p.funct(p.param);
            return 0;
        }

    // -----------------------------------------------------------------------------------

        bool spawn_thread (
            void (*funct)(void*),
            void* param
        )
        {
            info* p;
            try { p = new info; }
            catch (...) { return false; }

            p->funct = funct;
            p->param = param;

            /* Une pile de 16 Mo (fork pour macOS, 2026-11-21) : macOS ne donne que 512 Ko à un
               fil secondaire, contre 8 Mo sous Linux.  La passe d'un réseau dnn descend couche
               par couche par des appels imbriqués, et celle de Gemma 3 4B (34 blocs) débordait
               dans le fil de dnn_trainer au premier pas d'entraînement.  La pile n'est que
               réservée : macOS ne fournit ses pages qu'à l'usage. */
            pthread_attr_t attr;
            if ( pthread_attr_init (&attr) )
            {
                delete p;
                return false;
            }
            pthread_attr_setstacksize (&attr, size_t(16) << 20);
            pthread_t thread_id;
            const int failed = pthread_create (&thread_id, &attr, thread_starter, p);
            pthread_attr_destroy (&attr);
            if ( failed )
            {
                delete p;
                return false;
            }
            return true;
        }

    // -----------------------------------------------------------------------------------

    }

}

#endif // DLIB_POSIX

#endif // DLIB_THREADS_KERNEL_2_CPp_

