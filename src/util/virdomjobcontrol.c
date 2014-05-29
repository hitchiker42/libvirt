/*
 * virdomjobcontrol.c Job control in domains
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

#include "virdomjobcontrol.h"

#define MASK_FILL(dom_job) (dom_job->mask=-1)
#define MASK_CLEAR(dom_job) (dom_job->mask=0)
#define MASK_SET(dom_job, val) (dom_job->mask|=val)
#define MASK_UNSET(dom_job, val) (dom_job->mask&=(~val))
#define MASK_CHECK(dom_job, val) (dom_job->mask & val)

#define LOCK_DOM_JOB(dom_job)                   \
    virMutexLock(&dom_job->lock)
#define UNLOCK_DOM_JOB(dom_job)                 \
    virMutexUnlock(&dom_job->lock)

#define vir_alloc virAlloc
static inline void*
virAllocSize(size_t size)
{
    void *ptr;
    vir_alloc(&ptr, size, 0, 0, 0, 0, 0);
    return ptr;
}
int
virDomainObjJobObjInit(virDomainObjPtr dom,
                       virDomainJobObjPtr dom_job,
                       bool asyncAllowed,
                       int maxQueuedJobs,
                       unsigned long long condWaitTime)
{
    if (virMutexInit(&dom_job->lock) < 0) {
        goto error_1;
    }
    if (virCondInit(&dom_job->cond) < 0) {
        goto error_2;
    }
    if (virJobInit(&dom_job->current_job) <= 0) {
        goto error_3;
    }
    if (virJobInit(&dom_job->async_job) <= 0) {
        goto error_4;
    }
    dom_job->dom = dom;
    dom_job->async_allowed = asyncAllowed;
    dom_job->waitLimit = condWaitTime;
    return 0;
    /*I have four different error conditions, this is the eaisest way to do this*/
 error_4:
    virJobCleanup(&dom_job->current_job);
 error_3:
    virCondDestroy(&dom_job->cond);
 error_2:
    virMutexDestroy(&dom_job->lock);
 error_1:
    return -1;

}
void
virDomainObjJobObjFree(virDomainJobObjPtr dom_job)
{
    virCondDestroy(&dom_job->cond);
    virMutexDestroy(&dom_job->lock);
    virJobCleanup(&dom_job->current_job);
    virJobCleanup(&dom_job->async_job);

    VIR_FREE(dom_job);
}
/* dom_job should be locked by the calling function*/
static inline int
wait_for_current_job_completion(virDomainJobObjPtr dom_job)
{
 restart:
    if (virJobActive(dom_job->current_job)) {
        if (dom_job->num_jobs_queued >= dom_job->max_queued_jobs) {
            goto error;
        }
        dom_job->num_jobs_queued++;
        if (virJobObjWait(&dom_job->current_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0){
            dom_job->num_jobs_queued--;
            goto error;
        }
        dom_job->num_jobs_queued--;
    }
    if (virJobActive(dom_job->current_job)) {
        goto restart;
    }
    return 1;
 error:
    return -1;
}

int
virDomainObjStartJobInternal(virDomainJobObjPtr dom_job, virJobType type)
{
    LOCK_DOM_JOB(dom_job);
    int retval;
 retry:
    /* wait for current non-async job to finish */
    if (virJobActive(dom_job->current_job)) {
        if (virJobObjWait(&dom_job->current_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0){
            goto error;
        }
    }
    /*
       check if the current async job allows jobs of our type to run
       if not wait for it to finish
    */
    if (type ^ dom_job->mask) {
        if (virJobObjWait(&dom_job->async_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0){
            goto error;
        }
    }
    /* it's possible another job started before us */
    if (virJobActive(dom_job->current_job) || type ^dom_job->mask) {
        goto retry;
    }
    if ((retval = virJobBegin(dom_job->current_job, type)) < 0) {
        goto error;
    }
    UNLOCK_DOM_JOB(dom_job);
    return retval;

 error:
    UNLOCK_DOM_JOB(dom_job);
    return -1;
}

int
virDomainObjStartAsyncJob(virDomainJobObjPtr dom_job,
                              int mask,
                              virJobType type)
{
    LOCK_DOM_JOB(dom_job);
    /* wait for the async job to finish first */
 retry:
    if (virJobActive(dom_job->async_job)) {
        if (virJobObjWait(dom_job->async_job,
                          dom_job->lock,
                          dom_job->waitLimit) < 0) {
            goto error;
        }
    }
    if (virJobActive(dom_job->current_job)) {
        if (virJobObjWait(dom_job->current_job,
                          dom_job->lock,
                          dom_job->waitLimit) < 0) {
            goto error;
        }
    }
    if (virJobActive(dom_job->async_job) ||
        virJobActive(dom_job->current_job)) {
        goto retry;
    }
    dom_job->mask = mask;
    if ((retval = virJobObjBegin(dom_job->async_job, type) < 0)) {
        goto error;
    }
    UNLOCK_DOM_JOB(dom_job);
    return retval;
 error:
    UNLOCK_DOM_JOB(dom_job);
    return -1
}
int
virDomainObjEndJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    virJobObjEnd(dom_job->current_job);
    UNLOCK_DOM_JOB(dom_job);
    return 0;
}
int
virDomainObjEndAsyncJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    virJobObjEnd(dom_job->async_job);
    UNLOCK_DOM_JOB(dom_job);
    return 0;
}

int
virDomainObjAbortAsyncJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    virJobObjAbort(dom_job->async_job);
    UNLOCK_DOM_JOB(dom_job);
}

/*
  suspend job
  similar to endJob.
  Need to figure out how to store suspended jobs

*/
/*
  resume job
  similar to start job
*/
