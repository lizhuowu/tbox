/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2017, ruki All rights reserved.
 *
 * @author      ruki
 * @file        channel.h
 * @ingroup     coroutine
 *
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "channel"
#define TB_TRACE_MODULE_DEBUG           (0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "channel.h"
#include "coroutine.h"
#include "scheduler.h"
#include "impl/impl.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the coroutine channel type
typedef struct __tb_co_channel_t
{
    // the queue
    tb_circle_queue_ref_t           queue;

    // the send semaphore 
    tb_co_semaphore_ref_t           send;

    // the recv semaphore 
    tb_co_semaphore_ref_t           recv;

    // the waiting send coroutines 
    tb_single_list_entry_head_t     waiting_send;

    // the waiting recv coroutines 
    tb_single_list_entry_head_t     waiting_recv;

}tb_co_channel_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
static tb_void_t tb_co_channel_send_buffer(tb_co_channel_t* channel, tb_cpointer_t data)
{
    // check
    tb_assert_and_check_return(channel && channel->queue && channel->send && channel->recv);

    // done
    do
    {
        // put data into queue if be not full
        if (!tb_circle_queue_full(channel->queue))
        {
            // trace
            tb_trace_d("send[%p]: put data(%p)", tb_coroutine_self(), data);

            // put data
            tb_circle_queue_put(channel->queue, data);

            // notify to recv data
            tb_co_semaphore_post(channel->recv, 1);

            // send ok
            break;
        }
        // wait it if be full
        else
        {
            // trace
            tb_trace_d("send[%p]: wait(%lu) ..", tb_coroutine_self(), tb_co_semaphore_value(channel->send));

            // wait send
            tb_long_t ok = tb_co_semaphore_wait(channel->send, -1);
            tb_assert_and_check_return(ok > 0);
 
            // trace
            tb_trace_d("send[%p]: wait(%lu) ok", tb_coroutine_self(), tb_co_semaphore_value(channel->send));
        }

    } while (1);
 
    // trace
    tb_trace_d("send[%p]: ok", tb_coroutine_self());
}
static tb_pointer_t tb_co_channel_recv_buffer(tb_co_channel_t* channel)
{
    // check
    tb_assert_and_check_return_val(channel && channel->queue && channel->send && channel->recv, tb_null);

    // done
    tb_pointer_t data = tb_null;
    do
    {
        // recv data from channel if be not null
        if (!tb_circle_queue_null(channel->queue))
        {
            // get data
            data = tb_circle_queue_get(channel->queue);

            // pop data
            tb_circle_queue_pop(channel->queue);

            // trace
            tb_trace_d("recv[%p]: get data(%p)", tb_coroutine_self(), data);

            // notify to send data
            tb_co_semaphore_post(channel->send, 1);

            // recv ok
            break;
        }
        // wait it if be null
        else
        {
            // trace
            tb_trace_d("recv[%p]: wait(%p) ..", tb_coroutine_self(), tb_co_semaphore_value(channel->send));

            // wait recv
            tb_long_t ok = tb_co_semaphore_wait(channel->recv, -1);
            tb_assert_and_check_return_val(ok > 0, tb_null);

            // trace
            tb_trace_d("recv[%p]: wait(%p) ok", tb_coroutine_self(), tb_co_semaphore_value(channel->send));
        }

    } while (1);
 
    // trace
    tb_trace_d("recv[%p]: ok", tb_coroutine_self());

    // get data
    return data;
}
static tb_void_t tb_co_channel_send_buffer0(tb_co_channel_t* channel, tb_cpointer_t data)
{
    // check
    tb_assert_and_check_return(channel);

    // resume the first waiting recv coroutine 
    if (tb_single_list_entry_size(&channel->waiting_recv))
    {
        // get the next entry from head
        tb_single_list_entry_ref_t entry = tb_single_list_entry_head(&channel->waiting_recv);
        tb_assert(entry);

        // remove it from the waiting recv coroutines
        tb_single_list_entry_remove_head(&channel->waiting_recv);

        // get the waiting recv coroutine
        tb_coroutine_ref_t waiting = (tb_coroutine_ref_t)tb_single_list_entry(&channel->waiting_recv, entry);

        // resume this coroutine 
        tb_coroutine_resume(waiting, tb_null);
    }

    // get the running coroutine 
    tb_coroutine_t* running = (tb_coroutine_t*)tb_coroutine_self();
    tb_assert(running);

    // save this coroutine to the waiting send coroutines
    tb_single_list_entry_insert_tail(&channel->waiting_send, &running->rs.single_entry);

    // send data and wait it
    tb_coroutine_suspend(data);
}
static tb_pointer_t tb_co_channel_recv_buffer0(tb_co_channel_t* channel)
{
    // check
    tb_assert_and_check_return_val(channel, tb_null);

    // done
    tb_pointer_t data = tb_null;
    do
    {
        // resume the first waiting send coroutine and recv data
        if (tb_single_list_entry_size(&channel->waiting_send))
        {
            // get the next entry from head
            tb_single_list_entry_ref_t entry = tb_single_list_entry_head(&channel->waiting_send);
            tb_assert(entry);

            // remove it from the waiting send coroutines
            tb_single_list_entry_remove_head(&channel->waiting_send);

            // get the waiting send coroutine
            tb_coroutine_ref_t waiting = (tb_coroutine_ref_t)tb_single_list_entry(&channel->waiting_send, entry);

            // resume this coroutine and recv data
            data = tb_coroutine_resume(waiting, tb_null);

            // recv ok
            break;
        }
        // no data?
        else
        {
            // get the running coroutine 
            tb_coroutine_t* running = (tb_coroutine_t*)tb_coroutine_self();
            tb_assert(running);

            // save this coroutine to the waiting recv coroutines
            tb_single_list_entry_insert_tail(&channel->waiting_recv, &running->rs.single_entry);

            // wait data
            tb_coroutine_suspend(tb_null);
        }

    } while (1);

    // ok?
    return data;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
tb_co_channel_ref_t tb_co_channel_init(tb_size_t size)
{
    // done
    tb_bool_t           ok = tb_false;
    tb_co_channel_t*    channel = tb_null;
    do
    {
        // make channel
        channel = tb_malloc0_type(tb_co_channel_t);
        tb_assert_and_check_break(channel);

        // with buffer?
        if (size)
        {
            // init queue 
            channel->queue = tb_circle_queue_init(size, tb_element_ptr(tb_null, tb_null));
            tb_assert_and_check_break(channel->queue);

            // init send semaphore
            channel->send = tb_co_semaphore_init(0);
            tb_assert_and_check_break(channel->send);

            // init recv semaphore
            channel->recv = tb_co_semaphore_init(0);
            tb_assert_and_check_break(channel->recv);
        }
        // no buffer
        else
        {
            // init waiting send coroutines
            tb_single_list_entry_init(&channel->waiting_send, tb_coroutine_t, rs.single_entry, tb_null);

            // init waiting recv coroutines
            tb_single_list_entry_init(&channel->waiting_recv, tb_coroutine_t, rs.single_entry, tb_null);
        }

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // exit it
        if (channel) tb_co_channel_exit((tb_co_channel_ref_t)channel);
        channel = tb_null;
    }

    // ok?
    return (tb_co_channel_ref_t)channel;
}
tb_void_t tb_co_channel_exit(tb_co_channel_ref_t self)
{
    // check
    tb_co_channel_t* channel = (tb_co_channel_t*)self;
    tb_assert_and_check_return(channel);

    // exit queue
    if (channel->queue) tb_circle_queue_exit(channel->queue);
    channel->queue = tb_null;

    // exit send semaphore
    if (channel->send) tb_co_semaphore_exit(channel->send);
    channel->send = tb_null;

    // exit recv semaphore
    if (channel->recv) tb_co_semaphore_exit(channel->recv);
    channel->recv = tb_null;

    // check waiting coroutines
    tb_assert(!tb_single_list_entry_size(&channel->waiting_send));
    tb_assert(!tb_single_list_entry_size(&channel->waiting_recv));

    // exit waiting coroutines
    tb_single_list_entry_exit(&channel->waiting_send);
    tb_single_list_entry_exit(&channel->waiting_recv);

    // exit the channel
    tb_free(channel);
}
tb_void_t tb_co_channel_send(tb_co_channel_ref_t self, tb_cpointer_t data)
{
    // check
    tb_co_channel_t* channel = (tb_co_channel_t*)self;
    tb_assert_and_check_return(channel);

    // send it
    if (channel->queue) tb_co_channel_send_buffer(channel, data);
    else tb_co_channel_send_buffer0(channel, data);
}
tb_pointer_t tb_co_channel_recv(tb_co_channel_ref_t self)
{
    // check
    tb_co_channel_t* channel = (tb_co_channel_t*)self;
    tb_assert_and_check_return_val(channel, tb_null);

    // recv it
    return channel->queue? tb_co_channel_recv_buffer(channel) : tb_co_channel_recv_buffer0(channel);
}

