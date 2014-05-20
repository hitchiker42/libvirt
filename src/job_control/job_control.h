/*
 * job_control.c Core implementation of job control
 *
 * Copyright (C) 2014 Tucker DiNapoli
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Tucker DiNapoli
 */
#ifndef __JOB_CONTROL_H__
#define __JOB_CONTROL_H__

#include <config.h>

#include <stdlib.h>

/* Currently job code exists in the libxl and qemu domains,
   This code is an attempt to create a unified api for job control
   that can replace the current domain specific code and be used for
   job control in other domains*/

/* I am currently unsure how (if at all) block jobs will factor into things
*/
/* this already exists, but the async fields need to be added */
typedef enum {
    VIR_JOB_NONE = 0, /*no job running*/
    VIR_JOB_QUERY, /* job will not change domain state (read-only)*/
    VIR_JOB_DESTROY, /* job will destroy the current domain */
    VIR_JOB_MODIFY, /* job may change domain state (read-write)*/
    /* Asynchronous jobs run in the background and allow other jobs to run at
       the same time, based on a specific mask in the job object*/
    VIR_JOB_ASYNC, /* asynchronous job running*/
    VIR_JOB_ASYNC_NESTED, /* normal job running within an async obj*/
    VIR_JOB_ABORT, /* abort current async job */
    VIR_JOB_LAST
} virDomainObjJobType; /*virDomainJobType is taken by an enum in libvirt.h*/
/*specifically this enum*/
# define JOB_MASK(job)                  (1 << (job - 1))

#if 0
typedef enum {
    VIR_DOMAIN_JOB_NONE      = 0, /* No job is active */
    VIR_DOMAIN_JOB_BOUNDED, /* Job with a finite completion time */
    VIR_DOMAIN_JOB_UNBOUNDED, /* Job without a finite completion time */
    VIR_DOMAIN_JOB_COMPLETED, /* Job has finished, but isn't cleaned up */
    VIR_DOMAIN_JOB_FAILED, /* Job hit error, but isn't cleaned up */
    VIR_DOMAIN_JOB_CANCELLED, /* Job was aborted, but isn't cleaned up */
    VIR_DOMAIN_JOB_LAST
} virDomainJobState;
#endif
/* Possibly consolidate these two structs into one*/
typedef struct _virDomainJobInfo virDomainJobInfo;
typedef virDomainJobInfo *virDomainJobInfoPtr;
typedef struct _virDomainJobObj virDomainJobObj;
typedef virDomainJobObj *virDomainJobPtr;
char *virDomainJobTypeToString(enum virDomainJobType);
enum virDomainJobProgressType {
    VIR_DOMAIN_JOB_TIME=0,
    VIR_DOMAIN_JOB_MEM,
    VIR_DOMAIN_JOB_FILES,
    VIR_DOMAIN_JOB_DATA,
};
typedef virDomainJobInfo *virDomainJobInfoPtr;
struct _virDomainJobInfo {
    /* One of virDomainJobType */
    int type;

    /* Time is measured in milliseconds */
    unsigned long long timeElapsed;    /* Always set */
    unsigned long long timeRemaining;  /* Only for VIR_DOMAIN_JOB_BOUNDED */
    //this should probably just use the same fileld as above
    unsigned long long estimatedTimeRemaning;
    /* Data is measured in bytes unless otherwise specified
     * and is measuring the job as a whole.
     *
     * For VIR_DOMAIN_JOB_UNBOUNDED, dataTotal may be less
     * than the final sum of dataProcessed + dataRemaining
     * in the event that the hypervisor has to repeat some
     * data, such as due to dirtied pages during migration.
     *
     * For VIR_DOMAIN_JOB_BOUNDED, dataTotal shall always
     * equal the sum of dataProcessed + dataRemaining.
     */
    unsigned long long dataTotal;
    unsigned long long dataProcessed;
    unsigned long long dataRemaining;

    /* As above, but only tracking guest memory progress */
    unsigned long long memTotal;
    unsigned long long memProcessed;
    unsigned long long memRemaining;

    /* As above, but only tracking guest disk file progress */
    unsigned long long fileTotal;
    unsigned long long fileProcessed;
    unsigned long long fileRemaining;
};


enum virDomainJobInfoType {
    VIR_DOMAIN_JOB_TIME_ELAPSED=0,
    VIR_DOMAIN_JOB_TIME_REMAINING,
    VIR_DOMAIN_JOB_DOWNTIME,
    VIR_DOMAIN_JOB_DATA_TOTAL,
    VIR_DOMAIN_JOB_DATA_PROCESSED,
    VIR_DOMAIN_JOB_DATA_REMAINING,
    VIR_DOMAIN_JOB_MEMORY_TOTAL,
    VIR_DOMAIN_JOB_MEMORY_PROCESSED,
    VIR_DOMAIN_JOB_MEMORY_REMAINING,
    VIR_DOMAIN_JOB_MEMORY_CONSTANT,
    VIR_DOMAIN_JOB_MEMORY_NORMAL,
    VIR_DOMAIN_JOB_MEMORY_NORMAL_BYTES,
    VIR_DOMAIN_JOB_DISK_TOTAL,
    VIR_DOMAIN_JOB_DISK_PROCESSED,
    VIR_DOMAIN_JOB_DISK_REMAINING,
    VIR_DOMAIN_JOB_COMPRESSION_CACHE,
    VIR_DOMAIN_JOB_COMPRESSION_BYTES,
    VIR_DOMAIN_JOB_COMPRESSION_PAGES,
    VIR_DOMAIN_JOB_COMPRESSION_CACHE_MISSES,
    VIR_DOMAIN_JOB_COMPRESSION_OVERFLOW,
    VIR_DOMAIN_JOB_TYPE_LAST
};
/*
  change these to the values of an enum virDomainJobInfoType, or something similar
  and add a function char *virDomainJobInfoTypeToString(virDomainJobInfoType type);
*/
VIR_ENUM_IMPL(virDomainJobInfo,VIR_DOMAIN_JOB_TYPE_LAST,
              "time_elapsed",
              "time_remaining",
              "downtime",
              "data_total",
              "data_processed",
              "data_remaining",
              "memory_total",
              "memory_processed",
              "memory_remaining",
              "memory_constant",
              "memory_normal",
              "memory_normal_bytes",
              "disk_total",
              "disk_processed",
              "disk_remaining",
              "compression_cache",
              "compression_bytes",
              "compression_pages",
              "compression_cache_misses",
              "compression_overflow",
);


VIR_ENUM_IMPL(virDomainJob, VIR_DOMAIN_JOB_LAST,
    "NONE",
    "QUERY",
    "DESTROY",
    "MODIFY",
    "ASYNC",
    "ASYNC NESTED",
    "ABORT",
);

/*structure adapted from qemuDomainJobObj */
/*Something needed, probably not as a part of this object will be a way
  to detect if jobs are allowed in the current domain*/
/* this should probably be added to the virDomainObj struct which is in domain_conf.c*/
/*NOTE: the qemu implementation keeps track of the number of queued jobs and 
 returns an error on trying to queue more jobs than some specificed maximum
 I should probably add this*/
struct virDomainJobObj {
    virCond cond; /* Use to coordinate jobs */
    /* obtained using virThreadSelfID, the code for which says it's for debugging
       use only, however it's used in job code for both qemu and libxl, so 
       I assume it's ok to use
    */
    unsigned long long owner;           /* Thread id which set current job */
    
    virCond asyncCond;                  /* Use to coordinate with async jobs */
    unsigned long long asyncOwner;      /* Thread which set current async job */
    unsigned long long mask;            /* Jobs allowed during async job */
    unsigned long long start;           /* When the async job started */
    
    virDomainObjJobType active; /* Currently running job type */
    virDomainObjJobType asyncJob; /* Currently running async job type */
    /*should this perhaps be inlined?*/
    virDomainObjJobInfo info;              /* Processed async job progress data */
    /*this is here for now, but might fit better elsewhere*/
    bool asyncAllowed; /* are asynchronous jobs allowed in the current domain */
    bool asyncAbort;                    /* abort of async job requested */
/* newly added */
    /*this is essentialy a variable length array, would it be better to declare
      it as such, or keep it like this, since variable length arrays are c99?
    */
    void * domainPrivateData; /* Data local to the specific domain of the job*/
/*  for instance the current qemu object has a qemuMonitorMigrationStatus field, which
    would need to be put here
*/
};
/*The specific details of a job are domain specific (for instance vm migration in qemu
 or wiping a storage volume) but */

/* starting jobs */
int virDomainObjBeginJob(virDomainObjPtr dom, enum virDomainObjJobType job);
int virDomainObjBeginAsyncJob(virDomainObjPtr dom, enum virDomainObjJobType job);
/* the above two functions are just wrappers around an internal funcion
int virDomainObjStartJobInternal(virDomainObjPtr dom
virDomainJobObjPtr obj,
enum virDomainObjJobType job,
enum virDomainObjJobType async); */
/* called when a job is finished, returns virObjUnref(dom) 
   or -1 if there is no job currently running*/
int virDomainObjEndJob(virDomainObjPtr dom);
int virDomainObjEndAsyncJob(virDomainObjPtr dom);
/* initializes the job obj in dom*/
int virDomainObjJobInit(virDomainObjPtr dom);
void virDomainObjFreeJob(virDomainObjPtr dom);/* should this be virDomainCleanupJob?*/
/* cancels the current async job, returns immediately*/
int virDomainObjAbortAsyncJob(virDomainObjPtr dom);
/* cancels the current synchronous job, waits for the job to stop*/
int virDomainObjAbortJob(virDomainObjPtr dom);
/* Changed to static internal functions

the name in qemu/libxl is _DomainObjResetJob, which seems to imply
   restarting the job, at least to me. 
void virDomainObjResetJobObj(virDomainObjPtr dom);
void virDomainObjResetAsyncJob(virDomainObjPtr dom);
*/
/* Async specific functions*/
int virDomainObjSetAsyncJobMask(virDomainObjPtr dom, unsigned long long mask);
/* I'm not sure if these will be possible to reasonably implement
virDomainJobObjPtr virDomainObjSaveJob(virDomainObjPtr dom);
//could be called RestoreJob,ResumeJob,etc
int virDomainObjSetJob(virDomainObjPtr dom, virDomainJobObjPtr job);
*/
/* functions to query job state, none exist in the current code,
   they might be usefull, but until then don't worry about them
enum virDomainJobType virDomainObjCurrentJob(const virDomainObjPtr dom);
*/
/* internal function to get job obj from the domain, I'm currently unsure of 
   what exactly this will entail, since the job object will probably 
   be part of the domain object this likely won't be necessary
*/
virDomainJobObjPtr virDomainObjGetJobObj(const virDomainObjPtr dom);


/*currently existing functions 
int virDomainGetJobInfo(virDomainPtr dom,
                        virDomainJobInfoPtr info);
int virDomainGetJobStats(virDomainPtr domain,
                         int *type,
                         virTypedParameterPtr *params,
                         int *nparams,
                         unsigned int flags);
int virDomainAbortJob(virDomainPtr dom);
*/

struct virJobObj {
    virCond cond; /* Use to coordinate jobs */
    /* obtained using virThreadSelfID, the code for which says it's for debugging
       use only, however it's used in job code for both qemu and libxl, so 
       I assume it's ok to use
    */
    unsigned long long jobID;
    unsigned long long owner;           /* Thread id which set current job */

    unsigned long long mask;            /* Jobs allowed during async job */
    unsigned long long start;           /* When the async job started */
    
    virDomainObjJobType active; /* Currently running job type */
    virDomainObjJobType asyncJob; /* Currently running async job type */
    /*should this perhaps be inlined?*/
    virDomainObjJobInfo info;              /* Processed async job progress data */
    /*this is here for now, but might fit better elsewhere*/
    bool asyncAllowed; /* are asynchronous jobs allowed in the current domain */
    bool asyncAbort;                    /* abort of async job requested */
/* newly added */
    /*this is essentialy a variable length array, would it be better to declare
      it as such, or keep it like this, since variable length arrays are c99?
    */
    void * domainPrivateData; /* Data local to the specific domain of the job*/
/*  for instance the current qemu object has a qemuMonitorMigrationStatus field, which
    would need to be put here
*/
};
#endif
