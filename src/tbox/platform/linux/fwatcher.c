/*!The Treasure Box Library
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2009-present, TBOOX Open Source Group.
 *
 * @author      ruki
 * @file        fwatcher.c
 * @ingroup     platform
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "../fwatcher.h"
#include "../socket.h"
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <unistd.h>
#if defined(TB_CONFIG_MODULE_HAVE_COROUTINE) \
    && !defined(TB_CONFIG_MICRO_ENABLE)
#   include "../../coroutine/coroutine.h"
#   include "../../coroutine/impl/impl.h"
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */
#define TB_FWATCHER_EVENT_SIZE      (sizeof(struct inotify_event))
#ifdef TB_CONFIG_SMALL
#   define TB_FWATCHER_BUFFER_SIZE  (4096 * (TB_FWATCHER_EVENT_SIZE + 16))
#else
#   define TB_FWATCHER_BUFFER_SIZE  (8192 * (TB_FWATCHER_EVENT_SIZE + 16))
#endif

// fd to fwatcher entry
#define tb_fd2entry(fd)              ((fd) >= 0? (tb_fwatcher_entry_ref_t)((tb_long_t)(fd) + 1) : tb_null)

// fwatcher entry to fd
#define tb_entry2fd(file)            (tb_int_t)((file)? (((tb_long_t)(file)) - 1) : -1)

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the fwatcher type
typedef struct __tb_fwatcher_t
{
    tb_int_t    fd;
    tb_byte_t   buffer[TB_FWATCHER_BUFFER_SIZE];

}tb_fwatcher_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * declaration
 */
__tb_extern_c_enter__
tb_long_t tb_socket_wait_impl(tb_socket_ref_t sock, tb_size_t events, tb_long_t timeout);
__tb_extern_c_leave__

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_fwatcher_ref_t tb_fwatcher_init()
{
    tb_bool_t ok = tb_false;
    tb_fwatcher_t* fwatcher = tb_null;
    do
    {
        fwatcher = tb_malloc0_type(tb_fwatcher_t);
        tb_assert_and_check_break(fwatcher);

        fwatcher->fd = inotify_init();
        tb_assert_and_check_break(fwatcher->fd >= 0);

        ok = tb_true;
    } while (0);

    if (!ok && fwatcher)
    {
        tb_fwatcher_exit((tb_fwatcher_ref_t)fwatcher);
        fwatcher = tb_null;
    }

    return (tb_fwatcher_ref_t)fwatcher;
}

tb_bool_t tb_fwatcher_exit(tb_fwatcher_ref_t self)
{
    tb_bool_t ok = tb_false;
    tb_fwatcher_t* fwatcher = (tb_fwatcher_t*)self;
    if (fwatcher)
    {
        if (fwatcher->fd >= 0 && close(fwatcher->fd) == 0)
        {
            fwatcher->fd = -1;
            tb_free(fwatcher);
            ok = tb_true;
        }
    }
    return ok;
}

tb_fwatcher_entry_ref_t tb_fwatcher_entry_add(tb_fwatcher_ref_t self, tb_char_t const* dir, tb_size_t events)
{
    tb_fwatcher_t* fwatcher = (tb_fwatcher_t*)self;
    tb_assert_and_check_return_val(fwatcher && fwatcher->fd >= 0 && dir && events, tb_null);

    tb_uint32_t mask = 0;
    if (events & TB_FWATCHER_EVENT_MODIFY) mask |= IN_MODIFY;
    if (events & TB_FWATCHER_EVENT_CREATE) mask |= IN_CREATE;
    if (events & TB_FWATCHER_EVENT_DELETE) mask |= IN_DELETE;
    tb_int_t wd = inotify_add_watch(fwatcher->fd, dir, mask);
    tb_assert_and_check_return_val(wd >= 0, tb_null);

    return tb_fd2entry(wd);
}

tb_bool_t tb_fwatcher_entry_remove(tb_fwatcher_ref_t self, tb_fwatcher_entry_ref_t entry)
{
    tb_fwatcher_t* fwatcher = (tb_fwatcher_t*)self;
    tb_assert_and_check_return_val(fwatcher && fwatcher->fd >= 0 && entry, tb_false);

    return inotify_rm_watch(fwatcher->fd, tb_entry2fd(entry)) == 0;
}

tb_long_t tb_fwatcher_entry_wait(tb_fwatcher_ref_t self, tb_fwatcher_entry_ref_t entry, tb_long_t timeout)
{
    // TODO
#if 0/*defined(TB_CONFIG_MODULE_HAVE_COROUTINE) \
       && !defined(TB_CONFIG_MICRO_ENABLE)*/
    // attempt to wait it in coroutine if timeout is non-zero
        if (timeout && tb_coroutine_self())
    {
        tb_poller_object_t object;
        object.type = TB_POLLER_OBJECT_PIPE;
        object.ref.pipe = file;
        return tb_coroutine_waitio(&object, TB_SOCKET_EVENT_RECV, timeout);
    }
#endif
    // we use poll/select to wait pipe/fd events
    return tb_socket_wait_impl((tb_socket_ref_t)entry, TB_SOCKET_EVENT_RECV, timeout);
}

tb_size_t tb_fwatcher_entry_events(tb_fwatcher_ref_t self, tb_fwatcher_entry_ref_t entry, tb_fwatcher_event_t* events, tb_size_t events_maxn)
{
    tb_fwatcher_t* fwatcher = (tb_fwatcher_t*)self;
    tb_assert_and_check_return_val(fwatcher && fwatcher->fd >= 0 && entry && events && events_maxn, 0);

    tb_int_t real = read(tb_entry2fd(entry), fwatcher->buffer, sizeof(fwatcher->buffer));
    tb_check_return_val(real >= 0, -1);

    tb_int_t i = 0;
    tb_size_t events_count = 0;
    while (i < real && events_count < events_maxn)
    {
        struct inotify_event* event = (struct inotify_event*)&fwatcher->buffer[i];
        if (event->mask & IN_CREATE)
        {
            events[events_count].event = TB_FWATCHER_EVENT_CREATE;
            events[events_count].filepath = event->name;
            events_count++;
        }
        else if (event->mask & IN_DELETE)
        {
            events[events_count].event = TB_FWATCHER_EVENT_DELETE;
            events[events_count].filepath = event->name;
            events_count++;
        }
        else if (event->mask & IN_MODIFY)
        {
            events[events_count].event = TB_FWATCHER_EVENT_MODIFY;
            events[events_count].filepath = event->name;
            events_count++;
        }
        i += TB_FWATCHER_EVENT_SIZE + event->len;
    }
    return events_count;
}

