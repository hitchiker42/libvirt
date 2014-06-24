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
#include "virlog.h"
VIR_LOG_INIT("virjobcontrol");

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
#define LOCK_JOB_INFO(job)                      \
    virMutexLock(&job->info->lock)
#define UNLOCK_JOB_INFO(job)                    \
    virMutexUnlock(&job->info->lock)
#define GET_CURRENT_TIME(time)                  \
    if (virTimeMillisNow(&time) < 0) {          \
        return -1;                              \
    }


#define CHECK_FLAG_ATOMIC(job, flag) (virAtomicIntGet(&job->flags) & VIR_JOB_FLAG_##flag)
#define CHECK_FLAG(job, flag) (job->flags & VIR_JOB_FLAG_##flag)
#define CHECK_FLAG_BY_VALUE(job, flag) (job->flags & flag)
#define CHECK_FLAG_BY_VALUE_ATOMIC(job, flag)\
    (virAtomicIntGet(&job->flags) & flag)

#define SET_FLAG_ATOMIC(job, flag) (virAtomicIntOr(&job->flags, VIR_JOB_FLAG_##flag))
#define SET_FLAG(job, flag) (job->flags |= VIR_JOB_FLAG_##flag)
#define SET_FLAG_BY_VALUE(job, flag) (job->flags |= flag)
#define SET_FLAG_BY_VALUE_ATOMIC(job, flag)\
    (virAtomicIntOr(&job->flags, flag))

#define UNSET_FLAG_ATOMIC(job, flag) (virAtomicIntAnd(&job->flags, (~VIR_JOB_FLAG_##flag)))
#define UNSET_FLAG(job, flag) (job->flags &= (~VIR_JOB_FLAG_##flag))
#define UNSET_FLAG_BY_VALUE(job, flag) (job->flags &= (~flag))
#define UNSET_FLAG_BY_VALUE_ATOMIC(job, flag)\
    (virAtomicIntAnd(&job->flags, (~flag)))

#define CLEAR_FLAGS_ATOMIC(job) (virAtomicIntSet(&job->flags, VIR_JOB_FLAG_NONE))
#define CLEAR_FLAGS(job) (job->flags = VIR_JOB_FLAG_NONE)
typedef struct _jobHashEntry {
    virJobID id;
    virJobObjPtr job;
    struct _jobHashEntry *next;
} jobHashEntry;

typedef struct _jobHash {
    jobHashEntry **table;
    size_t size;
    size_t num_entries;
    virRWLock lock;
} jobHash;

/* Using this incures a cost on every call to a job control function
   that uses that job control hash table, but it means that no one using
   job control needs to do anyting to use it.

   The other option would be to have a function:
   virJobControlInit(void){
     return virOnce(job_once, jobControlInit);
   }
   and require anyone using job control to call it.
 */
static struct _jobHash job_hash[];
static int init_err=0;
static virOnceControl job_once = VIR_ONCE_CONTROL_INITIALIZER;
static void
jobControlInit(void){
    if (virRWLockInit(&job_hash->lock) < 0) {
        init_err=1;
    }
    if (VIR_ALLOC_N_QUIET(job_hash->table, 16) <0) {
        init_err=1;
    }
}

/* global job id

   Because it gets incremented before it's first use 0 is
   never a valid job id

   Only ever touched with atomic instructions.
*/
static unsigned int global_id = 0;



/* Simple hash table implementation for jobs.
   It is keyed by job id's which are just integers so there isn't acutally
   a hash function (I guess you could say it's just the identity function).
*/
#ifndef VIR_JOB_REHASH_THRESHOLD
#define VIR_JOB_REHASH_THRESHOLD 0.8
#endif

#ifndef VIR_JOB_BUCKET_LEN
#define VIR_JOB_BUCKET_LEN 5
#endif

#ifndef VIR_JOB_GROWTH_FACTOR
#define VIR_JOB_GROWTH_FACTOR 2
#endif

#define READ_LOCK_TABLE(ht) virRWLockRead((virRWLockPtr)&ht->lock)
#define WRITE_LOCK_TABLE(ht) virRWLockWrite((virRWLockPtr)&ht->lock)
#define UNLOCK_TABLE(ht) virRWLockUnlock((virRWLockPtr)&ht->lock)

static int jobHashRehash(jobHash *ht);
#define maybe_rehash(ht)                        \
    if (((double)ht->size*VIR_JOB_BUCKET_LEN)/(ht->num_entries) >=      \
        VIR_JOB_REHASH_THRESHOLD){                                      \
        jobHashRehash(ht);                                           \
    }


/* Doesn't lock ht, so should only be called with a write lock held on ht*/
static int jobHashRehash(jobHash *ht);

/* look for id in the hash table ht and return the entry associated with it
   if id isn't it the table return nil*/
static jobHashEntry*
jobLookup(const jobHash *ht, virJobID id)
{
    if (virOnce(&job_once, jobControlInit) < 0) {
        return NULL;
    }
    READ_LOCK_TABLE(ht);
    int bucket = id % ht->size;
    jobHashEntry *retval = NULL, *job_entry;
    if (!ht->table[bucket]) {
        goto end;
    }
    job_entry = ht->table[bucket];
    do {
        if (job_entry->id == id) {
            retval = job_entry;
            goto end;
        }
    } while ((job_entry = job_entry->next));

 end:
    UNLOCK_TABLE(ht);
    return retval;
}

/* add job the hashtable of currently existing jobs, job should
   have already been initialized via virJobObjInit.
   returns 0 if job is already in the hash,
   returns the id of the job if it was successfully added
   returns -1 on error.
 */
static int
jobHashAdd(jobHash *ht, virJobObjPtr job)
{
    if (virOnce(&job_once, jobControlInit) < 0) {
        return -1;
    }
    virJobID id = job->id;
    if (jobLookup(ht, id)) { /* check if job is in the table*/
        return 0;
    }
    /*we calculate this twice(once in jobLookup once here), but since its
      just a single division I think it's ok,
      if not I'll turn jobLookup into macro or something */
    int bucket = id % ht->size;
/*    jobHashEntry *new_entry = virAllocSize(sizeof(jobHashEntry));*/
    jobHashEntry *new_entry;
    if (VIR_ALLOC_QUIET(new_entry) < 0) {
        return -1;
    }
    jobHashEntry *last_entry = ht->table[bucket];
/*    if (!new_entry) {
        return -1;
        }   */
    *new_entry = (jobHashEntry) {.id = id, .job = job};

    WRITE_LOCK_TABLE(ht);
    if (!last_entry) {
        ht->table[bucket] = new_entry;
    } else {
        while (last_entry->next) {
            last_entry = last_entry->next;
        }
        last_entry->next = new_entry;
    }
    ht->num_entries++;
    maybe_rehash(ht);
    UNLOCK_TABLE(ht);
    return id;
}
/* Remove the job with id id from the list of currently existing jobs,
   this doesn't free/cleanup the actual job object, it just removes
   the list entry, this is called by virJobObjFree, so there shouldn't
   be any reason to call it directly.

   return values are the same as for jobAlist add, 0 if no job found,
   job id on success, -1 on some other error;
*/
static int
jobHashRemove(jobHash *ht, virJobID id)
{
    int bucket = id % ht->size;
    /*we can't just call jobLookup because we need the entry before the one
      we want to free*/
    /* It seems excessive to do a read lock here then get a write lock later*/
    WRITE_LOCK_TABLE(ht);
    jobHashEntry *entry = ht->table[bucket], *old_entry;
    if (!entry) {
        goto error;
    }
    if (entry->id != id) {
        while (entry && entry->next->id != id) {
            entry = entry->next;
        }
    }
    if (!entry) {
        goto error;
    }
    old_entry = entry->next;
    entry->next = old_entry->next;
    VIR_FREE(old_entry);
    UNLOCK_TABLE(ht);
    return id;
 error:
    UNLOCK_TABLE(ht);
    return 0;
}
static int
jobHashRehash(jobHash *ht)
{
    size_t new_size = ht->size*VIR_JOB_GROWTH_FACTOR;
    jobHashEntry **new_table; /* = virAllocSize(sizeof(jobHashEntry)*new_size);
    if (!new_table) {
        return -1;
        }*/
    if (VIR_ALLOC_N_QUIET(new_table, new_size)) {
        return -1;
    }
    jobHashEntry **old_table = ht->table;
    /*the max number of jobs is bounded by an int so the hash table
      can't be larger than an int, which is why I'm not using size_t,
      the empty comment makes it pass the syntax check, because I can
      prove that I don't need to use size_t;
    */
    int i/**/;
    for (i = 0; i<ht->size; i++) {
        jobHashEntry *old_entry = old_table[i];
        while (old_entry) {
            int bucket = old_entry->id % new_size;
            jobHashEntry *new_entry = new_table[bucket];
            if (!new_entry) {
                new_table[bucket] = old_entry;
            } else {
                while (new_entry->next) {
                    new_entry = new_entry->next;
                }
                new_entry->next = old_entry;
            }
            old_entry = old_entry->next;
        }
    }
    ht->size = new_size;
    ht->table = new_table;
    VIR_FREE(old_table);
    return 1;
}

/*inline versions for internal use, I'm not sure if there are specific
  rules for how to use inline in libvirt for portability */
static inline virJobID
jobIDFromJobInternal(virJobObjPtr job)
{
    return job->id;
}


static inline virJobObjPtr
jobFromIDInternal(virJobID id)
{
    jobHashEntry *entry = jobLookup(job_hash, id);
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
    /* This code checks to see if job was already initialized,
       I don't know if this is needed.*/
    if (job->id != 0) {
        VIR_DEBUG("job %d has already been initialized", job->id);
        return 0;
    }
    job->id = virAtomicIntInc(&global_id);
    job->maxJobsWaiting = INT_MAX;

    if (virCondInit(&job->cond) < 0) {
        return -1;
    }

    if (virMutexInit(&job->lock) < 0) {
        virCondDestroy(&job->cond);
        return -1;
    }

    jobHashAdd(job_hash, job);

    return job->id;
}

void
virJobObjFree(virJobObjPtr job)
{
    virCondDestroy(&job->cond);
    jobHashRemove(job_hash, job->id);
    VIR_FREE(job);
}

void
virJobObjCleanup(virJobObjPtr job)
{

    virCondDestroy(&job->cond);
    jobHashRemove(job_hash, job->id);
    memset(job, '\0', sizeof(virJobObj));
    return;
}

int
virJobObjBegin(virJobObjPtr job,
               virJobType type)
{
    LOCK_JOB(job);
    if (CHECK_FLAG(job, ACTIVE)) {
        VIR_DEBUG("Job %d is already running", job->id);
        UNLOCK_JOB(job);
        return 0;
    }
    VIR_DEBUG("Starting job %d with type %s",
              job->id, virJobTypeToString(type));
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
    int retval;
    LOCK_JOB(job);
    if (CHECK_FLAG(job, ACTIVE) || CHECK_FLAG(job, SUSPENDED)) {
        SET_FLAG(job, ABORTED);
        retval = job->id;
    } else {
        retval = -1;
    }
    UNLOCK_JOB(job);
    return retval;
}

int
virJobObjAbortAndSignal(virJobObjPtr job)
{
    int retval;
    LOCK_JOB(job);
    if (CHECK_FLAG(job, ACTIVE) || CHECK_FLAG(job, SUSPENDED)) {
        SET_FLAG(job, ABORTED);
        retval = job->id;
        virCondBroadcast(&job->cond);
    } else {
        retval = -1;
    }
    UNLOCK_JOB(job);
    return retval;
}

/* lock should be held by the calling function*/
int
virJobObjWait(virJobObjPtr job,
              virMutexPtr lock,
              unsigned long long limit)
{

    bool job_lock = false;
    int retval;
    if (CHECK_FLAG_ATOMIC(job, ACTIVE)) { /* if the job isn't active we're fine*/
        if (virAtomicIntInc(&job->jobsWaiting) > job->maxJobsWaiting) {
            errno = 0;
            goto error;
        }
        if (!lock) {
            LOCK_JOB(job);
            lock = &job->lock;
            job_lock = true;
        }
        while (CHECK_FLAG_ATOMIC(job, ACTIVE)) {
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
    if (job_lock) {
        UNLOCK_JOB(job);
    }
    return job->id;

 error:
    virAtomicIntDec(&job->jobsWaiting);
    if (job_lock) {
        UNLOCK_JOB(job);
    }
    if (!errno) {
        return -3;
    } else if (errno == ETIMEDOUT) {
        return -2;
    } else {
        return -1;
    }
}

int
virJobObjSuspend(virJobObjPtr job)
{
    if (!CHECK_FLAG_ATOMIC(job, ACTIVE)) {
        return -1;
    }
    LOCK_JOB(job);
    SET_FLAG(job, SUSPENDED);
    UNSET_FLAG(job, ACTIVE);
    UNLOCK_JOB(job);
    return job->id;
}
int
virJobObjResume(virJobObjPtr job)
{
    if (!CHECK_FLAG_ATOMIC(job, SUSPENDED)) {
        return -1;
    }
    LOCK_JOB(job);
    UNSET_FLAG(job, SUSPENDED);
    SET_FLAG(job, ACTIVE);
    UNLOCK_JOB(job);
    return job->id;
}
int
virJobObjResumeIfNotAborted(virJobObjPtr job)
{
    int retval;
    LOCK_JOB(job);
    if (!CHECK_FLAG(job, SUSPENDED)) {
        retval = -1;
    } else if (CHECK_FLAG(job, ABORTED)) {
        retval = 0;
    } else {
        UNSET_FLAG(job, SUSPENDED);
        SET_FLAG(job, ACTIVE);
        retval = job->id;
    }
    UNLOCK_JOB(job);
    return retval;
}

void
virJobObjSetMaxWaiters(virJobObjPtr job, int max)
{
    virAtomicIntSet(&job->maxJobsWaiting, max);
}

bool
virJobObjCheckAbort(virJobObjPtr job)
{
    return CHECK_FLAG_ATOMIC(job, ABORTED);
}
bool
virJobObjActive(virJobObjPtr job)
{
    return CHECK_FLAG_ATOMIC(job, ACTIVE);
}
void
virJobObjChangeOwner(virJobObjPtr job, unsigned long long id)
{
    /* can't do atomic operations on types bigger than an int */
    LOCK_JOB(job);
    job->owner=id;
    UNLOCK_JOB(job);
}

#define INVALID_FLAG(x) ((x & (x-1) || (x <= VIR_JOB_FLAG_MAX))

int
virJobObjSetPrivateFlag(virJobObjPtr job, int flag)
{
    if (INVALID_FLAG(x)) {
        return -1;
    }
    /* can't use atomic operations here without using
       compare and swap, so just lock for simplicity */
    LOCK_JOB(job);
    int prev_flags = job->flag;
    bool retval = (prev_flags == (SET_FLAG_BY_VALUE(job, flag)));
    UNLOCK_JOB(job);
    return retval;
}
int
virJobObjUnsetPrivateFlag(virJobObjPtr job, int flag)
{
    if (INVALID_FLAG(x)) {
        return -1;
    }
    /* can't use atomic operations here without using
       compare and swap, so just lock for simplicity */
    LOCK_JOB(job);
    int prev_flags = job->flag;
    bool retval = (prev_flags == (UNSET_FLAG_BY_VALUE(job, flag)));
    UNLOCK_JOB(job);
    return retval;
}
int
virJobObjSetPrivateFlag(virJobObjPtr job, int flag)
{
    if (INVALID_FLAG(x)) {
        return -1;
    }
    return CHECK_FLAG_BY_VALUE_ATOMIC(job, flag);
}


/* since we need to be able to return a negitive answer on error
   the time difference we can return is somewhat limited, but
   not in any significant way*/
long long
virJobObjCheckTime(virJobObjPtr job)
{
    if (!CHECK_FLAG(job, ACTIVE)) {
        return 0;
    }
    unsigned long long now;
    if (virTimeMillisNow(&now) < 0) {
        return -1;
    }
    return now - job->start;
}

void
virJobObjSignal(virJobObjPtr job, bool all)
{
    if (all) {
        virCondBroadcast(&job->cond);
    } else {
        virCondSignal(&job->cond);
    }
}

#if 0
/* Job info code, currently a work in progress */
/* I'm not sure if it makes sense to have the allowed info types
   stored in the job objects flags, but that's how it is now
*/
#define INFO_TYPE_TO_JOB_FLAG(type) (1 << (type + 1))
#define SET_INFO_TYPE_FLAG(job, type) \
    (job->flags |= INFO_TYPE_TO_JOB_FLAG(type))
#define SET_INFO_TYPE_FLAG_ATOMIC(job, type)            \
    (virAtomicIntOr(&job->flags, INFO_TYPE_TO_JOB_FLAG(type)))
#define CHECK_INFO_TYPE_FLAG(job, type)                 \
    (type < VIR_JOB_INFO_TYPE_LAST &&                   \
     (job->flags & INFO_TYPE_TO_JOB_FLAG(type)))
#define CHECK_INFO_TYPE_FLAG_ATOMIC(job, type)                          \
    (type < VIR_JOB_INFO_TYPE_LAST &&                                   \
     (virAtomicIntFetch(&job->flags) & INFO_TYPE_TO_JOB_FLAG(type)))
/* I'll admit these macros are kinda ugly, but it's better
   then having switch statements all over the place
*/
/* You should make sure type is valid before calling this*/
#define job_info_get(job, type, what)           \
    switch (type) {                               \
        case VIR_JOB_INFO_TIME:                 \
            return job->info->time##what;       \
        case VIR_JOB_INFO_MEM:                  \
            return job->info->mem##what;        \
        case VIR_JOB_INFO_FILE:                 \
            return job->info->files##what;      \
        case VIR_JOB_INFO_DATA:                 \
            return job->info->data##what;       \
        default:                                \
            return 0;                           \
    }
#define job_info_set(job, type, val, what)              \
    switch (type) {                                       \
        case VIR_JOB_INFO_TIME:                         \
            job->info->time##what = val;                \
            return job->id;                             \
        case VIR_JOB_INFO_MEM:                          \
            job->info->mem##what = val;                 \
            return job->id;                             \
        case VIR_JOB_INFO_FILE:                         \
            job->info->files##what = val;               \
            return job->id;                             \
        case VIR_JOB_INFO_DATA:                         \
            job->info->data##what = val;                \
            return job->id;                             \
        default:                                        \
            return -1;                                  \
    }
static unsigned long long
getInfoTotal(virJobObjPtr job, virJobInfoType type)
{
    job_info_get(job, type, Total);
}
static unsigned long long
getInfoProcessed(virJobObjPtr job, virJobInfoType type)
{
    job_info_get(job, type, Processed);
}
static unsigned long long
getInfoRemaining(virJobObjPtr job, virJobInfoType type)
{
    job_info_get(job, type, Remaining);
}
static int
setInfoTotal(virJobObjPtr job,
             virJobInfoType type,
             unsigned long long val)
{
    job_info_set(job, type, Total);
}
static int
setInfoProcessed(virJobObjPtr job,
                 virJobInfoType type,
                 unsigned long long val)
{
    job_info_set(job, type, Processed);
}
static int
setInfoRemaining(virJobObjPtr job,
                 virJobInfoType type,
                 unsigned long long val)
{
    job_info_set(job, type, Remaining);
}
/* Job info isn't ininitalized by default so that jobs that don't use info
   won't have to pay the cost of initializing the info object
*/
int
virJobObjInfoInitalize(virJobObjPtr job,
                       int numargs,
                       virJobInfoType *info_types,
                       unsigned long long *totals)
{
    int retval;
    if (numargs >= VIR_JOB_INFO_TYPE_LAST) {
        VIR_DEBUG("More info types passed as arguments than exist");
        return -3;
    }
    /* this needs to lock the job, but all other job info functions can
       just use the job info lock*/
    LOCK_JOB(job);
    if (job->info) {
        UNLOCK_JOB(job);
        return 0;
    }
    if (VIR_ALLOC_QUIET(job->info) < 0) {
        retval = -1;
        goto error;
    }
    if (virMutexInit(&job->info->lock) < 0) {
        retval = -1;
        goto error;
    }
    /* I already checked the number of arguments above, so I know an int is
       large enough for my loop variable
     */
    int i/**/;
    if (!numargs) {
        SET_FLAGS(job, UNBOUNDED);
    }
    while (i = 0; i < numargs; i++) {
        if (setInfoTotal(job, info_types[i], totals[i]) < 0) {
            retval = -2;
            goto error;
        }
        SET_INFO_TYPE_FLAG(job, info_types[i]);
    }
    unsigned long long now;
    if (virTimeMillisNow(&now) < 0) {
        retval = -1;
        goto error;
    }
    job->info->timeStamp = now;
    UNLOCK_JOB(job);
    return job->id;

 error:
    UNLOCK_JOB(job);
    return retval;
}

int
virJobObjUpdateInfo(virJobObjPtr job,
                    virJobInfoType type,
                    unsigned long long progress)
{
    LOCK_JOB_INFO(job);
    if (!CHECK_INFO_TYPE_FLAG_ATOMIC(job, type)) {
        UNLOCK_JOB_INFO(job);
        return -1;
    }
    unsigned long long last_progress = getInfoProcessed(job, type);
    setInfoProcessed(job, type, progress);
    /* This is all extra to get an estimated progress rate and remaning time
       which I imagine would be useful to the user*/
    unsigned long long total = getInfoTotal(job, type);

    double percent = ((double)progress/(double)total);
    double percent_diff = percent - job->info->percentComplete;
    job->info->percentComplete = percent;

    unsigned long long now, then, rate, remaining;
    if (virTimeMillisNow(&now) < 0) {
        retval = -1;
        goto error;
    }
    then = job->info->timeStamp;

    job->info->timeStamp = now;

    /* (%of job / time taken since last update + last_rate)/ 2 = avg rate*/
    rate = ((unsigned long long)(percent_diff/(now-then)) + job->info->estRate)/2;

    /* 1/rate (sec/%) * percent_remaning = time remaining*/
    remaining = ((1/rate) * (100 - percent));

    UNLOCK_JOB_INFO(job);
    return job->id;
}
#endif

static int
virJobIDArrayInit(virJobIDArray *arr, int size)
{
    if (VIR_ALLOC_N_QUIET(arr->ids, size) < 0) {
        return -1;
    }
    arr->size=size;
    arr->active=0;
    return 1;
}
/* managing multiple jobs */
int
virJobControlObjInit(virJobControlObjPtr jobs)
{
    if (VIR_ALLOC_QUIET(jobs) < 0) {
        return -1;
    }
    if (virJobIDArrayInit(&jobs->active, 10) < 0) {
        goto error;
    }
    if (virJobIDArrayInit(&jobs->running, 10) < 0) {
        goto error;
    }
    if (virJobIDArrayInit(&jobs->suspended, 10) < 0) {
        goto error;
    }
    if (virJobIDArrayInit(&jobs->finished, 10) < 0) {
        goto error;
    }
    if (virMutexInit(jobs->lock) < 0 ) {
        goto error;
    }
    jobs->jobs_size = jobs->running_size =
        jobs->suspended_size = jobs->finished_size = 10;
    return 1;
    
 error:
    VIR_FREE(jobs->active.ids);
    VIR_FREE(jobs->running.ids);
    VIR_FREE(jobs->suspended.ids);
    VIR_FREE(jobs->finished.ids);
    VIR_FREE(jobs);
    return -1;
}
int
virJobControlStartNewJob(virJobControlObjPtr jobs)
{
    
