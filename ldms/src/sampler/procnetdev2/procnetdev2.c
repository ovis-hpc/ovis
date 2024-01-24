/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018,2022,2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018,2022,2024 Open Grid Computing, Inc. All rights
 * reserved.
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
/**
 * \file procnetdev2.c
 * \brief /proc/net/dev data provider
 *
 * This is based on \c procnetdev.c. The difference is that \c procnetdev2 uses
 * \c LDMS_V_LIST and \c LDMS_V_RECORD.
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "../sampler_base.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;

typedef struct procnetdev2_s *procnetdev2_t;
struct procnetdev2_s {
	union {
		struct ldmsd_sampler sampler; /* sampler interface */
		struct ldmsd_plugin  plugin;  /* plugin interface */
	};
	FILE *mf;
	base_data_t base_data;

	int rec_def_idx;
	int netdev_list_mid;
	size_t rec_heap_sz;

	int niface;
	char **iface;
	char *iface_str;

	int nexcludes;
	char **excludes;
	char *excludes_str;
};

struct rec_metric_info {
	int mid;
	const char *name;
	const char *unit;
	enum ldms_value_type type;
	int array_len;
};

#define MAXIFACE 32
#ifndef IFNAMSIZ
/* from "linux/if.h" */
#define IFNAMSIZ 16
#endif

struct ldms_metric_template_s rec_metrics[] = {
	{ "name"          , 0,    LDMS_V_CHAR_ARRAY , ""        , IFNAMSIZ } ,
	{ "rx_bytes"      , 0,    LDMS_V_U64        , "bytes"   , 1  } ,
	{ "rx_packets"    , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_errs"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_drop"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_fifo"       , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "rx_frame"      , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "rx_compressed" , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_multicast"  , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_bytes"      , 0,    LDMS_V_U64        , "bytes"   , 1  } ,
	{ "tx_packets"    , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_errs"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_drop"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_fifo"       , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_colls"      , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_carrier"    , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_compressed" , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{0},
};
#define REC_METRICS_LEN (ARRAY_LEN(rec_metrics) - 1)
static int rec_metric_ids[REC_METRICS_LEN];

/*
 * Metrics/units references:
 * - linux/net/core/net-procfs.c
 * - linux/include/uapi/linux/if_link.h
 */


#define SAMP "procnetdev2"

static ovis_log_t mylog;

static int create_metric_set(procnetdev2_t p)
{
	static ldms_schema_t schema;
        ldms_record_t rec_def;
	size_t heap_sz;
	int rc;

	/* Create a metric set of the required size */
	schema = base_schema_new(p->base_data);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, p->base_data->schema_name, errno);
		rc = EINVAL;
		goto err1;
	}

	/* Create netdev record definition */
	rec_def = ldms_record_from_template("netdev", rec_metrics, rec_metric_ids);
        if (!rec_def) {
		rc = errno;
                goto err2;
	}
	p->rec_heap_sz = ldms_record_heap_size_get(rec_def);
	heap_sz = MAXIFACE * ldms_record_heap_size_get(rec_def);

	/* Add record definition into the schema */
	p->rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (p->rec_def_idx < 0) {
		rc = -p->rec_def_idx;
		goto err3;
	}

	/* Add a list (of records) */
	p->netdev_list_mid = ldms_schema_metric_list_add(schema, "netdev_list", NULL, heap_sz);
	if (p->netdev_list_mid < 0) {
		rc = -p->netdev_list_mid;
		goto err2;
	}

	base_set_new(p->base_data);
	if (!p->base_data->set) {
		rc = errno;
		goto err2;
	}

	return 0;
err3:
        /* Only manually delete rec_def when it has not yet been added
           to the schema */
        ldms_record_delete(rec_def);
err2:
        base_schema_delete(p->base_data);
        p->base_data = NULL;
err1:

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < ARRAY_LEN(deprecated); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP " [ifaces=<csv_str>] [excludes=<csv_str>]\n" \
		BASE_CONFIG_USAGE \
		"    ifaces          A comma-separated list of interface names (e.g. eth0,eth1)\n"
		"                    to be collected. If NOT specified, all interfaces are\n"
		"                    included (unluss the `excludes` option exclude them).\n"
		"    excludes        A comma-separated list of interface names (e.g. lo,eth0)\n"
		"                    to be excluded from the collection.\n"
		"\n"
		"If `ifaces` and `excludes` are NOT specified, the sampler collects data from\n"
		"all interfaces. If only `ifaces` is specified, the sampler only collects data\n"
		"from interfaces in the ifaces list. If only `excludes` is specified,\n"
		"the sampler collects data from all interfaces EXCEPT those in the `excludes`\n"
		"option. If both `ifaces` and `excludes` are specified, the sampler collects\n"
		"data from all interfaces that are in `ifaces` option but are NOT in the\n"
		"`excludes` option.\n"
		;
}

int strpcmp(const void *a, const void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

char **strarray(char *s, int *n_out)
{
	char **arr = NULL;
	char *pch, *saveptr;
	int i, n;

	n = 1;
	for (pch = s; *pch; pch++) {
		if (*pch == ',')
			n++;
	}

	arr = malloc(n*sizeof(arr[0]));
	if (!arr)
		goto out;
	/* fill */
	i = 0;
	pch = strtok_r(s, ",", &saveptr);
	while (pch != NULL){
		arr[i++] = pch;
		pch = strtok_r(NULL, ",", &saveptr);
	}

	/* sort iface array by strcmp */
	qsort(arr, n, sizeof(arr[0]), strpcmp);
	*n_out = n;

 out:
	return arr;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	procnetdev2_t p = (void*)self;
	char* ifacelist = NULL;
	char* excludes_str = NULL;
	char *ivalue = NULL;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (p->base_data) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	/* process ifaces */
	ivalue = av_value(avl, "ifaces");
	if (!ivalue)
		goto excludes;

	ifacelist = strdup(ivalue);
	if (!ifacelist) {
		ovis_log(mylog, OVIS_LCRIT, "out of memory\n");
		goto err;
	}

	p->iface = strarray(ifacelist, &p->niface);
	if (!p->iface) {
		goto err;
	}
	p->iface_str = ifacelist;

 excludes:
	/* process excludes */
	ivalue = av_value(avl, "excludes");
	if (!ivalue)
		goto cfg;
	excludes_str = strdup(ivalue);
	if (!excludes_str) {
		ovis_log(mylog, OVIS_LCRIT, "out of memory\n");
		goto err;
	}
	p->excludes = strarray(excludes_str, &p->nexcludes);
	if (!p->excludes) {
		goto err;
	}
	p->excludes_str = excludes_str;

 cfg:
	p->base_data = base_config(avl, SAMP, SAMP, mylog);
	if (!p->base_data){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(p);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	p->iface_str = NULL;
	p->niface = 0;
	if (p->iface) {
		free(p->iface);
		p->iface = NULL;
	}
	if (ifacelist)
		free(ifacelist);
	if (excludes_str)
		free(excludes_str);
	base_del(p->base_data);
	return rc;

}

static int sample(struct ldmsd_sampler *self)
{
	procnetdev2_t p = (void*)self;
	int rc;
	char *s;
	char lbuf[256];
	char _curriface[IFNAMSIZ];
	char *curriface = _curriface;
	union ldms_value v[REC_METRICS_LEN];
	int i;
	ldms_mval_t lh, rec_inst, name_mval;
	size_t heap_sz;

	if (!p->base_data) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	if (!p->mf)
		p->mf = fopen(procfile, "r");
	if (!p->mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}
begin:
	base_sample_begin(p->base_data);

	lh = ldms_metric_get(p->base_data->set, p->netdev_list_mid);

	/* reset device data */
	ldms_list_purge(p->base_data->set, lh);

	fseek(p->mf, 0, SEEK_SET); //seek should work if get to EOF
	s = fgets(lbuf, sizeof(lbuf), p->mf);
	s = fgets(lbuf, sizeof(lbuf), p->mf);

	/* data */
	do {
		s = fgets(lbuf, sizeof(lbuf), p->mf);
		if (!s)
			break;

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 "\n", curriface, &v[1].v_u64,
				&v[2].v_u64, &v[3].v_u64, &v[4].v_u64,
				&v[5].v_u64, &v[6].v_u64, &v[7].v_u64,
				&v[8].v_u64, &v[9].v_u64, &v[10].v_u64,
				&v[11].v_u64, &v[12].v_u64, &v[13].v_u64,
				&v[14].v_u64, &v[15].v_u64, &v[16].v_u64);
		if (rc != 17){
			ovis_log(mylog, OVIS_LINFO, "wrong number of "
					"fields in sscanf\n");
			continue;
		}

		if (p->niface) { /* ifaces list was given in config */
			if (bsearch(&curriface, p->iface, p->niface,
				    sizeof(p->iface[0]), strpcmp)) {
				goto rec;
			}
			/* not in the ifaces list */
			continue;
		}
	rec:
		/* must check if `curriface` is excluded */
		if (p->nexcludes) {
			if (bsearch(&curriface, p->excludes, p->nexcludes,
				    sizeof(p->excludes[0]), strpcmp)) {
				continue;
			}
		}
		rec_inst = ldms_record_alloc(p->base_data->set, p->rec_def_idx);
		if (!rec_inst)
			goto resize;
		/* iface name */
		name_mval = ldms_record_metric_get(rec_inst, rec_metric_ids[0]);
		snprintf(name_mval->a_char, IFNAMSIZ, "%s", curriface);
		/* metrics */
		for (i = 1; i < REC_METRICS_LEN; i++) {
			ldms_record_set_u64(rec_inst, rec_metric_ids[i], v[i].v_u64);
		}
		ldms_list_append_record(p->base_data->set, lh, rec_inst);
	} while (s);

	base_sample_end(p->base_data);
	return 0;
resize:
	/*
	 * We intend to leave the set in the inconsistent state so that
	 * the aggregators are aware that some metrics have not been newly sampled.
	 */
	heap_sz = ldms_set_heap_size_get(p->base_data->set) + 2*p->rec_heap_sz;
	base_set_delete(p->base_data);
	base_set_new_heap(p->base_data, heap_sz);
	if (!p->base_data->set) {
		rc = errno;
		ovis_log(mylog, OVIS_LCRITICAL, SAMP " : Failed to create a set with "
						"a bigger heap. Error %d\n", rc);
		return rc;
	}
	goto begin;
}


static void term(struct ldmsd_plugin *self)
{
	procnetdev2_t p = (void*)self;
	if (p->mf) {
		fclose(p->mf);
		p->mf = NULL;
	}
	p->mf = NULL;
	base_set_delete(p->base_data);
	base_del(p->base_data);
	p->base_data = NULL;

	if (p->iface) {
		free(p->iface);
		p->iface = NULL;
	}
	if (p->iface_str) {
		free(p->iface_str);
		p->iface_str = NULL;
	}
	p->niface = 0;
}


static void __procnetdev2_del(struct ldmsd_cfgobj *self)
{
	procnetdev2_t p = (void*)self;
	free(p);
}

static void __once()
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&mutex);
	if (mylog)
		goto out;
	mylog = ovis_log_register("sampler."SAMP, "Message for the " SAMP " plugin");
	if (!mylog) {
		ovis_log(NULL, OVIS_LWARN, "Failed to create the log subsystem "
					"of '" SAMP "' plugin. Error %d\n", errno);
	}
 out:
	pthread_mutex_unlock(&mutex);
}

struct ldmsd_plugin *get_plugin_instance(const char *name,
					 uid_t uid, gid_t gid, int perm)
{
	procnetdev2_t p;

	__once();

	p = (void*)ldmsd_sampler_alloc(name, sizeof(*p), __procnetdev2_del,
				       uid, gid, perm);
	if (!p)
		return NULL;

	snprintf(p->plugin.name, sizeof(p->plugin.name), "%s", SAMP);
	p->plugin.term    = term;
	p->plugin.config  = config;
	p->plugin.usage   = usage;

	p->sampler.sample = sample;

	return &p->plugin;
}
