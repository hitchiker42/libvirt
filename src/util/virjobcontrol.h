/*
 * virjobcontrol.h Core implementation of job control
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

#include <stdlib.h>

#include "viratomic.h"
#include "virthread.h"
#include "virutil.h"
/* No files other then this and virjobcontrol.c should need to
   have access to the core implmentation of jobs. The code in these
   files is intended to serve as a base for job control independent of
   drivers.

   the implmentation of the virJobObj struct is in this file just in
   case there is some reason it is needed, but it shouldn't be.
*/
/* The general type of the job, currently there are three possible values
   read only access, VIR_JOB_QUERY
   read-write access, VIR_JOB_MODIFY
   deletion, VIR_JOB_DESTROY*/
typedef enum {
    VIR_JOB_NONE     = 0, /*no job running*/
    VIR_JOB_QUERY    = 0x1, /* job will not change domain state (read-only)*/
    VIR_JOB_MODIFY   = 0x2, /* job may change domain state (read-write)*/
    VIR_JOB_DESTROY  = 0x4, /* job will destroy the domain/storage/object it is acting on */
    VIR_JOB_LAST
} virJobType;

/* General metadata/flags for jobs

   more specific flags can be added for speciic drivers,
   to do this the first value of the enum for the extra flags
   should be set to (VIR_JOB_FLAG_LAST << 1) and each additional
   flag should be set to the last flag shifted to the left by 1.

*/
typedef enum {
    VIR_JOB_FLAG_NONE          = 0x000, /* No job is active */
    VIR_JOB_FLAG_ACTIVE        = 0x001, /* Job is active */
    /* These next flags are used to indicate how progress should be measured */
    VIR_JOB_FLAG_TIME_BOUND    = 0x002, /* Job is bound by a specific ammount of time */
    VIR_JOB_FLAG_MEM_BOUND     = 0x004, /* Job is bound by a specific ammount of memory */
    VIR_JOB_FLAG_FILES_BOUND   = 0x008, /* Job is bound by a specific number of files */
    VIR_JOB_FLAG_DATA_BOUND    = 0x010, /* Job is bound by a specific ammount of data */
    VIR_JOB_FLAG_UNBOUNDED     = 0x020, /* Job has no specific bound */
    VIR_JOB_FLAG_SUSPENDED     = 0x040, /* Job is Suspended and can be resumed   */
    VIR_JOB_FLAG_COMPLETED     = 0x080, /* Job has finished, but isn't cleaned up */
    VIR_JOB_FLAG_FAILED        = 0x100, /* Job hit error, but isn't cleaned up */
    VIR_JOB_FLAG_ABORTED       = 0x200, /* Job was aborted, but isn't cleaned up */
    VIR_JOB_FLAG_LAST          = 0x400
} virJobFlag;

typedef unsigned int virJobID;
typedef struct _virJobObj virJobObj;
typedef virJobObj *virJobObjPtr;
typedef struct _virJobInfo virJobInfo;
typedef virJobInfo *virJobInfoPtr;


/*VIR_ENUM_IMPL(virJobType, VIR_JOB_LAST,
              "none",
              "query",
              "modify",
              "destroy");
*/
/*
VIR_ENUM_IMPL(virJobState, VIR_JOB_STATE_LAST,
   "none" ,
   "bounded" ,
   "unbounded" ,
   "suspended" ,
   "completed" ,
   "failed" ,
   "cancelled" ,
   "last");
*/
struct _virJobObj {
    /* this should only be locked when changing the job object, not while running a job
       it's use is mostly optional, but it is needed for waiting on a job*/
    virMutex lock;/*should have a compile time option to enable/disable locking */
    virCond cond; /* Use to coordinate jobs (is this necessary?) */
    virJobType type; /* The type of the job */
    unsigned long long owner; /* Thread id which set current job */
    unsigned long long start; /* when the job started*/
    int jobsWaiting; /* number jobs waiting on cond */
    int maxJobsWaiting; /* max number of jobs that can wait on cond */
    /* information about the job, this is a private struct, access to information
       about the job should be obtained by functions */
    virJobInfoPtr info;
    virJobID id; /* the job id, constant, and unique */
    virJobFlag flags; /* The current state of the job */
    void *privateData;
};
struct _virJobInfo {
    /*these fields are for monitoring estimated completion*/
    double percentComplete; /*easy access to simple progress */
    unsigned long long estTimeRemaining;
    unsigned long long timeStamp;
    unsigned long long estRate;

    /* these fields are for compatability with existing job
      information structs, some or all of them can be NULL depending
       on the job
    */
    virDomainBlockJobInfoPtr blockJobInfoPtr;
    virDomainJobInfoPtr domainJobInfoPtr;

    /* most of these next things are in the domain job info pointer
       but they should be accessed from here instead.
    */
    /* generally only one of these should be used unless a job
       has multiple completion conditions

       the time fields here should only be used if the job
       has a finite completion time, there are other fields
       used for monitoring estimated time

       the progress fields are for any means of measuring progross
       and can be used instead of any of the other fields
    */
    unsigned long long progressTotal;
    unsigned long long dataTotal;
    unsigned long long memTotal;
    unsigned long long filesTotal;
    unsigned long long timeTotal;

    unsigned long long progressProcessed;
    unsigned long long timeProcessed; /*not the best name, but it keeps
                                        naming consistant*/
    unsigned long long memProcessed;
    unsigned long long filesProcessed;
    unsigned long long dataProcessed;

    unsigned long long progressRemaining;
    unsigned long long dataRemaining;
    unsigned long long memRemaining;
    unsigned long long filesRemaining;
    unsigned long long timeRemaining;
};

/*main functions*/
/*this should take a mask of features*/
int virJobObjInit(virJobObjPtr job);
void virJobObjFree(virJobObjPtr job);
void virJobObjCleanup(virJobObjPtr job);
int virJobObjBegin(virJobObjPtr job,
                   virJobType type);
int virJobObjEnd(virJobObjPtr job);
int virJobObjReset(virJobObjPtr job);
void virJobObjAbort(virJobObjPtr job);
int virJobObjSuspend(virJobObjPtr job);
int virJobObjResume(virJobObjPtr job);

/* returns -3 if max waiters is exceeded, -2 on timeout, -1 on other error*/
int virJobObjWait(virJobObjPtr job,
                  virMutexPtr lock,
                  unsigned long long limit);
/*waits on the job's lock*/
int virJobObjWait2(virJobObjPtr job,
                  unsigned long long limit);

/* accessor functions*/
extern inline bool virJobObjCheckAbort(virJobObjPtr job);
long long virJobObjCheckTime(virJobObjPtr job);
extern inline bool virJobActive(virJobObjPtr job);
void virJobObjSetMaxWaiters(virJobObjPtr job, int max);

/*
int virJobIDInit(virJobID id);
int virJobIDFree(virJobID id);
int virJobIDBegin(virJobID id, virJobType type);
int virJobIDEnd(virJobID id);
int virJobIDReset(virJobID id);
int virJobIDAbort(virJobID id);
int virJobIDSuspend(virJobID id);
*/


virJobObjPtr virJobFromID(virJobID id);
virJobID virJobIDFromJob(virJobObjPtr job);
#endif
