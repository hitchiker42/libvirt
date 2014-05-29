/*
 * virjobcontrol.c Core implementation of job control
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

#include <config.h>

#include "virjobcontrol.h"
#include "viralloc.h"
#include "virtime.h"

#define vir_alloc virAlloc
#define VIR_ALLOC_SIZE(ptr, size) vir_alloc(&ptr, size, true, VIR_FROM_THIS, \
                                          __FILE__, __FUNCTION__, __LINE__)
#define VIR_ALLOC_SIZE_QUIET(ptr, size) vir_alloc(&ptr, size, 0, 0, 0, 0, 0)

#define GEN_JOB_ID_WRAPPER_FUNC(action)        \
    int virJobID##action(virJobID id){         \
        virJobObjPtr job = virJobFromIDInternal(id);      \
        return virJobObj##action(job);                 \
    }
#define GEN_JOB_ID_WRAPPER_FUNC_2(action, arg_type_)    \
    int virJobID##action(virJobID id, arg_type arg){    \
        virJobObjPtr job = virJobFromIDInternal(id);      \
        return virJobObj##action(job, arg);            \
    }
#define LOCK_JOB(job)                           \
    virMutexLock(&job->lock)
#define UNLOCK_JOB(job)                         \
    virMutexUnlock(&job->lock)

#define CHECK_FLAG_ATOMIC(job, flag) (virAtomicIntGet(&job->flags) & VIR_JOB_FLAG_##flag)
#define CHECK_FLAG(job, flag) (job->flags & VIR_JOB_FLAG_##flag)
#define SET_FLAG_ATOMIC(job, flag) (virAtomicIntOr(&job->flags, VIR_JOB_FLAG_##flag))
#define SET_FLAG(job, flag) (job->flags |= VIR_JOB_FLAG_##flag)
#define UNSET_FLAG_ATOMIC(job, flag) (virAtomicIntAnd(&job->flags, (~VIR_JOB_FLAG_##flag)))
#define UNSET_FLAG(job, flag) (job->flags &= (~VIR_JOB_FLAG_##flag))
#define CLEAR_FLAGS_ATOMIC(job) (virAtomicIntSet(&job->flags, VIR_JOB_FLAG_NONE))
#define CLEAR_FLAGS(job) (job->flags = VIR_JOB_FLAG_NONE)


/* This is a lazy way of doing thing but it'll work for getting somethings running
   and testable and because this is private it won't break anything when I change it*/
typedef struct _jobAlistEntry {
    virJobID id;
    virJobObjPtr job;
    struct _jobAlistEntry *next;
} jobAlistEntry;

typedef struct {
    jobAlistEntry *head;
    jobAlistEntry *tail;
    virRWLock *lock;  /*this seems like an unecessary wrapper*/
} jobAlist;

static inline void*
virAllocSize(size_t size)
{
    void *ptr;
    if (vir_alloc(&ptr, size, 0, 0, 0, 0, 0) < 0) {
        return NULL;
    }
    return ptr;
}

static jobAlist *job_alist;

static jobAlistEntry*
alistLookup(const jobAlist *alist, virJobID id)
{
    virRWLockRead(alist->lock);
    jobAlistEntry *retval = NULL;
    jobAlistEntry *job_entry = alist->head;
    do {
        if (job_entry->id == id) {
            retval = job_entry;
            break;
        }
    } while ((job_entry = job_entry->next));
    virRWLockUnlock(alist->lock);
    return retval;
}

/* add job the list of currently existing jobs, job should
   have already been initialized via virJobObjInit.
   returns 0 if job is already in the alist,
   returns the id of the job if it was successfully added
   returns -1 on error.

   job id's use unsigned ints so we need to return a long
   to fit all possible job id's
 */
static long
jobAlistAdd(jobAlist *alist, virJobObjPtr job)
{
    virJobID id = job->id;
    if (alistLookup(alist, id)) {
        return 0;
    }
    virRWLockWrite(alist->lock);
    jobAlistEntry *new_entry = virAllocSize(sizeof(jobAlistEntry));
    if (!new_entry) {
        return -1;
    }
    *new_entry = (jobAlistEntry) {.id = id, .job = job};
    alist->tail->next = new_entry;
    alist->tail = new_entry;
    virRWLockUnlock(alist->lock);
    return id;
}

/* Remove the job with id id from the list of currently existing jobs,
   this doesn't free/cleanup the actual job object, it just removes
   the list entry, this is called by virJobObjFree, so there shouldn't
   be any reason to call it directly.

   return values are the same as for jobAlist add, 0 if no job found,
   job id on success, -1 on some other error;
*/
static long
jobAlistRemove(jobAlist *alist, virJobID id)
{
    jobAlistEntry *entry = alist->head;
    /*we can't just call alistLookup because we need the entry before the one
      we want to free*/
    /* It seems excessive to do a read lock here then get a write lock later*/
    virRWLockWrite(alist->lock);
    do {
        if (entry->next->id == id) {
            break;
        }
    } while ((entry = entry->next));

    if (!entry) {
        return 0;
    }
    VIR_FREE(entry->next);
    virRWLockUnlock(alist->lock);
    return id;
}

/* global job id

   Because it gets incremented before it's first use 0 is
   never a valid job id

   it is possible this should be volatile
*/
static unsigned int global_id = 0;

static inline virJobID
jobIDFromJobInternal(virJobObjPtr job)
{
    return job->id;
}


static inline virJobObjPtr
jobFromIDInternal(virJobID id)
{
    jobAlistEntry *entry = alistLookup(job_alist, id);
    if (entry) {
        return entry->job;
    } else {
        return NULL;
    }
}

virJobObjPtr
virJobFromID(virJobID id)
{
    return jobFromIDInternal(id);

}
virJobID
virJobIDFromJob(virJobObjPtr job)
{
    return jobIDFromJobInternal(job);
}

/*should probably take an argument for initial flags*/
int
virJobObjInit(virJobObjPtr job)
{
    /* This code would check to see if job was already initialized,
       I don't know if this is needed.
       if (job->id != 0) {
         return 0;
       }
    */
    job->id = virAtomicIntInc(&global_id);
    job->maxJobsWaiting = INT_MAX;

    if (virCondInit(&job->cond) < 0) {
        return -1;
    }

    if (virMutexInit(&job->lock) < 0) {
        virCondDestroy(&job->cond);
        return -1;
    }

    jobAlistAdd(job_alist, job);

    return job->id;
}

void
virJobObjFree(virJobObjPtr job)
{
    virCondDestroy(&job->cond);
    jobAlistRemove(job_alist, job->id);
    VIR_FREE(job);
}

void
virJobObjCleanup(virJobObjPtr job)
{
    virCondDestroy(&job->cond);
    jobAlistRemove(job_alist, job->id);
}

int
virJobObjBegin(virJobObjPtr job,
               virJobType type)
{
    LOCK_JOB(job);
    /*This should ultimately do more then this */
    unsigned long long now;
    if (job->id <= 0) {
        goto error; /* job wasn't initialiazed*/
    }
    if (virTimeMillisNow(&now) < 0) {
        goto error;
    }
    job->type = type;
    SET_FLAG(job, ACTIVE);
    job->start = now;
    job->owner = virThreadSelfID();
    return job->id;
 error:
    UNLOCK_JOB(job);
    return -1;
}

int
virJobObjEnd(virJobObjPtr job)
{
    if (job->type == VIR_JOB_NONE) {
        return -1;
    }
    virJobObjReset(job);
    virCondSignal(&job->cond);

    return job->id;
}

int
virJobObjReset(virJobObjPtr job)
{
    LOCK_JOB(job);
    job->type = VIR_JOB_NONE;
    job->flags = VIR_JOB_FLAG_NONE;
    job->owner = 0;
    /*should clear the number of waiters?*/
    job->jobsWaiting = 0;
    UNLOCK_JOB(job);

    return job->id;
}

int
virJobObjAbort(virJobObjPtr job)
{
    LOCK_JOB(job);
    SET_FLAG(job, ABORTED);
    UNLOCK_JOB(job);
    return job->id;
}

/* lock should be held by the calling function*/
int
virJobObjWait(virJobObjPtr job,
              virMutexPtr lock,
              unsigned long long limit)
{
    int retval;
    if (CHECK_FLAG(job, ACTIVE)) { /* if the job isn't active we're fine*/
        if (virAtomicIntInc(&job->jobsWaiting) > job->maxJobsWaiting) {
            errno = 0;
            goto error;
        }
        while (CHECK_FLAG(job, ACTIVE)) {
            if (limit) {
                retval = virCondWaitUntil(&job->cond, lock, limit);
            } else {
                retval = virCondWait(&job->cond, lock);
            }
            if (retval < 0) {
                goto error;
            }
        }
    }
    virAtomicIntDec(&job->jobsWaiting);
    return job->id;

 error:
    virAtomicIntDec(&job->jobsWaiting);
    if (!errno) {
        return -3;
    } else if (errno == ETIMEDOUT) {
        return -2;
    } else {
        return -1;
    }
}
#if 0
int
virJobObjWaitAlt(virJobObjPtr job,
              virMutexPtr lock,
              unsigned long long limit)
{
    int retval;
    while (CHECK_FLAG(job, ACTIVE)) {
        if (limit) {
            retval = virCondWaitUntil(&job->cond, lock, limit);
        } else {
            retval = virCondWait(&job->cond, lock);
        }
        if (retval < 0) {
            goto error;
        }
    }
    return job->id;

 error:
    if (errno == ETIMEDOUT) {
        return -2);
    } else {
        return -1;
    }
}
#endif

int
virJobObjSuspend(virJobObjPtr job)
{
    LOCK_JOB(job);
    SET_FLAG(job, SUSPENDED);
    UNSET_FLAG(job, ACTIVE);
    UNLOCK_JOB(job);
    return job->id;
}
void
virJobObjSetMaxWaiters(virJobObjPtr job, int max)
{
    virAtomicIntSet(&job->maxJobsWaiting, max);
}

bool
virJobObjCheckAbort(virJobObjPtr job)
{
    return CHECK_FLAG(job, ABORTED);
}
bool
virJobActive(virJobObjPtr job)
{
    return CHECK_FLAG(job, ACTIVE);
}
/* since we need to be able to return a negitive answer on error
   the time difference we can return is somewhat limited, but
   not in any significant way*/
long long
virJobObjCheckTime(virJobObjPtr job)
{
    if (!job->start) {
        return 0;
    }
    unsigned long long now;
    if (virTimeMillisNow(&now) < 0) {
        return -1;
    }
    return now - job->start;
}
