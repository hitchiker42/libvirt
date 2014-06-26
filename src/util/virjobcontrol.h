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

/* Things that still need to be done,
   Completing the api for getting/updating job info.
   Create a generic way to manage multiple jobs (the way I manage jobs for
   domains in virdomjobcontrol.c/h is done for compatability)
   Write tests
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
    VIR_JOB_LAST = 4 /*needs to be explicitly set for VIR_ENUM_IMPL to work*/
} virJobType;
const char *virJobTypeToString(int type);
int virJobTypeFromString(const char *type);
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
    VIR_JOB_FLAG_FILE_BOUND    = 0x008, /* Job is bound by a specific number of files */
    VIR_JOB_FLAG_DATA_BOUND    = 0x010, /* Job is bound by a specific ammount of data */
    VIR_JOB_FLAG_UNBOUNDED     = 0x020, /* Job has no specific bound */
    VIR_JOB_FLAG_SUSPENDED     = 0x040, /* Job is Suspended and can be resumed   */
    VIR_JOB_FLAG_COMPLETED     = 0x080, /* Job has finished, but isn't cleaned up */
    VIR_JOB_FLAG_FAILED        = 0x100, /* Job hit error, but isn't cleaned up */
    VIR_JOB_FLAG_ABORTED       = 0x200, /* Job was aborted, but isn't cleaned up */
    VIR_JOB_FLAG_MAX           = 0x200, /* Keep set to the largest enum value */
    VIR_JOB_FLAG_LAST          = 11 /* set to the number of enum values */
} virJobFlag;

const char *virJobFlagToString(virJobType type);
int virJobFromString(const char *type);

typedef int virJobID; /*signed so negitive values can be returned on error*/
typedef struct _virJobObj virJobObj;
typedef virJobObj *virJobObjPtr;
typedef struct _virJobInfo virJobInfo;
typedef virJobInfo *virJobInfoPtr;

struct _virJobObj {
    /* this should only be locked when changing the job object, not while running a job
       it's use is mostly optional, but it is needed for waiting on a job*/
    virMutex lock;/*should have a compile time option to enable/disable locking */
    virCond cond; /* Use to coordinate jobs (is this necessary?) */
    virJobType type; /* The type of the job */
    unsigned long long owner; /* Thread id that owns current job */
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
#if 0
typedef enum {
    VIR_JOB_INFO_NONE = 0,
    VIR_JOB_INFO_TIME,
    VIR_JOB_INFO_MEM,
    VIR_JOB_INFO_FILE,
    VIR_JOB_INFO_DATA,
    VIR_JOB_INFO_LAST
} virJobInfoType;
struct _virJobInfo {
    /*these fields are for monitoring estimated completion*/
    virMutex lock; /* seperate locks for the job and job info */
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
struct virJobIDArray {
    virJobID *ids;
    int size;
    int active;
};
/* this is an idea for a way of managing an arbitrary number of jobs */
struct _virJobControlObj {
    virJobObj *jobs;
    virJobIDArray active; /* keeps track of the above array of jobs */
    virJobIDArray running;
    virJobIDArray suspended;
    virJobIDArray finished;

    virMutex lock;
    unsigned long long job_mask;
};
#endif
/*main functions*/
/*this should take a mask of features*/
/**
 * virJobObjInit:
 * @job: Pointer to the job object to initialize
 *
 * Initialize the job object given, should be called before any other
 * job function.
 *
 * Like all job functions it returns the job id on success and -1 on error
 */
int virJobObjInit(virJobObjPtr job);
/**
 * virJobObjFree:
 * @job: Pointer to the job object to free
 *
 * Cleanup/free all resources/memory assoicated with job
 */
void virJobObjFree(virJobObjPtr job);
/**
 * virJobObjCleanup:
 * @job: Pointer to the job object to cleanup
 *
 * Cleanup all resources assoicated with job, and zero out the
 * corrsponding memory, but do not free it.
 */

void virJobObjCleanup(virJobObjPtr job);
/**
 * virJobObjBegin:
 * @job: The job to begin
 * @type: The type of job that is being started
 *
 * Marks job as active, sets the calling thread as the owner of the job,
 * sets the job's start time to the current time and sets it's type to type.
 *
 * End job should be called after this, once the job is done
 *
 * Returns the job id of job on success, 0 if job is already active
 * and -1 on error.
 */
int virJobObjBegin(virJobObjPtr job,
                   virJobType type);
/**
 * virJobObjEnd:
 *
 * @job: The job to end
 *
 * Ends job, calls virJobObjReset and signals all threads waiting on job.
 *
 * Ending a job does not invalidate the job object, and a new job can be
 * started using the same job object, call virJobObjFree or virJobObjCleanup
 * in order to destroy a job object.
 *
 * returns the job's id on success and -1 if the job was not active.
 */
int virJobObjEnd(virJobObjPtr job);
/**
 * virJobObjReset:
 *
 * @job: The job to reset
 *
 * Clears all fields of job related to running a job. This does not
 * clear the job id, any configurable parameters (currently just the
 * maximum number of waiting threads), or the mutex/condition variable
 * assoicated with the job. This is called internally by virJobObjEnd
 * and there should be few reasons to call this explicitly.
 */
int virJobObjReset(virJobObjPtr job);
/**
 * virJobObjAbort:
 * @job: The job to abort
 *
 * Marks job as aborted, since jobs are asyncronous this doesn't actually
 * stop the job. The abort status of a job can be checked by
 * virJobObjCheckAbort. A suspended job can be aborted.
 *
 * returns the job id on success and -1 if
 * job is not currently running/suspended.
 */
int virJobObjAbort(virJobObjPtr job);
/**
 * virJobObjAbort:
 * @job: The job to abort/signal waiters
 *
 * Behaves identically to virJobObjAbort except all threads waiting
 * on job are signaled after the abort status is set.
 */
int virJobObjAbortAndSignal(virJobObjPtr job);
/**
 * virJobObjSuspend:
 * @job: The job to suspend
 *
 * Marks job as suspended, it is up to the caller of the function
 * to actually save any state assoicated with the job
 *
 * This function returns the job's id on success and -1 if the job
 * was not active.
 */
int virJobObjSuspend(virJobObjPtr job);
/**
 * virJobObjResume:
 * @job The job to resume
 *
 * Resume job, as with virJobObjSuspend it is up to the caller to
 * insure that the work being done by job is actually restarted.
 *
 * Since a job can be aborted while it is suspended the caller should
 * check to see if job has been aborted, a convenience function
 * virJobObjResumeIfNotAborted is provided.
 *
 * returns the job id if job was resumed and -1 if the job was not suspended.
 */
int virJobObjResume(virJobObjPtr job);

/**
 * virJobObjResumeIfNotAborted:
 * @job The job to resume
 *
 * Behaves the same as virJobObjResume except it returns 0 and does not
 * resume the job if the job was aborted while suspended.
 */
int virJobObjResumeIfNotAborted(virJobObjPtr job);

/* returns -3 if max waiters is exceeded, -2 on timeout, -1 on other error*/
/**
 * virJobObjWait:
 * @job: The job to wait on
 * @lock: The lock to use in the call to virCondWait
 * @limit: If not 0 the maximum ammount of time to wait (in milliseconds)
 *
 * This function waits for job to be completed, or to otherwise signal on it's
 * condition variable.
 *
 * If lock is NULL the internal job lock will be used, otherwise lock should
 * be held by the calling thread.
 * (NOTE: I'm not sure if it's a good idea or not to use the internal lock)
 *
 * If limit is > 0 virCondWaitUntil is called instead of virCondWait with limit
 * being used as the time parameter.
 *
 * If job is not currently active return successfully.
 *
 * Like all job functions returns the job's id on success.
 *
 * On Failure returns a negitive number to indicate the cause of failure
 * -3 indicates the maximum number of threads were alread waiting on job
 * -2 indicates that virCondWaitUntil timed out
 * -1 indicates some other error in virCondWait/virCondWaitUntil
 */
int virJobObjWait(virJobObjPtr job,
                  virMutexPtr lock,
                  unsigned long long limit);
/* Should I provide a function to wait for a suspended job to resume? */

/**
 * virJobObjSignal:
 * @job: The job to signal from
 * @all: If true signal all waiting threads, otherwise just signal one
 *
 * Signal a thread/threads waiting on job. In most cases waiting threads
 * are signaled when needed internally, but this is provided if for
 * some reason waiting threads need to be manually signaled.
 */

void virJobObjSignal(virJobObjPtr job, bool all);

/* accessor functions*/
/**
 * virJobObjCheckAbort:
 * @job: The job whoes status should be checked
 *
 * Returns true if job has been aborted, false otherwise
 */
bool virJobObjCheckAbort(virJobObjPtr job);
/**
 * virJobObjCheckTime:
 * @job: The job whoes time should be checked
 *
 * Returns the time in milliseconds that job has been running for.
 * returns 0 if job is not active and -1 if there is an error in
 * getting the current time.
 */
long long virJobObjCheckTime(virJobObjPtr job);
/**
 * virJobObjActive:
 * @job: The job whoes status should be checked
 *
 * Returns true if job is currently active, false otherwise
 */
bool virJobObjActive(virJobObjPtr job);
/**
 * virJobObjJobType:
 * @job: The job to get the type of
 *
 * returns the job type of job.
 */
virJobType virJobObjJobType(virJobObjPtr job);
/**
 * virJobObjJobType:
 * @job: The job to change the type of
 * @type: The new type of job
 *
 * Changes the job type of a currently running/suspended job.
 *
 */
/* Should this return an error if job isn't active*/
void virJobObjSetJobType(virJobObjPtr job, virJobType type);
/**
 * virJobObjSetMaxWaiters:
 * @job: The job to modify
 * @max: The maximum number of threads to allow to wait on job at once
 *
 * Sets the maximum number of threads that can simultaneously wait on job.
 * By default there is essentially no limit (in reality the limit is the
 * maximum value that can be held by an int)
 */
void virJobObjSetMaxWaiters(virJobObjPtr job, int max);
/**
 * virJobObjChangeOwner:
 * @job: the job to modify
 * @id: the thread id of the new owner
 *
 * Change the owner of the given job to the thread with the given id.
 * the thread id isn't checked in any way so it is up to the calling
 * function to insure that the thread id is valid
 */
void virJobObjChangeOwner(virJobObjPtr job, unsigned long long id);

/* These functions allow for the code using jobs to
   work with extra job flags. This is seperate from job info
   For all functions if the flag is not greater than VIR_JOB_LAST return -1.
*/
/* return true if flag changed and false if not */
int virJobObjSetPrivateFlag(virJobObjPtr job, int flag);
int virJobObjUnsetPrivateFlag(virJobObjPtr job, int flag);
/* return true if flag set and false if not*/
int virJobObjCheckPrivateFlag(virJobObjPtr job, int flag);


virJobObjPtr virJobFromID(virJobID id);
virJobID virJobIDFromJob(virJobObjPtr job);

/* Macro(s) to access fields quickly in actual structs (i.e not pointers).
   Considering how often it needs to be known if a job is active or not this
   should be worthwhile. (I imagine the complier could optimize away the function
   calls, but it would need the function definations, which would require
   link-time-optimization or whole program compilation.
*/
#define VIR_JOB_OBJ_ACTIVE(obj) (virAtomicIntGet(&obj.flags) & VIR_JOB_FLAG_ACTIVE)
#define VIR_JOB_OBJ_CHECK_ABORT(obj) (virAtomicIntGet(&obj.flags) & VIR_JOB_FLAG_ABORTED)

#endif
