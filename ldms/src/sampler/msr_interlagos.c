/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
/**
 * \file msr_interlagos.c
 * \brief msr data provider for interlagos only
 *
 * Sets/checks/ and reads msr counters for interlagos only
 *
 */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef __linux__ // modern linux provides either bitness in asm/unistd.h as needed
#if defined(__i386__)
#include "/usr/include/asm/unistd.h"
#endif

#if defined(__x86_64__)
#include "/usr/include/asm-x86_64/unistd.h"
#endif
#else // __linux__
#include <asm/unistd.h>
#endif // __linux__


#define MSR_MAXLEN 20LL
#define MSR_ARGLEN 4LL
#define MSR_HOST 0LL
#define MSR_CNT_MASK 0LL
#define MSR_INV 0LL
#define MSR_EDGE 0LL
#define MSR_ENABLE 1LL
#define MSR_INTT 0LL
#define MSR_TOOMANYMAX 100LL
#define MSR_CONFIGLINE_MAX 1024

typedef enum{CTR_OK, CTR_HALTED, CTR_BROKEN} ctr_state;
typedef enum{CFG_PRE, CFG_DONE_INIT, CFG_IN_FINAL, CFG_FAILED_FINAL, CFG_DONE_FINAL} ctrcfg_state;
typedef enum{CTR_UNCORE, CTR_NUMCORE} ctr_num_values;

static pthread_mutex_t cfglock;

struct active_counter{
	struct MSRcounter* mctr;
	uint64_t wctl;
	int valid; //this is kept track of, but currently unused
	ctr_state state;
	int metric_ctl;
	int metric_ctr;
	int metric_name;
	int ndata;
	uint64_t* data;
	pthread_mutex_t lock;
	TAILQ_ENTRY(active_counter) entry;
};

TAILQ_HEAD(, active_counter) counter_list;

struct MSRcounter{
	char* name;
	uint64_t w_reg;
	uint64_t event;
	uint64_t umask;
	uint64_t r_reg;
	uint64_t os_user;
	uint64_t int_core_ena;
	uint64_t int_core_sel;
	char* core_flag;
	ctr_num_values numvalues_type;
	int numcore; /* max legit core vals will consider (was numvals) */
	int offset; /* offsets for core and uncore counters */
	int maxcore; /* allows for extra zeros */
};

/*
//These now come from a config file. NOTE: not the last 3 nums.

static struct MSRcounter counter_assignments[] = {
	{"TOT_CYC", 0xc0010202, 0x076, 0x00, 0xc0010203, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"TOT_INS", 0xc0010200, 0x0C0, 0x00, 0xc0010201, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"L2_DCM",  0xc0010202, 0x043, 0x00, 0xc0010203, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"L1_DCM",  0xc0010204, 0x041, 0x01, 0xc0010205, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"DP_OPS",  0xc0010206, 0x003, 0xF0, 0xc0010207, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"VEC_INS", 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"TLB_DM",  0xc001020A, 0x046, 0x07, 0xc001020B, 0b11, "-a", CTR_NUMCORE, 0, 0, 0},
	{"L3_CACHE_MISSES", 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0b0, "", CTR_UNCORE, 0, 0, 0},
	{"DCT_PREFETCH", 0xc0010242, 0x1F0, 0x02, 0xc0010243, 0b0, "", CTR_UNCORE, 0, 0, 0},
	{"DCT_RD_TOT", 0xc0010244, 0x1F0, 0x01, 0xc0010245, 0b0, "", CTR_UNCORE, 0, 0, 0},
	{"DCT_WRT", 0xc0010246, 0x1F0, 0x00, 0xc0010247, 0b0, "", CTR_UNCORE, 0, 0, 0}
};
*/

static struct MSRcounter* counter_assignments = NULL;
static char** initnames = NULL;
static int msr_numoptions = 0;
static int numinitnames = 0;
static int numcore = 0;
static int maxcore = 0;


/**
 * NOTE:
 * 1) still subject to race conditions if the user zero's or changes the registers back and forth between our grab
 * 2) since we iterate thru we select a little staggered in time cpu to cpu
 *
 * CONFIGURATION/INTERACTION:
 * a) users must supply the configuration file with the counter assignments.
 *    incompatible pairs are discovered.
 * b) users will add each they want one separately
 * c) users can swap out a var with one of the same size. syntax? name of the one to swap.
 * d) users will also say which one to halt/continue
 *
 * LOCKS:
 * 1) cant call write (change the var or zero the reg) the register while we are reading the register.
 * 2) cant read into the register while we are working with the counter data (print fctn right now.
 *    note we could save them while we are reading them)
 * 3) cant change the intended variable while we are reading or writing the data
 *
 * TODO:
 * 1) could read different ctr independently (threads)
 */

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name = NULL;
static char *instance_name = NULL;
static char *schema_name = NULL;
static ldms_schema_t schema;
static uint64_t comp_id;
#define SAMP "msr_interlagos"
static char *default_schema_name = SAMP;

static int metric_offset = 1;
LJI_GLOBALS;

static ctrcfg_state cfgstate = CFG_PRE;


static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=" SAMP " action=initialize producer=<prod_name> instance=<inst_name> [component_id=<comp_id> schema=<sname> with_jobid=<jid>] maxcore=<maxcore> corespernuma=<corespernuma> conffile=<cfile>\n"
		"            - Initialization activities for the set. Does not create it.\n"
		"            producer        - The producer name\n"
		"            instance        - The instance name\n"
		"            component_id    - Optional unique number identifier. Also that for\n"
		"                              the timermetric. Defaults to zero.\n"
		"            maxcore         - max cores that will be reported for all counters.\n"
		"                              If unspecified, it will use the actual number of cores.\n"
		"                              If specified N must be >= actual numcores. This will\n"
		"                              report 0 as values any N > actual numcores\n"
		"            corespernuma    - num cores per numa domain (used for uncore counters)\n"
		"            conffile        - configuration file with the counter assignment options\n"
		LJI_DESC
		"            schema          - Optional schema name. Defaults to '" SAMP "'\n"
		"\n"
		"    config name=msr_interlagos action=add metricname=<name>\n"
		"            - Adds a metric. The order they are issued are the ordered they are added\n"
		"            metricname      - The metric name for the event\n"
		"\n"
		"    config name=msr_interlagos action=finalize\n"
		"            - Creates the set when called after all adds.\n"
		"\n"
		"    config name=msr_interlagos action=ls\n"
		"            - List the currently configured events.\n"
		"\n"
		"    config name=msr_interlagos action=halt metricname=<name>\n"
		"            metricname      - The metric name for the event to halt.\n"
		"                              metricname=all halts all\n"
		"\n"
		"    config name=msr_interlagos action=continue metricname=<name>\n"
		"            metricname      - The metric name for the event to continue (once halted)\n"
		"                              metricname=all\n"
		"\n"
		"    config name=msr_interlagos action=reassign oldmetricname=<oldname> newmetricname=<newname>\n"
		"            oldmetricname   - The metric name for the event to swap out\n"
		"            newmetricname   - The metric name for the event to swap in\n"
		"\n"
		"    config name=msr_interlagos action=rewrite metricname=<name>\n"
		"            metricname      - The metric name for the event to rewrite\n"
		"                              metricname=all rewrites all\n"
		;
}

static int parseConfig(char* fname){
	char name[MSR_MAXLEN];
	uint64_t w_reg;
	uint64_t event;
	uint64_t umask;
	uint64_t r_reg;
	uint64_t os_user;
	uint64_t int_core_ena;
	uint64_t int_core_sel;
	char core_flag[MSR_ARGLEN];
	char temp[MSR_MAXLEN];
	ctr_num_values numvalues_type;

	char lbuf[MSR_CONFIGLINE_MAX];
	char* s;
	int rc;
	int i, count;

	FILE *fp = fopen(fname, "r");
	if (!fp){
		msglog(LDMSD_LERROR, "%s: Cannot open config file <%s>\n",
		       __FILE__, fname);
		return EINVAL;
	}

	count = 0;
	//parse once to count
	do  {

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
		//rc = sscanf(lbuf, "%[^,], %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %[^,], %[^,]",
		rc = sscanf(lbuf, "%[^,],%llx,%llx,%llx,%llx,%llx,%llx,%llx,%[^,],%s",
			    name, &w_reg, &event, &umask, &r_reg, &os_user, &int_core_ena, &int_core_sel, core_flag, temp);
		if ((strlen(name) > 0)  && (name[0] == '#')){
			msglog(LDMSD_LDEBUG, "Comment in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		if (rc != 10){
			msglog(LDMSD_LDEBUG, "Bad format in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		msglog(LDMSD_LDEBUG, "msr config fields: <%s> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%"PRIu64 "> <%s> <%s>\n",
		       name, w_reg, event, umask, r_reg, os_user, int_core_ena, int_core_sel, core_flag, temp);


		if ((strcmp(temp, "CTR_UNCORE") != 0) && (strcmp(temp, "CTR_NUMCORE") != 0)){
			msglog(LDMSD_LDEBUG,"Bad type in msr config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		count++;
	} while (s);

	counter_assignments = (struct MSRcounter*)malloc(count*sizeof(struct MSRcounter));
	if (!counter_assignments){
		fclose(fp);
		return ENOMEM;
	}
	//let the user add up to this many names as well
	initnames = (char**)malloc(count*sizeof(char*));
	if (!initnames){
		free(counter_assignments);
		fclose(fp);
		return ENOMEM;
	}

	//parse again to fill
	fseek(fp, 0, SEEK_SET);
	i = 0;
	do  {

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;

		//rc = sscanf(lbuf, "%[^,], %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %"PRIu64 ", %[^,], %[^,]",
		rc = sscanf(lbuf, "%[^,],%llx,%llx,%llx,%llx,%llx,%llx,%llx,%[^,],%s",
			    name, &w_reg, &event, &umask, &r_reg, &os_user, &int_core_ena, &int_core_sel, core_flag, temp);
		if ((strlen(name) > 0) && (name[0] == '#')){
			continue;
		}
		if (rc != 10){
			continue;
		}
		if ((strcmp(temp, "CTR_UNCORE") == 0)) {
			numvalues_type = CTR_UNCORE;
		} else if ((strcmp(temp, "CTR_NUMCORE") == 0)) {
			numvalues_type = CTR_NUMCORE;
		} else {
			continue;
		}

		if (i == count){
			msglog(LDMSD_LERROR, "Changed number of valid entries from first pass. aborting.\n");
			free(counter_assignments);
			free(initnames);
			return EINVAL;
		}

		counter_assignments[i].name = strdup(name);
		counter_assignments[i].w_reg = w_reg;
		counter_assignments[i].event = event;
		counter_assignments[i].umask = umask;
		counter_assignments[i].r_reg = r_reg;
		counter_assignments[i].os_user = os_user;
		counter_assignments[i].int_core_ena = int_core_ena;
		counter_assignments[i].int_core_sel = int_core_sel;
		counter_assignments[i].core_flag = strdup(core_flag);
		counter_assignments[i].numvalues_type = numvalues_type;
		counter_assignments[i].numcore = 0; //this will get filled in later
		counter_assignments[i].offset = 0; //this will get filled in later
		counter_assignments[i].maxcore = 0; //this will get filled in later

		i++;
	} while (s);
	fclose(fp);

	msr_numoptions = i;

	return 0;
}

static void _free_names(){
	if (producer_name) free(producer_name);
	producer_name = NULL;
	if (schema_name) free(schema_name);
	schema_name = NULL;
	if (instance_name) free(instance_name);
	instance_name = NULL;
}

//the calling function needs to get the cfg lock
struct active_counter* findactivecounter(char* name){
	struct active_counter* pe;

	if ((name == NULL) || (strlen(name) == 0)){
		return NULL;
	}

	TAILQ_FOREACH(pe, &counter_list, entry){
		if (strcmp(name, pe->mctr->name) == 0){
			return pe;
		}
	}

	return NULL;
}


static int halt(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg)
{
	struct active_counter* pe;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for halting events <%d>\n", cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		msglog(LDMSD_LERROR, SAMP ": no name to halt\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			switch(pe->state){
			case CTR_OK:
				//will halt. make everything we have invalid now
				pe->valid = 0;
				pe->state = CTR_HALTED;
				break;
			default:
				//do nothing
				break;
			}
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			msglog(LDMSD_LERROR, SAMP ": cannot find <%s> to halt\n", value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		switch(pe->state){
		case CTR_OK:
			//will halt. make everything we have invalid now
			pe->valid = 0;
			pe->state = CTR_HALTED;
			break;
		default:
			//do nothing
			break;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return 0;

}


static int cont(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg)
{
	struct active_counter* pe;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for continuing events <%d>\n", cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		msglog(LDMSD_LERROR, SAMP ": no name to continue\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			switch(pe->state){
			case CTR_HALTED:
				pe->state = CTR_OK;
				break;
			default:
				//do nothing
				break;
			}
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			msglog(LDMSD_LERROR, SAMP ": cannot find <%s> to continue\n", value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		switch(pe->state){
		case CTR_HALTED:
			pe->state = CTR_OK;
			break;
		default:
			//do nothing
			break;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return 0;
}


int writeregistercpu(uint64_t x_reg, int cpu, uint64_t val){

	char fname[MSR_MAXLEN];
	uint64_t dat;
	int fd;

	snprintf(fname, MSR_MAXLEN-1, "/dev/cpu/%d/msr", cpu);
	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		int errsv = errno;
		msglog(LDMSD_LERROR, SAMP ": writeregistercpu cannot open fd=<%d> for cpu %d errno=<%d>",
		       fd, cpu, errsv);
		return -1;
	}

	dat = val;
	if (pwrite(fd, &dat, sizeof dat, x_reg) != sizeof dat) {
		int errsv = errno;
		msglog(LDMSD_LERROR, SAMP ": writeregistercpu cannot pwrite MSR 0x%08" PRIx64
		       " to 0x%016" PRIx64 " for cpu %d errno=<%d>\n",
		       x_reg, dat, cpu, errsv);
		return -1;
	}

	close(fd);

	return 0;
}


int readregistercpu(uint64_t x_reg, int cpu, uint64_t* val){

	char fname[MSR_MAXLEN];
	uint64_t dat;
	int fd;

	snprintf(fname, MSR_MAXLEN-1, "/dev/cpu/%d/msr", cpu);
	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		int errsv = errno;
		msglog(LDMSD_LERROR, SAMP ": readregistercpu cannot open fd=<%d> for cpu %d errno=<%d>",
		       fd, cpu, errsv);
		return -1;
	}

	if (pread(fd, &dat, sizeof dat, x_reg) != sizeof dat) {
		int errsv = errno;
		msglog(LDMSD_LERROR, SAMP ": readregistercpu cannot pread MSR 0x%08" PRIx64
		       " for cpu %d errno=<%d>\n",
		       x_reg, cpu, errsv);
		close(fd);
		return -1;
	}

	close(fd);
	*val = dat;

	return 0;
}


static int zerometricset( struct active_counter *pe){
	union ldms_value v;
	char* str = NULL;
	int i;
	int rc;

	//populate the metrics with zero values
	if (pe == NULL){
		return -1;
	}

	v.v_u64 = 0;
	ldms_metric_set(set, pe->metric_ctl, &v);
	ldms_metric_array_set_str(set, pe->metric_name, str);
	for (i = 0; i < pe->ndata; i++){
		ldms_metric_array_set_val(set, pe->metric_ctr, i, &v);
	}

	pe->valid = 0; //invalidates
	return 0;
}


static int readregisterguts( struct active_counter *pe){
	union ldms_value v;
	int i, j;
	int rc;

	if (pe == NULL){
		return -1;
	}
	j = 0;
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){ //will only read what's there (numcore)
		//NOTE: possible race condition if the register changes while reading through.
		rc = readregistercpu(pe->mctr->r_reg, i, &(pe->data[j]));
		if (rc != 0){
			//if any of them fail, invalidate all
			zerometricset(pe);
			return rc;
		}
		j++;
	}
	v.v_u64 = pe->wctl;
	ldms_metric_set(set, pe->metric_ctl, &v);
	ldms_metric_array_set_str(set, pe->metric_name, pe->mctr->name);
	for (j = 0; j < pe->ndata; j++){ //set all of them, which will include the padding.
		v.v_u64 = pe->data[j];
		ldms_metric_array_set_val(set, pe->metric_ctr, j, &v);
	}

	pe->valid = 1;
	return 0;

}


static int checkregister( struct active_counter *pe){

	int i;
	uint64_t val;
	int rc;

	if (pe == NULL){
		return -1;
	}

	//read the val(s) and make sure they match wctl
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = readregistercpu(pe->mctr->w_reg, i, &val);
		if (rc != 0){
			msglog(LDMSD_LERROR, SAMP ": <%s> readregistercpu bad %d\n", pe->mctr->name, rc);
			return rc;
		}
		//              printf("Comparing %llx to %llx\n", val, pe->wctl);
		if (val != pe->wctl){
			msglog(LDMSD_LDEBUG, SAMP ": Register changed! read <%llx> want <%llx>\n", val, pe->wctl);
			return -1;
			break;
		}
	}

	return 0;
}


static int readregister(struct active_counter *pe){
	int rc;

	if (pe == NULL){
		return -1;
	}

	switch (pe->state){
	case CTR_HALTED:
		msglog(LDMSD_LDEBUG, SAMP ": %s Halted. Register will not be read.\n",
		       pe->mctr->name);
		//invalidate the current vals because this is an invalid read. (but this will have already been done as part of the halt)
		zerometricset(pe); //these sets zero values in the metric set
		rc = 0;
		break;
	case CTR_OK:
		//check all of them first
		rc = checkregister(pe);
		if (rc != 0){
			msglog(LDMSD_LDEBUG, SAMP ": Control register for %s has changed. Register will not be read.\n",
			       pe->mctr->name);
			//invalidate the current vals because this is an invalid read.
			zerometricset(pe); //these sets zero values in the metric set
			// we are ok with this
			rc = 0;
		} else {
			//then read all of them
			rc = readregisterguts(pe); //this invalidates if fails. this is an invalid read. this sets values in the metric set
			if (rc != 0){
				msglog(LDMSD_LERROR, SAMP ": Read register failed %s\n", pe->mctr->name);
				// we are not ok with this. do not change rc
			}
		}
		break;
	default:
		msglog(LDMSD_LDEBUG, SAMP ": register state <%d>. Wont read\n", pe->state);
		rc = 0;
		break;
	}

	return rc;

}


static ctr_state writeregister(struct active_counter *pe){

	int i;
	int rc;

	if (pe == NULL){
		return CTR_BROKEN;
	}

	pe->valid = 0;
	pe->state = CTR_BROKEN;
	//Zero the ctrl register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->w_reg, i, 0);
		if (rc != 0){
			return pe->state;
		}
	}
	//Zero the val register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->r_reg, i, 0);
		if (rc != 0){
			return pe->state;
		}
	}
	//Write the ctrl register
	for (i = 0; i < pe->mctr->numcore; i+=pe->mctr->offset){
		rc = writeregistercpu(pe->mctr->w_reg, i, pe->wctl);
		if (rc != 0){
			return pe->state;
		}
	}

	pe->state = CTR_OK;
	return pe->state;
}

static int checkreassigncounter(struct active_counter *rpe, int idx){
	struct active_counter* pe;

	if (rpe == NULL) {
		return -1;
	}

	//validity. compare with all the others
	TAILQ_FOREACH(pe, &counter_list, entry){
		if (pe != rpe){
			//duplicates are ok
			if (rpe->mctr->w_reg == pe->mctr->w_reg){
				if (strcmp(rpe->mctr->name, pe->mctr->name) == 0){
					//duplicates are ok
					msglog(LDMSD_LINFO, SAMP ": Notify - Duplicate assignments! <%s>\n",
					       rpe->mctr->name);
				} else {
					return -1;
				}
			}
		}
	}

	//size check: can only reassign to a space of the same size
	if (rpe->mctr->numcore != counter_assignments[idx].numcore){
		return -1;
	}
	//offset check: can only reassign to the same offset (for the data array)
	if (rpe->mctr->offset != counter_assignments[idx].offset){
		return -1;
	}

	return 0;
}


static int rewrite(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg){
	struct active_counter* pe;
	ctr_state s;
	char* value;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for rewriting events <%d>\n", cfgstate);
		return -1;
	}

	value = av_value(avl, "metricname");
	if (!value){
		msglog(LDMSD_LERROR, SAMP ": no name to rewrite\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	if (strcmp(value, "all") == 0){
		TAILQ_FOREACH(pe, &counter_list, entry){
			s = writeregister(pe);
			if (s != CTR_OK){
				msglog(LDMSD_LERROR, SAMP ": cannot rewrite register <%s>\n", value);
				//but will continue;
			}
			pthread_mutex_unlock(&pe->lock);
		}
	} else {
		pe = findactivecounter(value);
		if (pe == NULL){
			msglog(LDMSD_LERROR, SAMP ": cannot find <%s> to rewrite\n", value);
			pthread_mutex_unlock(&cfglock);
			return -1;
		}

		pthread_mutex_lock(&pe->lock);
		s = writeregister(pe);
		if (s != CTR_OK){
			msglog(LDMSD_LERROR, SAMP ": cannot rewrite register <%s>\n", value);
			//but will continue;
		}
		pthread_mutex_unlock(&pe->lock);
	}

	pthread_mutex_unlock(&cfglock);
	return 0;
}

int assigncounter(struct active_counter* pe, int j);

static struct active_counter* reassigncounter(char* oldname, char* newname) {
	struct active_counter *pe;
	int j;
	int rc;

	if ((oldname == NULL) || (newname == NULL) ||
	    (strlen(oldname) == 0) || (strlen(newname) == 0)){
		msglog(LDMSD_LERROR, SAMP ": Invalid args to reassign counter\n");
		return NULL;
	}

	int idx = -1;
	for (j = 0; j < msr_numoptions; j++){
		if (strcmp(newname, counter_assignments[j].name) == 0){
			idx = j;
			break;
		}
	}
	if (idx < 0){
		msglog(LDMSD_LERROR, SAMP ": No counter <%s> to reassign to\n", newname);
		return NULL;
	}

	pthread_mutex_lock(&cfglock);
	pe = findactivecounter(oldname);
	if (pe == NULL){
		msglog(LDMSD_LERROR, SAMP ": Cannot find counter <%s> to replace\n", oldname);
		pthread_mutex_unlock(&cfglock);
		return NULL;
	} else {
		rc = checkreassigncounter(pe, idx);
		if (rc != 0){
			msglog(LDMSD_LERROR, SAMP ": Reassignment of <%s> to <%s> invalid\n",
			       oldname, newname);
			pthread_mutex_unlock(&cfglock);
			return NULL;
		}

		rc = assigncounter(pe, idx);
		if (rc != 0){
			pthread_mutex_unlock(&cfglock);
			return NULL;
		}
	}

	pthread_mutex_unlock(&cfglock);
	return pe;
}


static int reassign(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg){
	struct active_counter* pe;
	ctr_state s;
	char* ovalue;
	char* nvalue;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for reassigning events <%d>\n", cfgstate);
		return -1;
	}

	ovalue = av_value(avl, "oldmetricname");
	if (!ovalue){
		msglog(LDMSD_LERROR, SAMP ": no name to rewrite\n");
		return -1;
	}

	nvalue = av_value(avl, "newmetricname");
	if (!nvalue){
		msglog(LDMSD_LERROR, SAMP ": no name to rewrite to\n");
		return -1;
	}

	pthread_mutex_lock(&cfglock);
	pe = reassigncounter(ovalue, nvalue);
	if (pe == NULL){
		msglog(LDMSD_LERROR, SAMP ": cannot reassign counter\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	pthread_mutex_unlock(&cfglock);
	return 0;

}


static int add_event(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg){
	int idx;
	char* nam;
	char* val;
	int i;

	pthread_mutex_lock(&cfglock);
	if (cfgstate != CFG_DONE_INIT){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for adding events <%d>\n", cfgstate);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	//add an event to the list to be parsed
	if (numinitnames == msr_numoptions){
		msglog(LDMSD_LERROR, SAMP ": Trying to add too many events\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	nam = av_value(avl, "metricname");
	if ((!nam) || (strlen(nam) == 0)){
		msglog(LDMSD_LERROR, SAMP ": Invalid event name\n");
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	idx = -1;
	for (i = 0; i < msr_numoptions; i++){
		if (strcmp(nam, counter_assignments[i].name) == 0){
			idx = i;
			break;
		}
	}
	if (idx < 0){
		msglog(LDMSD_LERROR, SAMP ": Non-existent event name <%s>\n", nam);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	initnames[numinitnames] = strdup(nam);
	if (!initnames[numinitnames]){
		pthread_mutex_unlock(&cfglock);
		return ENOMEM;
	}

	msglog(LDMSD_LDEBUG, SAMP ": Added event name <%s>\n", nam);

	numinitnames++;
	pthread_mutex_unlock(&cfglock);

	return 0;

}

static int checkcountersinit(){ //this will only be called once, from finalize
	//get the lock outside of this
	//check for conflicts. check no conflicting w_reg
	int i, j, ii, jj;

	//Duplicates are OK but warn. this lets you swap.
	for (i = 0; i < numinitnames; i++){
		int imatch = -1;
		for (ii = 0; ii < msr_numoptions; ii++){
			if (strcmp(initnames[i], counter_assignments[ii].name) == 0){
				imatch == ii;
				break;
			}
		}
		if (imatch < 0)
			continue;

		for (j = 0; j < numinitnames; j++){
			int jmatch = -1;
			if (i == j)
				continue;

			for (jj = 0; jj < msr_numoptions; jj++){
				if (strcmp(initnames[j], counter_assignments[jj].name) == 0){
					jmatch == jj;
				}
			}
			if (jmatch < 0)
				continue;

			//do they have the same wregs if they are not the same name?
			if (counter_assignments[imatch].w_reg == counter_assignments[jmatch].w_reg){
				if (strcmp(initnames[i], initnames[j]) == 0){
					//this is ok
					msglog(LDMSD_LINFO,
					       SAMP ": Notify - Duplicate assignments! <%s>\n",
					       initnames[i]);
				} else {
					msglog(LDMSD_LERROR,
					       SAMP ": Cannot have conflicting counter assignments <%s> <%s>\n",
					       initnames[i], initnames[j]);
					return -1;
				}
			}
		}
	}

	return 0;
}


static int list(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct active_counter* pe;

	msglog(LDMSD_LINFO,"%-24s %10x %10x %10xs\n",
	       "Name", "wreg", "wctl", "rreg");
	msglog(LDMSD_LINFO,"%-24s %10s %10s %10s\n",
	       "------------------------",
	       "----------", "----------", "----------");
	pthread_mutex_lock(&cfglock);
	TAILQ_FOREACH(pe, &counter_list, entry) {
		msglog(LDMSD_LINFO,"%-24s %10x %10x %10x\n",
		       pe->mctr->name, pe->mctr->w_reg, pe->wctl,
		       pe->mctr->r_reg);
	}
	pthread_mutex_unlock(&cfglock);
	return 0;
}

static int dfilter(const struct dirent *dp){
	return ((isdigit(dp->d_name[0])) ? 1 : 0);
}

// numcore, maxcore, and corespernuma are all assigned by the end of this
static int init(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	struct dirent **dlist;
	char* val;
	char* cfile;
	int rc;
	int i;

	if (cfgstate != CFG_PRE){
		msglog(LDMSD_LERROR, SAMP ": cannot reinit");
		return -1;
	}

	val = av_value(avl, "producer");
	if (!val) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}
	producer_name = strdup(val);

	val = av_value(avl, "component_id");
	if (!val){
		comp_id = 0;
	} else {
		comp_id = strtoull(val, NULL, 0);
	}

	LJI_CONFIG(val, avl);

	val = av_value(avl, "instance");
	if (!val) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		_free_names();
		return ENOENT;
	}
	instance_name = strdup(val);

	val = av_value(avl, "schema");
	if (!val)
		val = default_schema_name;
	if (strlen(val) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		_free_names();
		return EINVAL;
	}
	schema_name = strdup(val);

	cfile = av_value(avl, "conffile");
	if (!cfile){
		msglog(LDMSD_LERROR, SAMP ": no config file");
		_free_names();
		return EINVAL;
	} else {
		rc = parseConfig(cfile);
		if (rc != 0){
			msglog(LDMSD_LERROR, SAMP ": error parsing config file. Aborting\n");
			_free_names();
			return rc;
		}
	}

	//get the actual number of counters = num entries like /dev/cpu/%d
	numcore = scandir("/dev/cpu", &dlist, dfilter, 0);
	if (numcore < 1){
		msglog(LDMSD_LERROR, SAMP ": cannot get numcore\n");
		_free_names();
		return -1;
	}
	for (i = 0; i < numcore; i++){
		free(dlist[i]);
	}
	free(dlist);

	pthread_mutex_lock(&cfglock);

	maxcore = numcore;
	val = av_value(avl, "maxcore");
	if (val){
		maxcore = atoi(val);
		if ((maxcore < numcore) || (maxcore > MSR_TOOMANYMAX)){ //some big number. just a safety check.
			msglog(LDMSD_LERROR, SAMP ": maxcore %d invalid\n",
			       maxcore);
			_free_names();
			pthread_mutex_unlock(&cfglock);
			return -1;
		}
	}

	int corespernuma = 1;
	val = av_value(avl, "corespernuma");
	if (val){
		corespernuma = atoi(val);
		if ((corespernuma < 1) || (corespernuma > MSR_TOOMANYMAX)){ //some big number. just a safety check.
			msglog(LDMSD_LERROR, SAMP ": corespernuma %d invalid\n",
			       maxcore);
			_free_names();
			pthread_mutex_unlock(&cfglock);
			return -1;
		}
	} else {
		msglog(LDMSD_LERROR, SAMP ": must specify corespernuma\n");
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return -1;
	}


	for (i = 0; i < msr_numoptions; i++){
		counter_assignments[i].numcore = numcore;
		counter_assignments[i].maxcore = maxcore;
		if (counter_assignments[i].numvalues_type == CTR_NUMCORE){
			counter_assignments[i].offset = 1;
		} else {
			counter_assignments[i].offset = corespernuma;
		}
	}

	pthread_mutex_unlock(&cfglock);

	numinitnames = 0;
	cfgstate = CFG_DONE_INIT;

	return 0;
}


int assigncounter(struct active_counter* pe, int j){ //includes the write
	uint64_t junk;
	int i;

	int init = 0;
	if (pe == NULL){ //if already init, dont want to mess up the metric ptrs
		init = 1;
	}

	if (init){
		pe = calloc(1, sizeof *pe);
		if (!pe){
			return ENOMEM;
		}
		pthread_mutex_init(&(pe->lock), NULL);
	}
	pthread_mutex_lock(&(pe->lock));


	pe->mctr = &counter_assignments[j];
	pe->valid = 0;
	if (init){
		int nval = (pe->mctr->maxcore)/(pe->mctr->offset); //unlike v2, array will include the padding
		pe->data = calloc(nval, sizeof(junk));
		if (!pe->data){
			pthread_mutex_unlock(&(pe->lock));
			return ENOMEM;
		} //wont need to zero out otherwise since valid = 0;

		pe->metric_ctl = 0;
		pe->metric_ctr = 0;
		pe->ndata = nval; //some of these may be padding
	}

	//WRITE COMMAND
	uint64_t w_reg = counter_assignments[j].w_reg;
	uint64_t event_hi = counter_assignments[j].event >> 8;
	uint64_t event_low = counter_assignments[j].event & 0xFF;
	uint64_t umask = counter_assignments[j].umask;
	uint64_t os_user = counter_assignments[j].os_user;
	uint64_t int_core_ena = counter_assignments[j].int_core_ena;
	uint64_t int_core_sel = counter_assignments[j].int_core_sel;


//	pe->wctl = MSR_HOST << 40 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask << 8 | event_low;
	pe->wctl = MSR_HOST << 40 | int_core_sel << 37 | int_core_ena << 36 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask << 8 | event_low;
	//        printf("%s: 0x%llx, 0x%llx, 0x%llx 0x%llx\n",
	//               pe->mctr->name, event_hi, event_low, umask, pe->wctl);

	msglog(LDMSD_LINFO, "WRITECMD: writeregister(0x%llx, %d, 0x%llx)\n", w_reg, (pe->mctr->numcore)/(pe->mctr->offset), pe->wctl);

	//CHECK COMMAND
	msglog(LDMSD_LINFO, "CHECKCMD: readregister(0x%llx, %d)\n", w_reg, (pe->mctr->numcore)/(pe->mctr->offset));

	//READ COMMAND
	msglog(LDMSD_LINFO, "READCMD: readregister(0x%llx, %d)\n", pe->mctr->r_reg, (pe->mctr->numcore)/(pe->mctr->offset));

	pe->state = CTR_BROKEN; //until written

	writeregister(pe); //this will reset the state

	if (init){ //cfglock held outside
		TAILQ_INSERT_TAIL(&counter_list, pe, entry);
	}

	pthread_mutex_unlock(&(pe->lock));

	return 0;

}


static int assigncountersinit(){
	int i, j;
	int rc;


	TAILQ_INIT(&counter_list);

	for (i = 0; i < numinitnames; i++){
		int found = -1;
		for (j = 0; j < msr_numoptions; j++){
			if (strcmp(initnames[i], counter_assignments[j].name) == 0){
				rc = assigncounter(NULL, j);
				if (rc != 0){
					return rc;
				}
				found = 1;
				break;
			}
		}
		if (found == -1){
			msglog(LDMSD_LERROR, SAMP ": Bad init counter name <%s>\n", initnames[i]);
			return -1;
		}
	}

	return 0;
}


static int finalize(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{

	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	struct active_counter* pe;
	char name[MSR_MAXLEN];
	union ldms_value v;
	int rc;
	int i, j, k;

	pthread_mutex_lock(&cfglock);
	msglog(LDMSD_LDEBUG, SAMP ": finalizing\n");

	if (cfgstate != CFG_DONE_INIT){
		_free_names();
		pthread_mutex_unlock(&cfglock);
		msglog(LDMSD_LERROR, SAMP ": in wrong state to finalize <%d>", cfgstate);
		return -1;
	}

	cfgstate = CFG_IN_FINAL;

	/* do any checking */
	rc = checkcountersinit();
	if (rc < 0){
		//in theory can do more add's but there is currently no way to remove the conflicting ones
		cfgstate = CFG_FAILED_FINAL;
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	//after this point can no longer add more counters
	rc = assigncountersinit();
	if (rc < 0){
		cfgstate = CFG_FAILED_FINAL;
		_free_names();
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	for (i = 0; i < numinitnames; i++){
		free(initnames[i]);
	}
	numinitnames = 0;

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0){
		rc = ENOMEM;
		goto err;
	}

	metric_offset++;
	rc = LJI_ADD_JOBID(schema);
	if (rc < 0){
		goto err;
	}

	/* add the metrics */
	i = 0;
	TAILQ_FOREACH(pe, &counter_list, entry) {
		snprintf(name, MSR_MAXLEN, "Ctr%d", i);
		rc = ldms_schema_metric_add(schema, name, LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_ctl = rc;

		//new for v3: store the name as well. FIXME: decide if we will keep this.
		snprintf(name, MSR_MAXLEN, "Ctr%d_name", i);
		rc = ldms_schema_metric_array_add(schema, name, LDMS_V_CHAR_ARRAY, MSR_MAXLEN);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_name = rc;

		//process the real ones and the padded ones
		if (pe->mctr->numvalues_type == CTR_NUMCORE){
			snprintf(name, MSR_MAXLEN, "Ctr%d_c", i);
		} else {
			snprintf(name, MSR_MAXLEN, "Ctr%d_n", i);
		}
		rc = ldms_schema_metric_array_add(schema, name, LDMS_V_U64_ARRAY, pe->ndata);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		pe->metric_ctr = rc;
		i++;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	ldms_set_producer_name_set(set, producer_name);
	_free_names();

	//add specialized metrics
	v.v_u64 = comp_id;
	ldms_metric_set(set, 0, &v);

	LJI_SAMPLE(set,1);

	cfgstate = CFG_DONE_FINAL;
	pthread_mutex_unlock(&cfglock);
	return 0;

 err:
	msglog(LDMSD_LERROR, SAMP ": failed finalize\n");
	cfgstate = CFG_FAILED_FINAL;
	_free_names();
	if (schema) ldms_schema_delete(schema);
	schema = NULL;
	pthread_mutex_unlock(&cfglock);
	if (set)
		ldms_set_delete(set);
	set = NULL;
	return rc;

}


struct kw kw_tbl[] = {
	{ "add", add_event },
	{ "continue", cont },
	{ "finalize", finalize },
	{ "halt", halt },
	{ "initialize", init },
	{ "ls", list },
	{ "reassign", reassign },
	{ "rewrite", rewrite },
};


static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	int rc;


	char *action = av_value(avl, "action");

	if (!action)
		goto err0;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw)
		goto err1;

	rc = kw->action(kwl, avl, NULL);
	if (rc)
		goto err2;
	return 0;
 err0:
	msglog(LDMSD_LDEBUG,usage(self));
	goto err2;
 err1:
	msglog(LDMSD_LDEBUG,"Invalid configuration keyword '%s'\n", action);
 err2:
	return 0;
}


void fincounter(struct active_counter *pe){

	if (pe == NULL){
		return;
	}

	pe->mctr = NULL;
	pe->wctl = 0;

	pe->state = CTR_BROKEN;
	free(pe->data);
	pe->data = NULL;
	pe->ndata = 0;

	pthread_mutex_destroy(&pe->lock);
}


void fin(){
	struct active_counter *pe;

	pthread_mutex_lock(&cfglock);
	TAILQ_FOREACH(pe, &counter_list, entry){
		fincounter(pe);
	}

	free(pe); //FIXME: does this work?
	pthread_mutex_unlock(&cfglock);

};

static int sample(struct ldmsd_sampler *self)
{
	struct active_counter* pe;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR, SAMP ": in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	ldms_transaction_begin(set);

	LJI_SAMPLE(set, 1);

	TAILQ_FOREACH(pe, &counter_list, entry) {
		pthread_mutex_lock(&pe->lock);
		readregister(pe);
		pthread_mutex_unlock(&pe->lock);
	}

	ldms_transaction_end(set);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	_free_names();

	for (i = 0; i < numinitnames; i++){
		free(initnames[i]);
		initnames[i] = NULL;
	}
	numinitnames = 0;

	for (i = 0; i < msr_numoptions; i++){
		free(counter_assignments[i].name);
		counter_assignments[i].name = NULL;
		free(counter_assignments[i].core_flag);
		counter_assignments[i].core_flag = NULL;
	}
	msr_numoptions = 0;

	//should also free the counter list

	ldms_set_delete(set);
}

static struct ldmsd_sampler msr_interlagos_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &msr_interlagos_plugin.base;
}
