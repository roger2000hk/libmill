/*
    Copyright (c) 2014 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "mill.h"

#include <stdio.h>

/******************************************************************************/
/* Generic stuff.                                                             */
/******************************************************************************/

void mill_coframe_head_init (
    struct mill_coframe_head *self,
    mill_handler_fn handler,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    self->handler = handler;
    self->state = 0;
    self->tag = tag;
    self->err = 0;
    self->parent = parent;
    self->next = 0;
    self->loop = loop;
}

void mill_coframe_head_term (
    struct mill_coframe_head *self)
{
    assert (0);
}

void mill_coframe_head_emit (
    struct mill_coframe_head *self,
    int err)
{
    self->err = err;
    mill_loop_emit (self->loop, self);
}

/******************************************************************************/
/* The event loop.                                                            */
/******************************************************************************/

static void loop_cb (uv_idle_t* handle)
{
    struct mill_loop *self;

    self = mill_cont (handle, struct mill_loop, idle);

    while (self->first) {
        if (!self->first->parent) {
            uv_stop (&self->uv_loop);
            return;
        }
        self->first->parent->handler (self->first->parent, self->first);
        self->first = self->first->next;
    }
    self->last = 0;
}

void mill_loop_init (
    struct mill_loop *self)
{
    int rc;

    rc = uv_loop_init (&self->uv_loop);
    assert (rc == 0);
    rc = uv_idle_init (&self->uv_loop, &self->idle);
    assert (rc == 0);
    rc = uv_idle_start (&self->idle, loop_cb);
    assert (rc == 0);
    self->first = 0;
    self->last = 0;
}

void mill_loop_term (
    struct mill_loop *self)
{
    assert (0);
}

void mill_loop_run (
    struct mill_loop *self)
{
    int rc;

    rc = uv_run (&self->uv_loop, UV_RUN_DEFAULT);
    assert (rc >= 0);
}

void mill_loop_emit (
    struct mill_loop *self,
    struct mill_coframe_head *ev)
{
    if (self->first == 0)
        self->first = ev;
    else
        self->last->next = ev;
    ev->next = 0;
    self->last = ev;
}

/******************************************************************************/
/* Alarm.                                                                     */
/******************************************************************************/

static void alarm_handler(
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_alarm *cf;

    cf = mill_cont (cfh, struct mill_coframe_alarm, mill_cfh);
    assert (0);
}

static void alarm_cb (
    uv_timer_t *timer)
{
    struct mill_coframe_alarm *cf;

    cf = mill_cont (timer, struct mill_coframe_alarm, timer);

    /* The coroutine is done. */
    mill_coframe_head_emit (&cf->mill_cfh, 0);
}

void mill_call_alarm (
    struct mill_coframe_alarm *cf,
    int milliseconds,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, alarm_handler, parent, loop, tag);

    uv_timer_init(&loop->uv_loop, &cf->timer);
    uv_timer_start(&cf->timer, alarm_cb, milliseconds, 0);
}

/******************************************************************************/
/* TCP socket.                                                                */
/******************************************************************************/

#define TCPSOCKET_STATE_INIT 1
#define TCPSOCKET_STATE_CONNECTING 2
#define TCPSOCKET_STATE_LISTENING 3
#define TCPSOCKET_STATE_ACCEPTING 4
#define TCPSOCKET_STATE_ACTIVE 5
#define TCPSOCKET_STATE_TERMINATING 6

int tcpsocket_init (
    struct tcpsocket *self,
    struct mill_loop *loop)
{
    self->loop = &loop->uv_loop;
    self->state = TCPSOCKET_STATE_INIT;
    self->recvop = 0;
    self->sendop = 0;
    return uv_tcp_init (&loop->uv_loop, &self->s);
}

static void tcpsocket_term_handler (
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_tcpsocket_term *cf;

    cf = mill_cont(cfh, struct mill_coframe_tcpsocket_term, mill_cfh);
    assert (0);
}

static void tcpsocket_term_cb (
    uv_handle_t* handle)
{
    struct tcpsocket *self = mill_cont (handle, struct tcpsocket, s);

    /* Double check that the socket is in the correct state. */
    assert (self->state == TCPSOCKET_STATE_TERMINATING);
    assert (self->recvop != 0);

    /* The coroutine is done. */
    mill_coframe_head_emit (self->recvop, 0);

    /* Flip the state. */
    self->state = 0;
    self->recvop = 0;
}

void mill_call_tcpsocket_term (
    struct mill_coframe_tcpsocket_term *cf,
    struct tcpsocket *self,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, tcpsocket_term_handler,
        parent, loop, tag);
    cf->self = self;

    /* Make sure that no async operations are going on on the socket. */
    assert (cf->self->state != TCPSOCKET_STATE_TERMINATING);
    assert (cf->self->recvop == 0);
    assert (cf->self->sendop == 0);

    /* Mark the socket as being in process of being terminated. */
    cf->self->state = TCPSOCKET_STATE_TERMINATING;
    cf->self->recvop = &cf->mill_cfh;

    /* Initiate the termination. */
    uv_close ((uv_handle_t*) &cf->self->s, tcpsocket_term_cb);
}

static void tcpsocket_connect_handler (
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_tcpsocket_connect *cf;

    cf = mill_cont (cfh, struct mill_coframe_tcpsocket_connect, mill_cfh);
    assert (0);
}

static void tcpsocket_connect_cb (
    uv_connect_t* req,
    int status)
{
    struct mill_coframe_tcpsocket_connect *cf;

    cf = mill_cont (req, struct mill_coframe_tcpsocket_connect, req);

    /* Double-check that the socket is in the correct state. */
    assert (cf->self->state == TCPSOCKET_STATE_CONNECTING);
    
    /* Flip the state. */
    cf->self->state = (status == 0) ?
        TCPSOCKET_STATE_ACTIVE :
        TCPSOCKET_STATE_INIT;
    cf->self->recvop = 0;

    /* The coroutine is done. */
    mill_coframe_head_emit (&cf->mill_cfh, status);
}

void mill_call_tcpsocket_connect (
    struct mill_coframe_tcpsocket_connect *cf,
    struct tcpsocket *self,
    struct sockaddr *addr,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    int rc;

    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, tcpsocket_connect_handler,
        parent, loop, tag);
    cf->self = self;

    /* Connect can be done only on a fresh socket. */
    assert (cf->self->state == TCPSOCKET_STATE_INIT);

    /* Mark the socket as being in process of connecting. */
    cf->self->state = TCPSOCKET_STATE_CONNECTING;
    cf->self->recvop = &cf->mill_cfh;
    
    /* Initiate the connecting. */
    rc = uv_tcp_connect (&cf->req, &cf->self->s, addr, tcpsocket_connect_cb);
    assert (rc == 0);
}



int tcpsocket_bind (
    struct tcpsocket *self,
    struct sockaddr *addr,
    int flags)
{
    /* Socket can be bound only before it is listening or connected. */
    assert (self->state == TCPSOCKET_STATE_INIT);

    return uv_tcp_bind (&self->s, addr, flags);
}

static void tcpsocket_listen_cb (
    uv_stream_t *s,
    int status)
{
    int rc;
    struct tcpsocket *self;
    struct mill_coframe_tcpsocket_accept *cf;
    uv_tcp_t uvs;

    self = mill_cont (s, struct tcpsocket, s);

    /* Double-check that the socket is in the correct state. */
    assert (self->state == TCPSOCKET_STATE_LISTENING ||
        self->state == TCPSOCKET_STATE_ACCEPTING);

    /* If nobody is accepting connections at the moment
       we'll simply drop them. */
    if (self->state != TCPSOCKET_STATE_ACCEPTING) {
        rc = uv_tcp_init (s->loop, &uvs);
        assert (rc == 0);
        rc = uv_accept (s, (uv_stream_t*) &uvs);
        assert (rc == 0);
        uv_close ((uv_handle_t*) &s, NULL); // TODO: This is an async op!

        /* The coroutine goes on. No need to notify the parent. */
        return; 
    }

    if (status != 0)
        goto error;

    /* Actual accept. */
    assert (self->recvop != 0);
    cf = mill_cont (self->recvop, struct mill_coframe_tcpsocket_accept,
        mill_cfh);
    status = uv_accept (s, (uv_stream_t*) &cf->newsock->s);
    if (status != 0)
        goto error;

    /* Flip both listening and accepted socket to new states. */
    cf->newsock->state = TCPSOCKET_STATE_ACTIVE;
    cf->self->state = TCPSOCKET_STATE_LISTENING;
    self->recvop = 0;

    /* The coroutine is done. */
    mill_coframe_head_emit (&cf->mill_cfh, 0);
    return;

error:

    /* End the coroutine. Report the error. */
    cf->newsock->state = TCPSOCKET_STATE_INIT;
    cf->self->state = TCPSOCKET_STATE_LISTENING;
    self->recvop = 0;
    mill_coframe_head_emit (&cf->mill_cfh, status);
}

int tcpsocket_listen (
    struct tcpsocket *self,
    int backlog,
    struct mill_loop *loop)
{
    int rc;

    /* Listen cannot be called on socket that is already listening or
       connected. */
    assert (self->state == TCPSOCKET_STATE_INIT);

    /* Start listening. */
    rc = uv_listen((uv_stream_t*) &self->s, backlog, tcpsocket_listen_cb);
    if (rc != 0)
        return rc;
    self->state = TCPSOCKET_STATE_LISTENING;
    return 0;
}

static void tcpsocket_accept_handler (
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_tcpsocket_accept *cf;

    cf = mill_cont (cfh, struct mill_coframe_tcpsocket_accept, mill_cfh);
    assert (0);
}

void mill_call_tcpsocket_accept (
    struct mill_coframe_tcpsocket_accept *cf,
    struct tcpsocket *self,
    struct tcpsocket *newsock,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, tcpsocket_accept_handler,
        parent, loop, tag);
    cf->self = self;
    cf->newsock = newsock;

    /* Only sockets that are already listening can accept new connections. */
    assert (cf->self->state == TCPSOCKET_STATE_LISTENING);

    /* Mark the socket as being in process of being accepted. */
    cf->self->state = TCPSOCKET_STATE_ACCEPTING;
    cf->self->recvop = &cf->mill_cfh;

    /* There's no need for any action here as callback for incoming connections
       was already registered in tcpsocket_listen function. */
}

static void tcpsocket_send_handler (
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_tcpsocket_send *cf;

    cf = mill_cont (cfh, struct mill_coframe_tcpsocket_send, mill_cfh);
    assert (0);
}

static void tcpsocket_send_cb (
    uv_write_t* req,
    int status)
{
    struct mill_coframe_tcpsocket_send *cf;

    cf = mill_cont (req, struct mill_coframe_tcpsocket_send, req);

    /* Notify the parent coroutine that the operation is finished. */
    cf->self->sendop = 0;
    mill_coframe_head_emit (&cf->mill_cfh, status);
}

void mill_call_tcpsocket_send (
    struct mill_coframe_tcpsocket_send *cf,
    struct tcpsocket *self,
    const void *buf,
    size_t len,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    int rc;

    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, tcpsocket_send_handler,
        parent, loop, tag);
    cf->self = self;

    /* Make sure that the socket is properly connected and that no other send
       operation is in progress. */
    assert (self->state == TCPSOCKET_STATE_ACTIVE);
    assert (self->sendop == 0);

    /* Mark the socket as being in process of sending. */
    cf->self->sendop = &cf->mill_cfh;

    /* Initiate the sending. */
    cf->buffer.base = (void*) buf;
    cf->buffer.len = len;
    rc = uv_write (&cf->req, (uv_stream_t*) &cf->self->s, &cf->buffer, 1,
        tcpsocket_send_cb);
    assert (rc == 0);
}

static void tcpsocket_recv_handler (
    struct mill_coframe_head *cfh,
    struct mill_coframe_head *event)
{
    struct mill_coframe_tcpsocket_recv *cf;

    cf = mill_cont (cfh, struct mill_coframe_tcpsocket_recv, mill_cfh);
    assert (0);
}

static void tcpsocket_alloc_cb (
    uv_handle_t* handle,
    size_t suggested_size,
    uv_buf_t* buf)
{
    struct mill_coframe_tcpsocket_recv *cf;
    struct tcpsocket *self;

    self = mill_cont (handle, struct tcpsocket, s);
    assert (self->recvop);
    cf = mill_cont (self->recvop, struct mill_coframe_tcpsocket_recv,
        mill_cfh);

    buf->base = cf->buf;
    buf->len = cf->len;
}

static void tcpsocket_recv_cb (
    uv_stream_t* stream,
    ssize_t nread,
    const uv_buf_t* buf)
{
    struct mill_coframe_tcpsocket_recv *cf;
    struct tcpsocket *self;

    self = mill_cont (stream, struct tcpsocket, s);
    assert (self->recvop);
    cf = mill_cont (self->recvop, struct mill_coframe_tcpsocket_recv,
        mill_cfh);

    /* Adjust the input buffer to not cover the data already received. */
    assert (buf->base == cf->buf);
    assert (nread <= cf->len);
    cf->buf = ((char*) cf->buf) + nread;
    cf->len -= nread;

    /* If there are no more data to be read, stop reading and notify the parent
       coroutine that the operation is finished. */
    if (!cf->len) {
        uv_read_stop (stream);
        self->recvop = 0;
        mill_coframe_head_emit (&cf->mill_cfh, 0);
    }
}

void mill_call_tcpsocket_recv (
    struct mill_coframe_tcpsocket_recv *cf,
    struct tcpsocket *self,
    void *buf,
    size_t len,
    struct mill_coframe_head *parent,
    struct mill_loop *loop,
    int tag)
{
    int rc;

    /* Fill in the coframe. */
    mill_coframe_head_init (&cf->mill_cfh, tcpsocket_send_handler,
        parent, loop, tag);
    cf->self = self;
    cf->buf = buf;
    cf->len = len;

    /* Make sure that the socket is properly connected and that no other recv
       operation is in progress. */
    assert (self->state == TCPSOCKET_STATE_ACTIVE);
    assert (self->recvop == 0);

    /* Mark the socket as being in process of receiving. */
    cf->self->recvop = &cf->mill_cfh;

    /* Initiate the receiving. */
    rc = uv_read_start ((uv_stream_t*) &cf->self->s,
        tcpsocket_alloc_cb, tcpsocket_recv_cb);
    assert (rc == 0);
}
