/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
 *
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <regex.h>

#include "ovis_util/os_util.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_private.h"

#ifdef ENABLE_AUTH
#include "ovis_auth/auth.h"
#endif /* ENABLE_AUTH */

/**
 * zap callback function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev);

/**
 * zap callback function for endpoints that automatically created from accepting
 * connection requests.
 */
static void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev);

static void default_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fflush(stdout);
}

#if 0
#define TF() default_log("%s:%d\n", __FUNCTION__, __LINE__)
#else
#define TF()
#endif

pthread_mutex_t xprt_list_lock;

ldms_t ldms_xprt_get(ldms_t x)
{
	assert(x->ref_count > 0);
	__sync_add_and_fetch(&x->ref_count, 1);
	return x;
}

LIST_HEAD(xprt_list, ldms_xprt) xprt_list;
ldms_t ldms_xprt_first()
{
	struct ldms_xprt *x = NULL;
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_FIRST(&xprt_list);
	if (!x)
		goto out;
	x = ldms_xprt_get(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

ldms_t ldms_xprt_next(ldms_t x)
{
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_NEXT(x, xprt_link);
	if (!x)
		goto out;
	x = ldms_xprt_get(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

ldms_t ldms_xprt_by_remote_sin(struct sockaddr_in *sin)
{
	struct sockaddr_storage ss_local, ss_remote;
	socklen_t socklen;

	ldms_t l;
	for (l = ldms_xprt_first(); l; l = ldms_xprt_next(l)) {
		int rc = zap_get_name(l->zap_ep,
				      (struct sockaddr *)&ss_local,
				      (struct sockaddr *)&ss_remote,
				      &socklen);
		if (rc)
			continue;
		struct sockaddr_in *s = (struct sockaddr_in *)&ss_remote;
		if (s->sin_addr.s_addr == sin->sin_addr.s_addr
		    && ((sin->sin_port == 0xffff) ||
			(s->sin_port == sin->sin_port)))
			return l;
		ldms_xprt_put(l);
	}
	return 0;
}

size_t __ldms_xprt_max_msg(struct ldms_xprt *x)
{
	return zap_max_msg(x->zap);
}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    const char *set_name)
{
	size_t len;
	int set_count;
	int set_list_sz;
	int rc = 0;
	struct ldms_reply *reply;

	switch (t) {
	case LDMS_DIR_LIST:
		__ldms_get_local_set_list_sz(&set_count, &set_list_sz);
		break;
	case LDMS_DIR_DEL:
	case LDMS_DIR_ADD:
		set_count = 1;
		set_list_sz = strlen(set_name) + 1;
		break;
	}

	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply)
		+ set_list_sz;

	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in dir update of peer.\n");
		return;
	}

	switch (t) {
	case LDMS_DIR_LIST:
		rc = __ldms_get_local_set_list(reply->dir.set_list,
					       set_list_sz,
					       &set_count, &set_list_sz);
		break;
	case LDMS_DIR_DEL:
	case LDMS_DIR_ADD:
		strcpy(reply->dir.set_list, set_name);
		break;
	}

	reply->hdr.xid = x->remote_dir_xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.type = htonl(t);
	reply->dir.set_count = htonl(set_count);
	reply->dir.set_list_len = htonl(set_list_sz);
	reply->hdr.len = htonl(len);

	zap_send(x->zap_ep, reply, len);
	free(reply);
	return;
}

static void send_req_notify_reply(struct ldms_xprt *x,
				  struct ldms_set *set,
				  uint64_t xid,
				  ldms_notify_event_t e)
{
	size_t len;
	int rc = 0;
	struct ldms_reply *reply;

	len = sizeof(struct ldms_reply_hdr) + e->len;
	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in notify of peer.\n");
		return;
	}
	reply->hdr.xid = xid;
	reply->hdr.cmd = htonl(LDMS_CMD_REQ_NOTIFY_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->hdr.len = htonl(len);
	if (e->len > sizeof(struct ldms_notify_event_s))
		memcpy(reply->req_notify.event.u_data, e,
		       e->len - sizeof(struct ldms_notify_event_s));

	zap_send(x->zap_ep, reply, len);
	free(reply);
	return;
}

static void dir_update(const char *set_name, enum ldms_dir_type t)
{
	struct ldms_xprt *x;
	for (x = (struct ldms_xprt *)ldms_xprt_first(); x;
	     x = (struct ldms_xprt *)ldms_xprt_next(x)) {
		if (x->remote_dir_xid)
			send_dir_update(x, t, set_name);
		ldms_xprt_put(x);
	}
}

void __ldms_dir_add_set(const char *set_name)
{
	dir_update(set_name, LDMS_DIR_ADD);
}

void __ldms_dir_del_set(const char *set_name)
{
	dir_update(set_name, LDMS_DIR_DEL);
}

void ldms_xprt_close(ldms_t x)
{
	x->remote_dir_xid = x->local_dir_xid = 0;
	zap_close(x->zap_ep);
}

void __ldms_xprt_resource_free(struct ldms_xprt *x)
{
	x->remote_dir_xid = x->local_dir_xid = 0;

#ifdef DEBUG
		x->log("DEBUG: xprt_resource_free. zap %p: active_dir = %d.\n",
			x->zap_ep, x->active_dir);
		x->log("DEBUG: xprt_resource_free. zap %p: active_lookup = %d.\n",
			x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	while (x->active_dir > 0) {
		x->active_dir--;
		zap_put_ep(x->zap_ep);
	}

	while (x->active_lookup > 0) {
		x->active_lookup--;
		zap_put_ep(x->zap_ep);
	}

	while (!LIST_EMPTY(&x->rbd_list)) {
		struct ldms_rbuf_desc *rbd;
		rbd = LIST_FIRST(&x->rbd_list);
		__ldms_free_rbd(rbd);
	}
}

void ldms_xprt_put(ldms_t x)
{
	assert(x->ref_count);
	if (0 == __sync_sub_and_fetch(&x->ref_count, 1)) {
		pthread_mutex_lock(&xprt_list_lock);
		LIST_REMOVE(x, xprt_link);
		pthread_mutex_unlock(&xprt_list_lock);

		__ldms_xprt_resource_free(x);

		if (x->zap_ep)
			zap_free(x->zap_ep);
		sem_destroy(&x->sem);
		free(x);
	}
}

struct make_dir_arg {
	int reply_size;		/* size of reply in total */
	struct ldms_reply *reply;
	struct ldms_xprt *x;
	int reply_count;	/* sets in this reply */
	int set_count;		/* total sets we have */
	char *set_list;		/* buffer for set names */
	ssize_t set_list_len;	/* current length of this buffer */
};

static int send_dir_reply_cb(struct ldms_set *set, void *arg)
{
	struct make_dir_arg *mda = arg;
	int len;

	len = strlen(get_instance_name(set->meta)->name) + 1;
	if (mda->reply_size + len < __ldms_xprt_max_msg(mda->x)) {
		mda->reply_size += len;
		strcpy(mda->set_list, get_instance_name(set->meta)->name);
		mda->set_list += len;
		mda->set_list_len += len;
		mda->reply_count ++;
		if (mda->reply_count < mda->set_count)
			return 0;
	}

	/* Update remaining set count */
	mda->set_count -= mda->reply_count;

	mda->reply->dir.more = htonl(mda->set_count != 0);
	mda->reply->dir.set_count = htonl(mda->reply_count);
	mda->reply->dir.set_list_len = htonl(mda->set_list_len);
	mda->reply->hdr.len = htonl(mda->reply_size);

	zap_send(mda->x->zap_ep, mda->reply, mda->reply_size);

	/* All sets are sent. */
	if (mda->set_count == 0)
		return 0;

	/* Change the dir type to ADD for the subsequent sends */
	mda->reply->dir.type = htonl(LDMS_DIR_ADD);

	/* Initialize arg for remainder of walk */
	mda->reply_size = sizeof(struct ldms_reply_hdr) +
		sizeof(struct ldms_dir_reply) +
		len;
	strcpy(mda->reply->dir.set_list, get_instance_name(set->meta)->name);
	mda->set_list = mda->reply->dir.set_list + len;
	mda->set_list_len = len;
	mda->reply_count = 1;
	return 0;
}

static void process_dir_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct make_dir_arg arg;
	size_t len;
	int set_count;
	int set_list_sz;
	int rc;
	struct ldms_reply reply_;
	struct ldms_reply *reply = &reply_;

	if (req->dir.flags)
		/* Register for directory updates */
		x->remote_dir_xid = req->hdr.xid;
	else
		/* Cancel any previous dir update */
		x->remote_dir_xid = 0;

	__ldms_get_local_set_list_sz(&set_count, &set_list_sz);
	if (!set_count) {
		rc = 0;
		goto out;
	}

	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply)
		+ set_list_sz;
	if (len > __ldms_xprt_max_msg(x))
		len = __ldms_xprt_max_msg(x);
	reply = malloc(len);
	if (!reply) {
		rc = ENOMEM;
		reply = &reply_;
		len = sizeof(struct ldms_reply_hdr);
		goto out;
	}

	/* Initialize the set_list walking callback argument */
	arg.reply_size = sizeof(struct ldms_reply_hdr) +
		sizeof(struct ldms_dir_reply);
	arg.reply = reply;
	memset(reply, 0, arg.reply_size);
	arg.x = x;
	arg.reply_count = 0;
	arg.set_list = reply->dir.set_list;
	arg.set_list_len = 0;
	arg.set_count = set_count;

	/* Initialize the reply header */
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->dir.type = htonl(LDMS_DIR_LIST);
	(void)__ldms_for_all_sets(send_dir_reply_cb, &arg);
	free(reply);
	return;
 out:
	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply);
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.more = 0;
	reply->dir.type = htonl(LDMS_DIR_LIST);
	reply->dir.set_count = 0;
	reply->dir.set_list_len = 0;
	reply->hdr.len = htonl(len);

	zap_send(x->zap_ep, reply, len);
	return;
}

static void
process_dir_cancel_request(struct ldms_xprt *x, struct ldms_request *req)
{
	x->remote_dir_xid = 0;
}

static void
process_req_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{

	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)req->req_notify.set_id;

	r->remote_notify_xid = req->hdr.xid;
	r->notify_flags = ntohl(req->req_notify.flags);
}

static void
process_cancel_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)req->cancel_notify.set_id;
	r->remote_notify_xid = 0;
}

static int __send_lookup_reply(struct ldms_xprt *x, struct ldms_set *set,
			       uint64_t xid, int more)
{
	struct ldms_reply_hdr hdr;
	struct ldms_rbuf_desc *rbd;
	int rc = ENOENT;
	if (!set)
		goto err_0;
	rbd = ldms_lookup_rbd(x, set);
	if (!rbd) {
		rc = ENOMEM;
		rbd = ldms_alloc_rbd(x, set);
		if (!rbd)
			goto err_0;
	}
	ldms_name_t name = get_instance_name(set->meta);
	size_t msg_len = sizeof(struct ldms_lookup_msg) + name->len;
	struct ldms_lookup_msg *msg = malloc(msg_len);
	if (!msg)
		goto err_0;

	strcpy(msg->inst_name, name->name);
	msg->inst_name_len = name->len;
	msg->xid = xid;
	msg->more = htonl(more);
	msg->data_len = htonl(__le32_to_cpu(set->meta->data_sz));
	msg->meta_len = htonl(__le32_to_cpu(set->meta->meta_sz));
	msg->card = htonl(__le32_to_cpu(set->meta->card));

	zap_share(x->zap_ep, rbd->lmap, (const char *)msg, msg_len);
	free(msg);
	return 0;
 err_0:
	hdr.rc = htonl(rc);
	hdr.xid = xid;
	hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	zap_send(x->zap_ep, &hdr, sizeof(hdr));
	return 1;
}

static int __re_match(struct ldms_set *set, regex_t *regex, const char *regex_str, int flags)
{
	regmatch_t regmatch;
	ldms_name_t name;
	int rc;

	if (flags & LDMS_LOOKUP_BY_SCHEMA)
		name = get_schema_name(set->meta);
	else
		name = get_instance_name(set->meta);

	if (flags & LDMS_LOOKUP_RE)
		rc = regexec(regex, name->name, 0, NULL, 0);
	else
		rc = strcmp(regex_str, name->name);

	return (rc == 0);
}

static struct ldms_set *__next_re_match(struct ldms_set *set,
					regex_t *regex, const char *regex_str, int flags)
{
	for (; set; set = __ldms_local_set_next(set)) {
		if (__re_match(set, regex, regex_str, flags))
			break;
	}
	return set;
}

static void process_lookup_request_re(struct ldms_xprt *x, struct ldms_request *req, uint32_t flags)
{
	regex_t regex;
	struct ldms_reply_hdr hdr;
	struct ldms_set *set, *nxt_set;
	int rc, more;

	if (flags & LDMS_LOOKUP_RE) {
		rc = regcomp(&regex, req->lookup.path, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			char errstr[512];
			size_t sz = regerror(rc, &regex, errstr, sizeof(errstr));
			x->log(errstr);
			rc = EINVAL;
			goto err_0;
		}
	}

	/* Get the first match */
	set = __ldms_local_set_first();
	set = __next_re_match(set, &regex, req->lookup.path, flags);
	if (!set) {
		rc = ENOENT;
		goto err_1;
	}
	while (set) {
		/* Get the next match if any */
		nxt_set = __next_re_match(__ldms_local_set_next(set),
					  &regex, req->lookup.path, flags);
		if (nxt_set)
			more = 1;
		else
			more = 0;
		rc = __send_lookup_reply(x, set, req->hdr.xid, more);
		set = nxt_set;
	}
	if (flags & LDMS_LOOKUP_RE)
		regfree(&regex);
	return;
 err_1:
	if (flags & LDMS_LOOKUP_RE)
		regfree(&regex);
 err_0:
	hdr.rc = htonl(rc);
	hdr.xid = req->hdr.xid;
	hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	zap_send(x->zap_ep, &hdr, sizeof(hdr));
}

/**
 * This function processes the lookup request from another peer.
 *
 * In the case of lookup OK, do ::zap_share().
 * In the case of lookup error, reply lookup error message.
 */
static void process_lookup_request(struct ldms_xprt *x, struct ldms_request *req)
{
	uint32_t flags = ntohl(req->lookup.flags);
	int rc;
	struct ldms_set *set;

	process_lookup_request_re(x, req, flags);
}

static int do_read_all(ldms_t t, ldms_set_t s, size_t len,
			ldms_update_cb_t cb, void *arg)
{
	struct ldms_set_desc *sd = s;

	if (!len)
		len = __ldms_set_size_get(s->set);
	struct ldms_xprt *x = t;
	struct ldms_context *ctxt = malloc(sizeof *ctxt);
	TF();

	ctxt->type = LDMS_CONTEXT_UPDATE;
	ctxt->rc = 0;
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;

	zap_map_t rmap = sd->rbd->rmap;
	zap_map_t lmap = sd->rbd->lmap;

	return zap_read(x->zap_ep, rmap, zap_map_addr(rmap),
			lmap, zap_map_addr(lmap),
			len, ctxt);
}

static int do_read_data(ldms_t t, ldms_set_t s, size_t len, ldms_update_cb_t cb, void*arg)
{
	struct ldms_xprt *x = t;
	struct ldms_set_desc *sd = s;
	struct ldms_context *ctxt = malloc(sizeof *ctxt);
	zap_map_t rmap = sd->rbd->rmap;
	zap_map_t lmap = sd->rbd->lmap;
	TF();
	ctxt->type = LDMS_CONTEXT_UPDATE;
	ctxt->rc = 0;
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;
	size_t doff = (void*)sd->set->data - (void*)sd->set->meta;

	return zap_read(x->zap_ep, rmap, zap_map_addr(rmap) + doff,
			lmap, zap_map_addr(lmap) + doff, len, ctxt);
}

/*
 * The meta data and the data are updated separately. The assumption
 * is that the meta data rarely (if ever) changes. The GN (generation
 * number) of the meta data is checked. If it is zero, then the meta
 * data has never been updated and it is fetched. If it is non-zero,
 * then the data is fetched. The meta data GN from the data is checked
 * against the GN returned in the data. If it matches, we're done. If
 * they don't match, then the meta data is fetched and then the data
 * is fetched again.
 */
int __ldms_remote_update(ldms_t x, ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	int rc;

	uint32_t meta_meta_gn = __le32_to_cpu(set->meta->meta_gn);
	uint32_t data_meta_gn = __le32_to_cpu(set->data->meta_gn);
	uint32_t meta_meta_sz = __le32_to_cpu(set->meta->meta_sz);
	uint32_t meta_data_sz = __le32_to_cpu(set->meta->data_sz);

	zap_get_ep(x->zap_ep);	/* Released in handle_zap_read_complete() */
	if (meta_meta_gn == 0 || meta_meta_gn != data_meta_gn) {
		/* Update the metadata along with the data */
		rc = do_read_all(x, s, meta_meta_sz +
				 meta_data_sz, cb, arg);
	} else {
		rc = do_read_data(x, s, meta_data_sz, cb, arg);
	}
	if (rc)
		zap_put_ep(x->zap_ep);
	return rc;
}

static int ldms_xprt_recv_request(struct ldms_xprt *x, struct ldms_request *req)
{
	int cmd = ntohl(req->hdr.cmd);

	switch (cmd) {
	case LDMS_CMD_LOOKUP:
		process_lookup_request(x, req);
		break;
	case LDMS_CMD_DIR:
		process_dir_request(x, req);
		break;
	case LDMS_CMD_DIR_CANCEL:
		process_dir_cancel_request(x, req);
		break;
	case LDMS_CMD_REQ_NOTIFY:
		process_req_notify_request(x, req);
		break;
	case LDMS_CMD_CANCEL_NOTIFY:
		process_cancel_notify_request(x, req);
		break;
	case LDMS_CMD_UPDATE:
		break;
	default:
		x->log("Unrecognized request %d\n", cmd);
		assert(0);
	}
	return 0;
}

void process_lookup_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			  struct ldms_context *ctxt)
{
	int rc = ntohl(reply->hdr.rc);
	if (!rc) {
		/* A peer should only receive error in lookup_reply.
		 * A successful lookup is handled by rendezvous. */
		x->log("WARNING: Receive lookup reply error with rc: 0\n");
		goto out;
	}
	if (ctxt->lookup.cb)
		ctxt->lookup.cb(x, rc, 0, NULL, ctxt->lookup.cb_arg);

out:
	assert(x->active_lookup);
	x->active_lookup--;
	zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
#ifdef DEBUG
	x->log("DEBUG: lookup_reply: put ref %p: active_lookup = %d\n",
			x->zap_ep, x->active_lookup);
#endif /* DEBUG */

	free(ctxt->lookup.path);
	free(ctxt);
}

void process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt)
{
	int i;
	char *src, *dst;
	enum ldms_dir_type type = ntohl(reply->dir.type);
	int rc = ntohl(reply->hdr.rc);
	int more = ntohl(reply->dir.more);
	size_t len = ntohl(reply->dir.set_list_len);
	unsigned count = ntohl(reply->dir.set_count);
	ldms_dir_t dir = NULL;
	if (rc)
		goto out;
	dir = malloc(sizeof (*dir) +
		     (count * sizeof(char *)) + len);
	rc = ENOMEM;
	if (!dir)
		goto out;
	rc = 0;
	dir->type = type;
	dir->more = more;
	dir->set_count = count;
	src = reply->dir.set_list;
	dst = (char *)&dir->set_names[count];
	for (i = 0; i < count; i++) {
		dir->set_names[i] = dst;
		strcpy(dst, src);
		len = strlen(src) + 1;
		dst += len;
		src += len;
	}
 out:
	/* Don't touch dir after callback because the dir.cb may have freed it. */
	if (ctxt->dir.cb)
		ctxt->dir.cb((ldms_t)x, rc, dir, ctxt->dir.cb_arg);
	pthread_mutex_lock(&x->lock);
	if (!x->local_dir_xid && !more)
		free(ctxt);
	if (!more) {
		assert(x->active_dir);
		x->active_dir--;
		zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_dir() */
#ifdef DEBUG
		x->log("DEBUG: ..dir_reply: put ref %p. active_dir = %d.\n",
				x->zap_ep, x->active_dir);
#endif /* DEBUG */
	}
	pthread_mutex_unlock(&x->lock);
}

void process_req_notify_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	ldms_notify_event_t event;
	size_t len = ntohl(reply->req_notify.event.len);
	event = malloc(len);
	if (!event)
		return;

	event->type = ntohl(reply->req_notify.event.type);
	event->len = ntohl(reply->req_notify.event.len);

	if (len > sizeof(struct ldms_notify_event_s))
		memcpy(event->u_data,
		       &reply->req_notify.event.u_data,
		       len - sizeof(struct ldms_notify_event_s));

	if (ctxt->req_notify.cb)
		ctxt->req_notify.cb((ldms_t)x,
				    ctxt->req_notify.s,
				    event, ctxt->dir.cb_arg);
}

#ifdef ENABLE_AUTH
static int send_auth_approval(struct ldms_xprt *x)
{
	size_t len;
	int rc = 0;
	struct ldms_reply *reply;

	len = sizeof(struct ldms_reply_hdr);
	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in notify of peer.\n");
		return ENOMEM;
	}
	reply->hdr.xid = 0;
	reply->hdr.cmd = htonl(LDMS_CMD_AUTH_APPROVAL_REPLY);
	reply->hdr.rc = 0;
	reply->hdr.len = htonl(len);
	zap_err_t zerr = zap_send(x->zap_ep, reply, len);
	if (zerr) {
		x->log("Auth error: Failed to send the approval. %s\n",
						zap_err_str(rc));
	}
	free(reply);
	return zerr;
}

void process_auth_challenge_reply(struct ldms_xprt *x, struct ldms_reply *reply,
					struct ldms_context *ctxt)
{
	int rc;
	if (0 != strcmp(x->password, reply->auth_challenge.s)) {
		/* Reject the authentication and disconnect the connection. */
		goto err_n_reject;
	}
	x->auth_approved = LDMS_XPRT_AUTH_APPROVED;
	rc = send_auth_approval(x);
	if (rc)
		goto err_n_reject;
	return;
err_n_reject:
	zap_close(x->zap_ep);
}

void process_auth_approval_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		struct ldms_context *ctxt)
{
	ldms_xprt_put(x); /* Match when sending the password */
	x->auth_approved = LDMS_XPRT_AUTH_APPROVED;
	if (x->connect_cb)
		x->connect_cb(x, LDMS_CONN_EVENT_CONNECTED,
					x->connect_cb_arg);
}
#endif /* ENABLE_AUTH */

void ldms_xprt_dir_free(ldms_t t, ldms_dir_t d)
{
	free(d);
}

void ldms_event_release(ldms_t t, ldms_notify_event_t e)
{
	free(e);
}

static int ldms_xprt_recv_reply(struct ldms_xprt *x, struct ldms_reply *reply)
{
	int cmd = ntohl(reply->hdr.cmd);
	uint64_t xid = reply->hdr.xid;
	struct ldms_context *ctxt;
	ctxt = (struct ldms_context *)(unsigned long)xid;
	switch (cmd) {
	case LDMS_CMD_LOOKUP_REPLY:
		process_lookup_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_REPLY:
		process_dir_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_REQ_NOTIFY_REPLY:
		process_req_notify_reply(x, reply, ctxt);
		break;
#ifdef ENABLE_AUTH
	case LDMS_CMD_AUTH_CHALLENGE_REPLY:
		process_auth_challenge_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_AUTH_APPROVAL_REPLY:
		process_auth_approval_reply(x, reply, ctxt);
		break;
#endif /* ENABLE_AUTH */
	default:
		x->log("Unrecognized reply %d\n", cmd);
	}
	return 0;
}

static int recv_cb(struct ldms_xprt *x, void *r)
{
	struct ldms_request_hdr *h = r;
	int cmd = ntohl(h->cmd);
	if (cmd > LDMS_CMD_REPLY)
		return ldms_xprt_recv_reply(x, r);

	return ldms_xprt_recv_request(x, r);
}

#if defined(__MACH__)
#define _SO_EXT ".dylib"
#undef LDMS_XPRT_LIBPATH_DEFAULT
#define LDMS_XPRT_LIBPATH_DEFAULT "/home/tom/macos/lib"
#else
#define _SO_EXT ".so"
#endif
static char _libdir[PATH_MAX];

zap_mem_info_t ldms_zap_mem_info()
{
	return NULL;
}

void __ldms_passive_connect_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	switch (e) {
	case LDMS_CONN_EVENT_ERROR:
		assert(0);
	case LDMS_CONN_EVENT_CONNECTED:
		assert(0);
	case LDMS_CONN_EVENT_DISCONNECTED:
		ldms_xprt_put(x);
		break;
	}
}

static void ldms_zap_handle_conn_req(zap_ep_t zep)
{
	struct sockaddr lcl, rmt;
	socklen_t xlen;
	char rmt_name[16];
	zap_err_t zerr;
	zap_get_name(zep, &lcl, &rmt, &xlen);
	getnameinfo(&rmt, sizeof(rmt), rmt_name, 128, NULL, 0, NI_NUMERICHOST);

	struct ldms_xprt *x = zap_get_ucontext(zep);
	/*
	 * Accepting zep inherit ucontext from the listening endpoint.
	 * Hence, x is of listening endpoint, not of accepting zep,
	 * and we have to create new ldms_xprt for the accepting zep.
	 */
	struct ldms_xprt *_x = calloc(1, sizeof(*_x));
	if (!_x) {
		x->log("ERROR: Cannot create new ldms_xprt for connection"
				" from %s.\n", rmt_name);
		goto err0;
	}

	*_x = *x; /* copy shared info from x, and just set the private ones */
	_x->zap = x->zap;
	_x->zap_ep = zep;
	_x->ref_count = 1;
	_x->remote_dir_xid = _x->local_dir_xid = 0;
	_x->connect_cb = __ldms_passive_connect_cb;
	zap_set_ucontext(zep, _x);
	pthread_mutex_init(&_x->lock, NULL);

	char *data = 0;
	size_t datalen = 0;
#ifdef ENABLE_AUTH
	uint64_t challenge;
	struct ovis_auth_challenge chl;
	if (x->password) {
		/*
		 * Do the authentication.
		 *
		 * The application sets the environment variable for
		 * authentication file.
		 *
		 * If x->auth_envpath is NULL, the state machine
		 * will be the state machine without authentication.
		 */
		challenge = ovis_auth_gen_challenge();
		_x->password = ovis_auth_encrypt_password(challenge, x->password);
		if (!_x->password) {
			x->log("Auth Error: Failed to encrypt the password.");
			zerr = zap_reject(zep);
			if (zerr) {
				x->log("Auth Error: Failed to reject the"
						"conn_request from %s\n",
						rmt_name);
				goto err0;
			}
		} else {
			data = (void *)ovis_auth_pack_challenge(challenge, &chl);
			datalen = sizeof(chl);
		}
	}
#endif /* ENABLE_AUTH */

	zerr = zap_accept(zep, ldms_zap_auto_cb, data, datalen);
	if (zerr) {
		x->log("ERROR: cannot accept connection from %s.\n", rmt_name);
		goto err0;
	}

	/* Take a 'connect' reference. Dropped in ldms_xprt_close() */
	ldms_xprt_get(_x);

	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, _x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
out:
	return;
err0:
	zap_close(zep);
}

#ifdef ENABLE_AUTH
int send_auth_password(struct ldms_xprt *x, const char *password)
{
	size_t len;
	int rc = 0;
	struct ldms_reply *reply;

	len = sizeof(struct ldms_reply_hdr)
			+ sizeof(struct ldms_auth_challenge_reply)
			+ strlen(password) + 1;
	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in notify of peer.\n");
		return ENOMEM;
	}
	reply->hdr.xid = 0;
	reply->hdr.cmd = htonl(LDMS_CMD_AUTH_CHALLENGE_REPLY);
	reply->hdr.rc = 0;
	reply->hdr.len = htonl(len);
	strncpy(reply->auth_challenge.s, password, strlen(password));
	/* Release in process...approval_reply/disconnected */
	ldms_xprt_get(x);
	zap_err_t zerr = zap_send(x->zap_ep, reply, len);
	if (zerr) {
		x->log("Auth error: Failed to send the password. %s\n",
						zap_err_str(rc));
		ldms_xprt_put(x);
		x->auth_approved = LDMS_XPRT_AUTH_FAILED;
	}
	x->auth_approved = LDMS_XPRT_AUTH_PASSWORD;
	free(reply);
	return zerr;
}

static void ldms_xprt_auth_handle_challenge(struct ldms_xprt *x, void *r)
{
	int rc;
	if (!x->password) {
		x->log("Auth error: the server requires authentication.\n");
		goto err;
	}
	struct ovis_auth_challenge *chl;
	chl = (struct ovis_auth_challenge *)r;
	uint64_t challenge = ovis_auth_unpack_challenge(chl);
	char *psswd = ovis_auth_encrypt_password(challenge, x->password);
	if (!psswd) {
		x->log("Auth error: Failed to get the password\n");
		goto err;
	} else {
		rc = send_auth_password(x, psswd);
		free(psswd);
		if (rc)
			goto err;
	}
	return;
err:
	/*
	 * Close the zap_connection. Both active and passive sides will receive
	 * DISCONNECTED event. See more in ldms_zap_cb().
	 */
	x->auth_approved = LDMS_XPRT_AUTH_FAILED;
	zap_close(x->zap_ep);
}
#endif /* ENABLE_AUTH */

static void handle_zap_read_complete(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_context *ctxt = ev->context;
	switch (ctxt->type) {
	case LDMS_CONTEXT_UPDATE:
		if (ctxt->update.cb) {
			struct ldms_xprt *x = zap_get_ucontext(zep);
			ctxt->update.cb((ldms_t)x, ctxt->update.s, ev->status,
					ctxt->update.arg);
			zap_put_ep(x->zap_ep); /* Taken in ldms_remote_update() */
		}
		break;
	case LDMS_CONTEXT_LOOKUP:
		if (ctxt->lookup.cb) {
			struct ldms_xprt *x = zap_get_ucontext(zep);
			ctxt->lookup.cb((ldms_t)x, ev->status, ctxt->lookup.more, ctxt->lookup.s,
					ctxt->lookup.cb_arg);
			if (!ctxt->lookup.more) {
				assert(x->active_lookup > 0);
				x->active_lookup--;
				zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
#ifdef DEBUG
				x->log("DEBUG: read_complete: put ref %p: "
						"active_lookup = %d\n",
						x->zap_ep, x->active_lookup);
#endif /* DEBUG */
			}

		}
		break;
	default:
		assert(0 == "Invalid context type in zap read completion.");
	}
	free(ctxt);
}

static void handle_zap_rendezvous(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt *x = zap_get_ucontext(zep);
	struct ldms_lookup_msg *lm = (struct ldms_lookup_msg *)ev->data;
	struct ldms_context *ctxt = (void*)lm->xid;
	struct ldms_set_desc *sd = NULL;
	struct ldms_rbuf_desc *rbd;
	int rc;
	ldms_set_t set_t;

	/*
	 * Create a local instance of this remote metric set. The set must not
	 * exists. The application should destroy existing set before lookup.
	 */
	rc = __ldms_create_set(lm->inst_name,
			       ntohl(lm->meta_len), ntohl(lm->data_len),
			       ntohl(lm->card),
			       &set_t,
			       LDMS_SET_F_REMOTE);
	if (rc)
		goto out;
	sd = (struct ldms_set_desc *)set_t;

	/* Bind this set to an RBD */
	rbd = ldms_alloc_rbd(x, sd->set);

	if (!rbd) {
		rc = ENOMEM;
		goto out_1;
	}

	rbd->rmap = ev->map;

	sd->rbd = rbd;
	struct ldms_context *rd_ctxt;
	if (lm->more) {
		rd_ctxt = malloc(sizeof *rd_ctxt);
		*rd_ctxt = *ctxt;
	} else {
		rd_ctxt = ctxt;
	}
	rd_ctxt->lookup.s = sd;
	rd_ctxt->lookup.more = ntohl(lm->more);
	if (zap_read(zep,
		     sd->rbd->rmap, zap_map_addr(sd->rbd->rmap),
		     sd->rbd->lmap, zap_map_addr(sd->rbd->lmap),
		     __le32_to_cpu(sd->set->meta->meta_sz),
		     rd_ctxt)) {
		rc = EIO;
		goto out;
	}
	return;
 out_1:
	ldms_set_delete(sd);
	free(sd);
	sd = NULL;
 out:
	if (ctxt->lookup.cb)
		ctxt->lookup.cb(x, rc, 0, (ldms_set_t)sd, ctxt->lookup.cb_arg);
	if (!lm->more) {
		assert(x->active_lookup);
		x->active_lookup--;
		zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
#ifdef DEBUG
		x->log("DEBUG: rendezvous error: put ref %p: "
				"active_lookup = %d\n",
				x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	}
	free(ctxt->lookup.path);
	free(ctxt);
}

/**
 * ldms-zap event handling function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t zerr;
	int ldms_conn_event;
	struct ldms_version *ver;
	struct ldms_xprt *x = zap_get_ucontext(zep);
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		ver = (void*)ev->data;
		if (!ev->data_len || !LDMS_VERSION_EQUAL(*ver)) {
			zap_reject(zep);
			break;
		}
		ldms_zap_handle_conn_req(zep);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		if (x->connect_cb)
			x->connect_cb(x, LDMS_CONN_EVENT_ERROR,
				      x->connect_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_REJECTED:
		if (x->connect_cb)
			x->connect_cb(x, LDMS_CONN_EVENT_REJECTED,
					x->connect_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_CONNECTED:
#ifdef ENABLE_AUTH
		if (ev->data_len) {
			/*
			 * The server sent a challenge for
			 * authentication.
			 */
			ldms_xprt_auth_handle_challenge(x, ev->data);
			break;
		}
		/*
		 * The server doesn't do authentication.
		 * Fall to the state machine without authentication.
		 */
#endif /* ENABLE_AUTH */
		if (x->connect_cb)
			x->connect_cb(x, LDMS_CONN_EVENT_CONNECTED,
				      x->connect_cb_arg);

		break;
	case ZAP_EVENT_DISCONNECTED:
		ldms_conn_event = LDMS_CONN_EVENT_DISCONNECTED;
#ifdef ENABLE_AUTH
		if ((x->auth_approved != LDMS_XPRT_AUTH_DISABLE) &&
			(x->auth_approved != LDMS_XPRT_AUTH_APPROVED)) {
			if (x->auth_approved == LDMS_XPRT_AUTH_PASSWORD) {
				/* Put the ref taken when sent the password */
				ldms_xprt_put(x);
			}
			/*
			 * The active side to receive DISCONNECTED before
			 * the authentication is approved because
			 *  - the server rejected the authentication, or
			 *  - the client fails to respond the server's challenge.
			 *
			 *  Send the LDMS_CONN_EVENT_REJECTED to the application.
			 */
			ldms_conn_event = LDMS_CONN_EVENT_REJECTED;
		}
#endif /* ENABLE_AUTH */
		if (x->connect_cb)
			x->connect_cb(x, ldms_conn_event, x->connect_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() or accept() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		recv_cb(x, ev->data);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		handle_zap_read_complete(zep, ev);
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		/* ldms don't do write. */
		assert(0 == "Illegal zap write");
		break;
	case ZAP_EVENT_RENDEZVOUS:
		/* The other end does zap_share(). */
		handle_zap_rendezvous(zep, ev);
		break;
	}
}

static void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t zerr;
	struct ldms_xprt *x = zap_get_ucontext(zep);
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		assert(0 == "Illegal connect request.");
		break;
	case ZAP_EVENT_CONNECTED:
		break;
	case ZAP_EVENT_DISCONNECTED:
#ifdef ENABLE_AUTH
		if (x->connect_cb)
			x->connect_cb(x, LDMS_CONN_EVENT_DISCONNECTED,
						x->connect_cb_arg);
		/* Put back the reference taken when accept the connection */
		ldms_xprt_put(x);
		break;
#endif /* ENABLE_AUTH */
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_RENDEZVOUS:
		ldms_zap_cb(zep, ev);
		break;
	default:
		assert(0);
	}
}

int __ldms_xprt_zap_new(struct ldms_xprt *x, const char *name,
					ldms_log_fn_t log_fn)
{
	int ret = 0;
	x->zap = zap_get(name, log_fn, ldms_zap_mem_info);
	if (!x->zap) {
		log_fn("ERROR: Cannot get zap plugin: %s\n", name);
		ret = ENOENT;
		goto err0;
	}

	x->zap_ep = zap_new(x->zap, ldms_zap_cb);
	if (!x->zap_ep) {
		log_fn("ERROR: Cannot create zap endpoint.\n");
		ret = ENOMEM;
		goto err1;
	}
	zap_set_ucontext(x->zap_ep, x);

	strncpy(x->name, name, LDMS_MAX_TRANSPORT_NAME_LEN);
	x->ref_count = 1;
	x->remote_dir_xid = x->local_dir_xid = 0;

	x->log = log_fn;
	sem_init(&x->sem, 0, 0);
	pthread_mutex_init(&x->lock, NULL);
	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
	return 0;
err1:
	free(x->zap);
err0:
	return ret;
}

ldms_t ldms_xprt_new(const char *name, ldms_log_fn_t log_fn)
{
	int ret = 0;
	char *libdir;
	struct ldms_xprt *x = calloc(1, sizeof(*x));
	if (!x) {
		ret = ENOMEM;
		goto err0;
	}

	if (!log_fn)
		log_fn = default_log;

	ret = __ldms_xprt_zap_new(x, name, log_fn);
	if (ret)
		goto err1;

	return x;
err1:
	free(x);
err0:
	errno = ret;
	return NULL;
}

#ifdef ENABLE_AUTH
ldms_t ldms_xprt_with_auth_new(const char *name, ldms_log_fn_t log_fn,
				const char *secretword)
{
#ifdef DEBUG
	log_fn("ldms_xprt [DEBUG]: Creating transport with authentication\n");
#endif /* DEBUG */
	int ret = 0;
	char *libdir;
	struct ldms_xprt *x = calloc(1, sizeof(*x));
	if (!x) {
		ret = ENOMEM;
		goto err0;
	}

	char *errstr;
	int len;

	if (!log_fn)
		log_fn = default_log;

	if (secretword) {
		x->password = strdup(secretword);
		if (!x->password) {
			ret = errno;
			goto err1;
		}
		x->auth_approved = LDMS_XPRT_AUTH_INIT;
	}

	ret = __ldms_xprt_zap_new(x, name, log_fn);
	if (ret)
		goto err1;

	return x;
err1:
	if (x->password)
		free((void *)x->password);
	free(x);
err0:
	errno = ret;
	return NULL;
}
#endif /* ENABLE_AUTH */

size_t format_lookup_req(struct ldms_request *req, enum ldms_lookup_flags flags,
			 const char *path, uint64_t xid)
{
	size_t len = strlen(path) + 1;
	strcpy(req->lookup.path, path);
	req->lookup.path_len = htonl(len);
	req->lookup.flags = htonl(flags);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_LOOKUP);
	len += sizeof(uint32_t) + sizeof(uint32_t) + sizeof(struct ldms_request_hdr);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_dir_req(struct ldms_request *req, uint64_t xid,
		      uint32_t flags)
{
	size_t len;
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_DIR);
	req->dir.flags = htonl(flags);
	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_dir_cmd_param);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_dir_cancel_req(struct ldms_request *req)
{
	size_t len;
	req->hdr.xid = 0;
	req->hdr.cmd = htonl(LDMS_CMD_DIR_CANCEL);
	len = sizeof(struct ldms_request_hdr);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_req_notify_req(struct ldms_request *req,
			     uint64_t xid,
			     uint64_t set_id,
			     uint64_t flags)
{
	size_t len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_req_notify_cmd_param);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_REQ_NOTIFY);
	req->hdr.len = htonl(len);
	req->req_notify.set_id = set_id;
	req->req_notify.flags = flags;
	return len;
}

size_t format_cancel_notify_req(struct ldms_request *req, uint64_t xid)
{
	size_t len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_cancel_notify_cmd_param);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_CANCEL_NOTIFY);
	req->hdr.len = htonl(len);
	return len;
}

/*
 * This is the generic allocator for both the request buffer and the
 * context buffer. A single buffer is allocated that is big enough to
 * contain one structure. When the context is freed, the associated
 * request buffer is freed as well.
 */
static int alloc_req_ctxt(struct ldms_request **req,
			  struct ldms_context **ctxt,
			  ldms_context_type_t type)
{
	struct ldms_context *ctxt_;
	void *buf = malloc(sizeof(struct ldms_request) + sizeof(struct ldms_context));
	if (!buf)
		return 1;
	*ctxt = ctxt_ = buf;
	*req = (struct ldms_request *)(ctxt_+1);
	ctxt_->type = type;
	return 0;
}

int __ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt, LDMS_CONTEXT_DIR))
		return ENOMEM;

	pthread_mutex_lock(&x->lock);
	/* If a dir has previously been done and updates were asked
	 * for, free that cached context */
	if (x->local_dir_xid) {
		free((void *)(unsigned long)x->local_dir_xid);
		x->local_dir_xid = 0;
	}
	len = format_dir_req(req, (uint64_t)(unsigned long)ctxt, flags);
	ctxt->dir.cb = cb;
	ctxt->dir.cb_arg = cb_arg;
	if (flags)
		x->local_dir_xid = (uint64_t)ctxt;
	pthread_mutex_unlock(&x->lock);

	zap_get_ep(x->zap_ep);	/* Released in process_dir_reply() */
	x->active_dir++; /* Increment number of active dir request */
#ifdef DEBUG
	x->log("DEBUG: remote_dir. get ref %p. active_dir = %d.\n",
			x->zap_ep, x->active_dir);
#endif /* DEBUG */
	int rc = zap_send(x->zap_ep, req, len);
	if (rc) {
		if (x->active_dir > 0) {
			/*
			 * The active_dir could be decremented in the
			 * DISCONNECTED path already.
			 */
			x->active_dir--;
			zap_put_ep(x->zap_ep);
#ifdef DEBUG
			x->log("DEBUG: remote_dir: error. put ref %p. "
					"active_dir = %d.\n",
					x->zap_ep, x->active_dir);
#endif /* DEBUG */
		}
	}
	return rc;
}

/* This request has no reply */
void __ldms_remote_dir_cancel(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt, LDMS_CONTEXT_DIR_CANCEL))
		return;

	pthread_mutex_lock(&x->lock);
	if (x->local_dir_xid)
		free((void *)(unsigned long)x->local_dir_xid);
	x->local_dir_xid = 0;
	pthread_mutex_unlock(&x->lock);

	len = format_dir_cancel_req(req);
	zap_send(x->zap_ep, req, len);
	free(ctxt);
}

int __ldms_remote_lookup(ldms_t _x, const char *path,
			 enum ldms_lookup_flags flags,
			 ldms_lookup_cb_t cb, void *arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;
	int rc;

	struct ldms_set *set = __ldms_find_local_set(path);
	__ldms_release_local_set(set);
	if (set)
		return EEXIST;

	if (alloc_req_ctxt(&req, &ctxt, LDMS_CONTEXT_LOOKUP))
		return ENOMEM;

	len = format_lookup_req(req, flags, path, (uint64_t)(unsigned long)ctxt);
	ctxt->lookup.s = NULL;
	ctxt->lookup.cb = cb;
	ctxt->lookup.cb_arg = arg;
	ctxt->lookup.flags = flags;
	ctxt->lookup.path = strdup(path);
	zap_get_ep(x->zap_ep);	/* Released in either ...lookup_reply() or ..rendezvous() */
	x->active_lookup++;
#ifdef DEBUG
	x->log("DEBUG: remote_lookup: get ref %p: active_lookup = %d\n",
		x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	rc = zap_send(x->zap_ep, req, len);
	if (rc) {
		if (x->active_lookup > 0) {
			/*
			 * The active_lookup could be decremented in the
			 * DISCONNECTED path already.
			 */
			x->active_lookup--;
			zap_put_ep(x->zap_ep);
#ifdef DEBUG
			x->log("DEBUG: lookup_reply: error. put ref %p: "
					"active_lookup = %d\n",
					x->zap_ep, x->active_lookup);
#endif /* DEBUG */
		}
	}
	return rc;
}

static int send_req_notify(ldms_t _x, ldms_set_t s, uint32_t flags,
			   ldms_notify_cb_t cb_fn, void *cb_arg)
{
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)
		((struct ldms_set_desc *)s)->rbd;
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt, LDMS_CONTEXT_REQ_NOTIFY))
		return ENOMEM;

	if (r->local_notify_xid) {
		free((void *)(unsigned long)r->local_notify_xid);
		r->local_notify_xid = 0;
	}
	len = format_req_notify_req(req, (uint64_t)(unsigned long)ctxt,
				    r->remote_set_id, flags);
	ctxt->req_notify.cb = cb_fn;
	ctxt->req_notify.arg = cb_arg;
	ctxt->req_notify.s = s;
	r->local_notify_xid = (uint64_t)ctxt;

	return zap_send(x->zap_ep, req, len);
}

int ldms_register_notify_cb(ldms_t x, ldms_set_t s, int flags,
			    ldms_notify_cb_t cb_fn, void *cb_arg)
{
	if (!cb_fn)
		goto err;
	return send_req_notify(x, s, (uint32_t)flags, cb_fn, cb_arg);
 err:
	errno = EINVAL;
	return -1;
}

static int send_cancel_notify(ldms_t _x, ldms_set_t s)
{
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)
		((struct ldms_set_desc *)s)->rbd;
	struct ldms_xprt *x = _x;
	struct ldms_request req;
	size_t len;

	len = format_cancel_notify_req
		(&req, (uint64_t)(unsigned long)r->local_notify_xid);
	r->local_notify_xid = 0;

	return zap_send(x->zap_ep, &req, len);
}

int ldms_cancel_notify(ldms_t t, ldms_set_t s)
{
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	if (!set)
		goto err;
	return send_cancel_notify(t, s);
 err:
	errno = EINVAL;
	return -1;
}

void ldms_notify(ldms_set_t s, ldms_notify_event_t e)
{
	struct ldms_set *set;
	struct ldms_rbuf_desc *r;
	if (!s)
		return;
	set = ((struct ldms_set_desc *)s)->set;
	if (!set)
		return;

	if (LIST_EMPTY(&set->rbd_list))
		return;

	LIST_FOREACH(r, &set->rbd_list, set_link) {
		if (r->remote_notify_xid)
			send_req_notify_reply(r->xprt,
					      set, r->remote_notify_xid,
					      e);
	}
}

int ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_connect_cb_t cb, void *cb_arg)
{
	int rc;
	struct ldms_xprt *_x = x;
	struct ldms_version ver;
	LDMS_VERSION_SET(ver);
	_x->connect_cb = cb;
	_x->connect_cb_arg = cb_arg;
	ldms_xprt_get(x);
	rc = zap_connect(_x->zap_ep, sa, sa_len, (void*)&ver, sizeof(ver));
	if (rc)
		ldms_xprt_put(x);
	return rc;
}

static void sync_connect_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	switch (e) {
	case LDMS_CONN_EVENT_CONNECTED:
		x->sem_rc = 0;
		break;
	case LDMS_CONN_EVENT_ERROR:
	case LDMS_CONN_EVENT_DISCONNECTED:
		x->sem_rc = ECONNREFUSED;
		break;
	}
	sem_post(&x->sem);
}

int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_connect_cb_t cb, void *cb_arg)
{
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	int rc = getaddrinfo(host, port, &hints, &ai);
	if (rc)
		return EHOSTUNREACH;
	if (!cb) {
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, sync_connect_cb, cb_arg);
		if (rc)
			return rc;
		sem_wait(&x->sem);
		rc = x->sem_rc;
	} else
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, cb, cb_arg);
 out:
	freeaddrinfo(ai);
	return rc;
}

int ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len)
{
	return zap_listen(x->zap_ep, sa, sa_len);
}

int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port_no)
{
	int rc;
	struct sockaddr_in sin;
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	if (host) {
		rc = getaddrinfo(host, port_no, &hints, &ai);
		if (rc)
			return EHOSTUNREACH;
		rc = ldms_xprt_listen(x, ai->ai_addr, ai->ai_addrlen);
	} else {
		short port = atoi(port_no);
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = 0;
		sin.sin_port = htons(port);
		rc = ldms_xprt_listen(x, (struct sockaddr *)&sin, sizeof(sin));
	}
	return rc;
}

static struct ldms_rbuf_desc *
ldms_alloc_rbd(struct ldms_xprt *x, struct ldms_set *s)
{
	struct ldms_rbuf_desc *rbd = calloc(1, sizeof(*rbd));
	if (!rbd)
		goto err0;

	rbd->xprt = x;
	rbd->set = s;
	size_t set_sz = __ldms_set_size_get(s);
	zap_err_t zerr = zap_map(x->zap_ep, &rbd->lmap, s->meta, set_sz,
							ZAP_ACCESS_READ);
	if (zerr)
		goto err1;

	/* Add RBD to set list */
	LIST_INSERT_HEAD(&s->rbd_list, rbd, set_link);
	LIST_INSERT_HEAD(&x->rbd_list, rbd, xprt_link);

	goto out;

err1:
	free(rbd);
	rbd = NULL;
err0:
out:
	return rbd;
}

void __ldms_free_rbd(struct ldms_rbuf_desc *rbd)
{
	LIST_REMOVE(rbd, xprt_link);
	LIST_REMOVE(rbd, set_link);
#ifdef DEBUG
	if (rbd->lmap) {
		rbd->xprt->log("DEBUG: zap %p: unmap local\n", rbd->xprt->zap_ep);
		zap_unmap(rbd->xprt->zap_ep, rbd->lmap);
	}
	if (rbd->rmap) {
		rbd->xprt->log("DEBUG: zap %p: unmap remote\n", rbd->xprt->zap_ep);
		zap_unmap(rbd->xprt->zap_ep, rbd->rmap);
	}
#else
	if (rbd->lmap)
		zap_unmap(rbd->xprt->zap_ep, rbd->lmap);
	if (rbd->rmap)
		zap_unmap(rbd->xprt->zap_ep, rbd->rmap);
#endif /* DEBUG */
	free(rbd);
}

static struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *x, struct ldms_set *set)
{
	struct ldms_rbuf_desc *r;
	if (!set)
		return NULL;

	if (LIST_EMPTY(&x->rbd_list))
		return NULL;

	LIST_FOREACH(r, &x->rbd_list, xprt_link) {
		if (r->set == set)
			return r;
	}

	return NULL;
}

static void __attribute__ ((constructor)) cs_init(void)
{
	pthread_mutex_init(&xprt_list_lock, 0);
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
