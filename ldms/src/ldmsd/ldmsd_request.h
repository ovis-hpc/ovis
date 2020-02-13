/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <inttypes.h>
#include "coll/rbt.h"
#include "ldms.h"
#include "ldmsd.h"

#ifndef LDMS_SRC_LDMSD_LDMSD_REQUEST_H_
#define LDMS_SRC_LDMSD_LDMSD_REQUEST_H_

extern int ldmsd_req_debug;

enum ldmsd_request {
	LDMSD_EXAMPLE_REQ = 0x1,
	LDMSD_GREETING_REQ = 0x2,
	LDMSD_PRDCR_ADD_REQ = 0x100,
	LDMSD_PRDCR_DEL_REQ,
	LDMSD_PRDCR_START_REQ,
	LDMSD_PRDCR_STOP_REQ,
	LDMSD_PRDCR_STATUS_REQ,
	LDMSD_PRDCR_START_REGEX_REQ,
	LDMSD_PRDCR_STOP_REGEX_REQ,
	LDMSD_PRDCR_SET_REQ,
	LDMSD_PRDCR_SUBSCRIBE_REQ,
	LDMSD_STRGP_ADD_REQ = 0x200,
	LDMSD_STRGP_DEL_REQ,
	LDMSD_STRGP_START_REQ,
	LDMSD_STRGP_STOP_REQ,
	LDMSD_STRGP_STATUS_REQ,
	LDMSD_STRGP_PRDCR_ADD_REQ,
	LDMSD_STRGP_PRDCR_DEL_REQ,
	LDMSD_STRGP_METRIC_ADD_REQ,
	LDMSD_STRGP_METRIC_DEL_REQ,
	LDMSD_UPDTR_ADD_REQ = 0x300,
	LDMSD_UPDTR_DEL_REQ,
	LDMSD_UPDTR_START_REQ,
	LDMSD_UPDTR_STOP_REQ,
	LDMSD_UPDTR_STATUS_REQ,
	LDMSD_UPDTR_PRDCR_ADD_REQ,
	LDMSD_UPDTR_PRDCR_DEL_REQ,
	LDMSD_UPDTR_MATCH_ADD_REQ,
	LDMSD_UPDTR_MATCH_DEL_REQ,
	LDMSD_SMPLR_ADD_REQ = 0x400,
	LDMSD_SMPLR_DEL_REQ,
	LDMSD_SMPLR_START_REQ,
	LDMSD_SMPLR_STOP_REQ,
	LDMSD_SMPLR_STATUS_REQ,
	LDMSD_PLUGN_ADD_REQ = 0x500,
	LDMSD_PLUGN_DEL_REQ,
	LDMSD_PLUGN_STATUS_REQ,
	LDMSD_PLUGN_LOAD_REQ,
	LDMSD_PLUGN_TERM_REQ,
	LDMSD_PLUGN_CONFIG_REQ,
	LDMSD_PLUGN_LIST_REQ,
	LDMSD_PLUGN_SETS_REQ,
	LDMSD_PLUGN_USAGE_REQ,
	LDMSD_PLUGN_QUERY_REQ,
	LDMSD_SET_UDATA_REQ = 0x600,
	LDMSD_SET_UDATA_REGEX_REQ,
	LDMSD_VERBOSE_REQ,
	LDMSD_DAEMON_STATUS_REQ,
	LDMSD_VERSION_REQ,
	LDMSD_ENV_REQ,
	LDMSD_INCLUDE_REQ,
	LDMSD_ONESHOT_REQ,
	LDMSD_LOGROTATE_REQ,
	LDMSD_EXIT_DAEMON_REQ,
	LDMSD_RECORD_LEN_ADVICE_REQ,
	LDMSD_SET_ROUTE_REQ,
	LDMSD_EXPORT_CONFIG_REQ,

	/* command-line options */
	LDMSD_CMD_LINE_SET_REQ,

	/* command-line options */
	LDMSD_LISTEN_REQ,

	/* failover requests by user */
	LDMSD_FAILOVER_CONFIG_REQ = 0x700, /* "failover_config" user command */
	LDMSD_FAILOVER_PEERCFG_START_REQ, /* "failover_peercfg_start" user command */
	LDMSD_FAILOVER_PEERCFG_STOP_REQ, /* "failover_peercfg_stop" user command */
	LDMSD_FAILOVER_MOD_REQ, /* DEPRECATED: "failove_mod" user command */
	LDMSD_FAILOVER_STATUS_REQ, /* "failover_status" user command */

	/* internal failover requests by ldmsd (not exposed to users) */
	LDMSD_FAILOVER_PAIR_REQ, /* Pairing request */
	LDMSD_FAILOVER_RESET_REQ, /* Request to reset failover */
	LDMSD_FAILOVER_CFGPRDCR_REQ, /* internal producer failover config */
	LDMSD_FAILOVER_CFGUPDTR_REQ, /* internal updater failover config */
	LDMSD_FAILOVER_CFGSTRGP_REQ, /* internal strgp failover config */
	LDMSD_FAILOVER_PING_REQ, /* ping message over REQ protocol */
	LDMSD_FAILOVER_PEERCFG_REQ, /* request peer cfg */

	/* additional failover requests by user */
	LDMSD_FAILOVER_START_REQ = 0x770, /* start the failover service */
	LDMSD_FAILOVER_STOP_REQ, /* stop the failover service */

	/* Set Group Requests */
	LDMSD_SETGROUP_ADD_REQ = 0x800, /* Create new set group */
	LDMSD_SETGROUP_MOD_REQ, /* Modify set group attributes */
	LDMSD_SETGROUP_DEL_REQ, /* Delete an entire set group */
	LDMSD_SETGROUP_INS_REQ, /* Insert a set into a group */
	LDMSD_SETGROUP_RM_REQ, /* Remove a set from a group */

	/* Publish/Subscribe Requests */
	LDMSD_STREAM_PUBLISH_REQ = 0x900, /* Publish data to a stream */
	LDMSD_STREAM_SUBSCRIBE_REQ,	  /* Subscribe to a stream */

	/* Auth */
	LDMSD_AUTH_ADD_REQ = 0xa00, /* Add auth domain */
	LDMSD_AUTH_DEL_REQ,         /* Delete auth domain */

	LDMSD_NOTSUPPORT_REQ,
};

enum ldmsd_request_attr {
	/* Common attribute */
	LDMSD_ATTR_TERM = 0,
	LDMSD_ATTR_NAME = 1,
	LDMSD_ATTR_INTERVAL,
	LDMSD_ATTR_OFFSET,
	LDMSD_ATTR_REGEX,
	LDMSD_ATTR_TYPE,
	LDMSD_ATTR_PRODUCER,
	LDMSD_ATTR_INSTANCE,
	LDMSD_ATTR_XPRT,
	LDMSD_ATTR_HOST,
	LDMSD_ATTR_PORT,
	LDMSD_ATTR_MATCH,
	LDMSD_ATTR_PLUGIN,
	LDMSD_ATTR_CONTAINER,
	LDMSD_ATTR_SCHEMA,
	LDMSD_ATTR_METRIC,
	LDMSD_ATTR_STRING,
	LDMSD_ATTR_UDATA,
	LDMSD_ATTR_BASE,
	LDMSD_ATTR_INCREMENT,
	LDMSD_ATTR_LEVEL,
	LDMSD_ATTR_PATH,
	LDMSD_ATTR_TIME,
	LDMSD_ATTR_PUSH,
	LDMSD_ATTR_TEST,
	LDMSD_ATTR_REC_LEN,
	LDMSD_ATTR_JSON,
	LDMSD_ATTR_PERM,
	LDMSD_ATTR_AUTO_SWITCH,
	LDMSD_ATTR_PEER_NAME,
	LDMSD_ATTR_TIMEOUT_FACTOR,
	LDMSD_ATTR_AUTO_INTERVAL,
	LDMSD_ATTR_UID,
	LDMSD_ATTR_GID,
	LDMSD_ATTR_STREAM,
	LDMSD_ATTR_COMP_ID,
	LDMSD_ATTR_QUERY,
	LDMSD_ATTR_AUTH,
	LDMSD_ATTR_ENV,
	LDMSD_ATTR_CMDLINE,
	LDMSD_ATTR_CFGCMD,
	LDMSD_ATTR_LAST,
};

#define LDMSD_RECORD_MARKER 0xffffffff

#define LDMSD_REQ_SOM_F	1 /* start of message */
#define LDMSD_REQ_EOM_F	2 /* end of message */

#define LDMSD_REQ_TYPE_CONFIG_CMD 1
#define LDMSD_REQ_TYPE_CONFIG_RESP 2

#pragma pack(push, 1)
typedef struct ldmsd_req_hdr_s {
	uint32_t marker;	/* Always has the value 0xff */
	uint32_t type;		/* Request type */
	uint32_t flags;		/* EOM==1 means this is the last record for this message */
	uint32_t msg_no;	/* Unique for each request */
	union {
		uint32_t req_id;	/* request id */
		uint32_t rsp_err;	/* response error  */
	};
	uint32_t rec_len;	/* Record length in bytes including this header */
} *ldmsd_req_hdr_t;
#pragma pack(pop)

/**
 * The format of requests and replies is as follows:
 *
 *                 Standard Record Format
 * +---------------------------------------------------------+
 * | ldmsd_req_hdr_s                                         |
 * +---------------------------------------------------------+
 * | attr_0, attr_discrim == 1                               |
 * +---------------------------------------------------------+
 * S                                                         |
 * +---------------------------------------------------------+
 * | attr_N, attr_discrim == 1                               |
 * +---------------------------------------------------------+
 * | attr_discrim == 0, remaining fields are not present     |
 * +---------------------------------------------------------+
 *
 *                 String Record Format
 * +---------------------------------------------------------+
 * | ldmsd_req_hdr_s                                         |
 * +---------------------------------------------------------+
 * | String bytes, i.e. attr_discrim > 1                     |
 * | length is rec_len - sizeof(ldmsd_req_hdr_s)             |
 * S                                                         S
 * +---------------------------------------------------------+
 */
typedef struct req_ctxt_key {
	uint32_t msg_no;
	uint64_t conn_id;
} *msg_key_t;

struct ldmsd_req_ctxt;


typedef struct ldmsd_cfg_ldms_s {
	ldms_t ldms;
} *ldmsd_cfg_ldms_t;

typedef struct ldmsd_cfg_sock_s {
	int fd;
	struct sockaddr_storage ss;
} *ldmsd_cfg_sock_t;

typedef struct ldmsd_cfg_file_s {
	const char *filename; /* Config file name */
} *ldmsd_cfg_file_t;

struct ldmsd_cfg_xprt_s;
typedef int (*ldmsd_cfg_send_fn_t)(struct ldmsd_cfg_xprt_s *xprt, char *data, size_t data_len);
typedef void (*ldmsd_cfg_cleanup_fn_t)(struct ldmsd_cfg_xprt_s *xprt);
typedef struct ldmsd_cfg_xprt_s {
	union {
		void *xprt;
		struct ldmsd_cfg_file_s file;
		struct ldmsd_cfg_sock_s sock;
		struct ldmsd_cfg_ldms_s ldms;
	};
	enum {
		LDMSD_CFG_XPRT_CONFIG_FILE = 1,
		LDMSD_CFG_XPRT_SOCK,
		LDMSD_CFG_XPRT_LDMS,
	} type;
	size_t max_msg;
	ldmsd_cfg_send_fn_t send_fn;
	ldmsd_cfg_cleanup_fn_t cleanup_fn;
	int trust; /* trust (to perform command expansion) */
	int rsp_err; /* error from the response */
} *ldmsd_cfg_xprt_t;

#define LINE_BUF_LEN 1024
#define REQ_BUF_LEN 4096

#define LDMSD_REQ_DEFER_FLAG 0x10
typedef struct ldmsd_req_ctxt {
	struct req_ctxt_key key;
	struct rbn rbn;
	int ref_count;
	int flags;

	ldmsd_cfg_xprt_t xprt;	/* network transport */

	void *ctxt;

	int rec_no;

	uint32_t errcode;
	uint32_t req_id;

	size_t line_len;
	size_t line_off;
	char *line_buf;

	size_t req_len;
	size_t req_off;
	char *req_buf;

	size_t rep_len;
	size_t rep_off;
	char *rep_buf;
} *ldmsd_req_ctxt_t;

typedef struct ldmsd_req_cmd *ldmsd_req_cmd_t;
typedef int (* ldmsd_req_resp_fn)(ldmsd_req_cmd_t rcmd);
struct ldmsd_req_cmd {
	ldmsd_req_ctxt_t reqc; /* The request context containing request to be forwarded */
	ldmsd_req_ctxt_t org_reqc; /* The original request context */
	ldmsd_req_resp_fn resp_handler; /* Pointer to the function to handle the response */
	void *ctxt;
};

#pragma pack(push, 1)
typedef struct ldmsd_req_attr_s {
	uint32_t discrim;	/* If 0, the remaining fields are not present */
	uint32_t attr_id;	/* Attribute identifier, unique per ldmsd_req_hdr_s.cmd_id */
	uint32_t attr_len;	/* Size of value in bytes */
	union {
		uint8_t attr_value[OVIS_FLEX_UNION];
		uint16_t attr_u16[OVIS_FLEX_UNION];
		uint32_t attr_u32[OVIS_FLEX_UNION];
		uint64_t attr_u64[OVIS_FLEX_UNION];
	};
} *ldmsd_req_attr_t;
#pragma pack(pop)

struct ldmsd_req_array {
	int num_reqs; /**<  Number of request handles */
	ldmsd_req_hdr_t reqs[OVIS_FLEX]; /**<  array of request handles */
};

/**
 * \brief Translate a config string to LDMSD configuration request(s)
 *
 * The configuration line is in the format of <verb>[ <attr=value> <attr=value>..]
 * The function translates \c cfg string into LDMSD requests. If the resulting request
 * has the record length larger than \c xprt_max_msg, multiple request records will be
 * created instead. The record length of each request (request->rec_len) will not exceed
 * \c xprt_max_msg.
 *
 * Each record is already in the network byte order format.
 *
 * The caller must free each request in the \c ldmsd_request_array->reqs and
 * the \c ldmsd_request_array.
 *
 * \param cfg A string containing the configuaration command text
 * \param msg_no The next unique message number
 * \param xprt_max_msg The
 * \param msglog Destination for error messages.
 *
 * \return a handle to an ldmsd_request_array. NULL is returned in case of error
 *         and errno is set.
 *
 * \seealso ldmsd_request_array
 */
struct ldmsd_req_array *ldmsd_parse_config_str(const char *cfg, uint32_t msg_no,
					size_t xprt_max_msg, ldmsd_msg_log_f msglog);

/**
 * \brief Convert a command string to the request ID.
 *
 * \param verb   Configuration command string
 *
 * \return The request ID is returned on success. LDMSD_NOTSUPPORT_REQ is returned
 *         if the command does not exist.
 */
uint32_t ldmsd_req_str2id(const char *verb);
/**
 * \brief req_id to string (for debugging)
 */
const char *ldmsd_req_id2str(enum ldmsd_request req_id);


/**
 * \brief Convert a attribute name to the attribute ID
 *
 * \param name   Attribute name string
 *
 * \return The attribute ID is returned on success. -1 is returned if
 *         the attribute name is not recognized.
 */
int32_t ldmsd_req_attr_str2id(const char *name);

/**
 * \brief Return a request attribute handle of the given ID
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   Attribute ID
 *
 * \return attribute handle. NULL if it does not exist.
 */
ldmsd_req_attr_t ldmsd_req_attr_get_by_id(char *request, uint32_t attr_id);

/**
 * \brief Return a request attribute handle of the given name
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return attribute handle. NULL if it does not exist.
 */
ldmsd_req_attr_t ldmsd_req_attr_get_by_name(char *request, const char *name);

/**
 * \brief Return the value string of the given attribute ID
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   Attribute ID
 *
 * \return a string. NULL if it does not exist.
 */
char *ldmsd_req_attr_str_value_get_by_id(ldmsd_req_ctxt_t req, uint32_t attr_id);

/**
 * \brief Verify whether the given attribute/keyword ID exists or not.
 *
 * \param request   Buffer storing request header and the attributes
 * \param attr_id   ID of the keyword
 *
 * \return 1 if the keyword or attribute exists. Otherwise, 0 is returned.
 */
int ldmsd_req_attr_keyword_exist_by_id(char *request, uint32_t attr_id);

/**
 * \brief Return the value string of the given attribute name
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return a string. NULL if it does not exist.*
 */
char *ldmsd_req_attr_str_value_get_by_name(ldmsd_req_ctxt_t req, const char *name);

/**
 * \brief Verify whether the given attribute/keyword name exists or not.
 *
 * \param request   Buffer storing request header and the attributes
 * \param name      Attribute name
 *
 * \return 1 if the keyword or attribute exists. Otherwise, 0 is returned.
 */
int ldmsd_req_attr_keyword_exist_by_name(char *request, const char *name);

/**
 * Convert the request header byte order from host to network
 */
void ldmsd_hton_req_hdr(ldmsd_req_hdr_t req);
/**
 * Convert the request attribute byte order from host to network
 */
void ldmsd_hton_req_attr(ldmsd_req_attr_t attr);
/**
 * Convert the message byte order from host to network
 *
 * It assumes that the buffer \c msg contains the whole message.
 */
void ldmsd_hton_req_msg(ldmsd_req_hdr_t msg);
/**
 * Convert the request header byte order from network to host
 */
void ldmsd_ntoh_req_hdr(ldmsd_req_hdr_t req);
/**
 * Convert the request attribute byte order from network to host
 */
void ldmsd_ntoh_req_attr(ldmsd_req_attr_t attr);
/**
 * Convert the message byte order from network to host
 *
 * It assumes that the buffer \c msg contains the whole message.
 */
void ldmsd_ntoh_req_msg(ldmsd_req_hdr_t msg);

/**
 * \brief Advise the peer our configuration record length
 *
 * Send configuration record length advice
 *
 * \param xprt The configuration transport handle
 * \param rec_len The record length
 */
void ldmsd_send_cfg_rec_adv(ldmsd_cfg_xprt_t xprt, uint32_t msg_no, uint32_t rec_len);
typedef int (*ldmsd_req_filter_fn)(ldmsd_req_ctxt_t reqc, void *ctxt);
int ldmsd_process_config_request(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t request,
						ldmsd_req_filter_fn filter_fn);
int ldmsd_process_config_response(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t response);
int ldmsd_append_reply(struct ldmsd_req_ctxt *reqc, const char *data, size_t data_len, int msg_flags);
void ldmsd_send_error_reply(ldmsd_cfg_xprt_t xprt, uint32_t msg_no,
			    uint32_t error, char *data, size_t data_len);
void ldmsd_send_req_response(ldmsd_req_ctxt_t reqc, const char *msg);
int ldmsd_handle_request(ldmsd_req_ctxt_t reqc);
static inline ldmsd_req_attr_t ldmsd_first_attr(ldmsd_req_hdr_t rh)
{
	return (ldmsd_req_attr_t)(rh + 1);
}

static inline ldmsd_req_attr_t ldmsd_next_attr(ldmsd_req_attr_t attr)
{
	return (ldmsd_req_attr_t)&attr->attr_value[attr->attr_len];
}

/**
 * \brief Initialize config transport to be an ldms transport
 */
void ldmsd_cfg_ldms_init(ldmsd_cfg_xprt_t xprt, ldms_t ldms);

/**
 * \brief Send a request to \c prdcr for the set_info of \c inst_name
 */
int ldmsd_set_route_request(ldmsd_prdcr_t prdcr,
			ldmsd_req_ctxt_t org_reqc, char *inst_name,
			ldmsd_req_resp_fn resp_handler, void *ctxt);

/**
 * \brief Construct ldmsd request command.
 *
 * The request command is created with a message buffer. The buffer is flushed
 * when the request command is terminated with ::ldmsd_req_cmd_attr_term()
 * or when the buffer is full. The caller doesn't have to handle
 * start-of-message or end-of-message logic.
 *
 * The \c resp_handler() is called if the peer respond to the request command.
 *
 * \param ldms         The LDMS transport handle.
 * \param req_id       Request ID (see ::ldmsd_request enumeration).
 * \param orgn_reqc    The associated original request (can be NULL). This is
 *                     primarily for relaying commands.
 * \param resp_handler The response handling callback function.
 * \param ctxt         The generic context to this command.
 */
ldmsd_req_cmd_t ldmsd_req_cmd_new(ldms_t ldms,
				    uint32_t req_id,
				    ldmsd_req_ctxt_t orgn_reqc,
				    ldmsd_req_resp_fn resp_handler,
				    void *ctxt);

/**
 * \brief Free the request command.
 */
void ldmsd_req_cmd_free(ldmsd_req_cmd_t rcmd);

/**
 * \brief Append a request attribute to the command.
 */
int ldmsd_req_cmd_attr_append(ldmsd_req_cmd_t rcmd,
			      enum ldmsd_request_attr req_id,
			      const void *value, int value_len);

static inline
int ldmsd_req_cmd_attr_append_str(ldmsd_req_cmd_t rcmd,
				  enum ldmsd_request_attr req_id,
				  const char *str)
{
	return ldmsd_req_cmd_attr_append(rcmd, req_id, str, strlen(str) + 1);
}

static inline
int ldmsd_req_cmd_attr_append_u16(ldmsd_req_cmd_t rcmd,
				  enum ldmsd_request_attr req_id,
				  uint16_t u16)
{
	return ldmsd_req_cmd_attr_append(rcmd, req_id, &u16, sizeof(u16));
}

static inline
int ldmsd_req_cmd_attr_append_u32(ldmsd_req_cmd_t rcmd,
				  enum ldmsd_request_attr req_id,
				  uint32_t u32)
{
	return ldmsd_req_cmd_attr_append(rcmd, req_id, &u32, sizeof(u32));
}

static inline
int ldmsd_req_cmd_attr_append_u64(ldmsd_req_cmd_t rcmd,
				  enum ldmsd_request_attr req_id,
				  uint64_t u64)
{
	return ldmsd_req_cmd_attr_append(rcmd, req_id, &u64, sizeof(u64));
}

/**
 * Terminate the attribute list.
 */
static inline
int ldmsd_req_cmd_attr_term(ldmsd_req_cmd_t rcmd)
{
	return ldmsd_req_cmd_attr_append(rcmd, LDMSD_ATTR_TERM, NULL, 0);
}

#endif /* LDMS_SRC_LDMSD_LDMSD_REQUEST_H_ */
