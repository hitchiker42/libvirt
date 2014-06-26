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
#include "viralloc.h"

#define MASK_FILL(dom_job) (dom_job->mask=-1)
#define MASK_CLEAR(dom_job) (dom_job->mask=0)
#define MASK_SET(dom_job, val) (dom_job->mask|=val)
#define MASK_UNSET(dom_job, val) (dom_job->mask&=(~val))
#define MASK_CHECK(dom_job, val) (dom_job->mask & val)



#define LOCK_DOM_JOB(dom_job)                   \
    virMutexLock(&dom_job->lock)
#define UNLOCK_DOM_JOB(dom_job)                 \
    virMutexUnlock(&dom_job->lock)

int
virDomainObjJobObjInit(virDomainJobObjPtr dom_job,
                       bool asyncAllowed,
                       int maxQueuedJobs,
                       unsigned long long condWaitTime)
{
    if (virMutexInit(&dom_job->lock) < 0) {
        goto error;
    }
    if (virJobObjInit(&dom_job->current_job) <= 0) {
        virMutexDestroy(&dom_job->lock);
        goto error;
    }

    virJobObjSetMaxWaiters(&dom_job->current_job, maxQueuedJobs);

    if (asyncAllowed) {
        dom_job->features |= VIR_DOM_JOB_ASYNC;
        if (virJobObjInit(&dom_job->async_job) <= 0) {
            virJobObjCleanup(&dom_job->current_job);
            virMutexDestroy(&dom_job->lock);
            goto error;
        }
        virJobObjSetMaxWaiters(&dom_job->async_job, maxQueuedJobs);
    }

    dom_job->waitLimit = condWaitTime;


    return 0;

 error:
    return -1;

}
void
virDomainObjJobObjFree(virDomainJobObjPtr dom_job)
{
    virMutexDestroy(&dom_job->lock);
    virJobObjCleanup(&dom_job->current_job);
    virJobObjCleanup(&dom_job->async_job);

    VIR_FREE(dom_job);
}

void
virDomainObjJobObjCleanup(virDomainJobObjPtr dom_job)
{
    virMutexDestroy(&dom_job->lock);
    virJobObjCleanup(&dom_job->current_job);
    virJobObjCleanup(&dom_job->async_job);
}

int
virDomainObjBeginJob(virDomainJobObjPtr dom_job, virJobType type)
{
    LOCK_DOM_JOB(dom_job);
    int retval;
 retry:
    /* wait for current non-async job to finish */
    if (VIR_JOB_OBJ_ACTIVE(dom_job->current_job)) {
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
    if (VIR_JOB_OBJ_ACTIVE(dom_job->current_job) || type ^dom_job->mask) {
        goto retry;
    }
    if ((retval = virJobObjBegin(&dom_job->current_job, type)) < 0) {
        goto error;
    }
    UNLOCK_DOM_JOB(dom_job);
    return retval;

 error:
    UNLOCK_DOM_JOB(dom_job);
    return -1;
}

int
virDomainObjBeginAsyncJob(virDomainJobObjPtr dom_job,
                          virJobType type)
{
    LOCK_DOM_JOB(dom_job);
    /* Set mask conservatively by default */
    int mask = 0;
    if (type == VIR_JOB_QUERY) {
        mask |= VIR_JOB_QUERY;
    }
    /* wait for the async job to finish first */
 retry:
    if (VIR_JOB_OBJ_ACTIVE(dom_job->async_job)) {
        if (virJobObjWait(&dom_job->async_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0) {
            goto error;
        }
    }
    if (VIR_JOB_OBJ_ACTIVE(dom_job->current_job)) {
        if (virJobObjWait(&dom_job->current_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0) {
            goto error;
        }
    }
    if (VIR_JOB_OBJ_ACTIVE(dom_job->async_job) ||
        VIR_JOB_OBJ_ACTIVE(dom_job->current_job)) {
        goto retry;
    }
    dom_job->mask = mask;
    int retval;
    if ((retval = virJobObjBegin(&dom_job->async_job, type) < 0)) {
        goto error;
    }
    UNLOCK_DOM_JOB(dom_job);
    return retval;
 error:
    UNLOCK_DOM_JOB(dom_job);
    return -1;
}
int
virDomainObjEndJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    int retval = virJobObjEnd(&dom_job->current_job);
    UNLOCK_DOM_JOB(dom_job);
    return retval;
}
int
virDomainObjEndAsyncJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    int retval = virJobObjEnd(&dom_job->async_job);
    UNLOCK_DOM_JOB(dom_job);
    return retval;
}

int
virDomainObjAbortAsyncJob(virDomainJobObjPtr dom_job)
{
    LOCK_DOM_JOB(dom_job);
    int retval = virJobObjAbort(&dom_job->async_job);
    UNLOCK_DOM_JOB(dom_job);
    return retval;
}
bool
virDomainObjJobAllowed(virDomainJobObjPtr dom_job,
                       virJobType type)
{
    LOCK_DOM_JOB(dom_job);
    bool retval = !(VIR_JOB_OBJ_ACTIVE(dom_job->current_job)) &&
        MASK_CHECK(dom_job, type);
    UNLOCK_DOM_JOB(dom_job);
    return retval;
}

int
virDomainObjSuspendJob(virDomainJobObjPtr dom_job)
{
/* Normally all non async jobs use the same job object, which is fine
   but when a job is suspended a new job object needs to be created.
   If any part of this function fails the current job is restored
   and the return value indicates the error:
   -1 allocation failure,
   -2 error initializing new job object,
   -3 error suspending job.
*/
    LOCK_DOM_JOB(dom_job);
    int retval = 0;
    virJobObjPtr suspended_job;
    if (VIR_ALLOC_QUIET(suspended_job) < 0) {
        return -1;
    }
    memcpy(&dom_job->current_job, suspended_job, sizeof(virJobObj));
    virJobObjCleanup(&dom_job->current_job);
    if (virJobObjInit(&dom_job->current_job) < 0) {
        retval = -2;
        goto error;
    }
    retval = virJobObjSuspend(&dom_job->current_job);
    if (retval <= 0) {
        retval = -3;
        goto error;
    }
    UNLOCK_DOM_JOB(dom_job);
    return retval;
 error:
    memcpy(suspended_job, &dom_job->current_job, sizeof(virJobObj));
    VIR_FREE(suspended_job);
    UNLOCK_DOM_JOB(dom_job);
    return retval;

}
int
virDomainObjResumeJob(virDomainJobObjPtr dom_job,
                          virJobID id)
{
    virJobObjPtr suspended_job = virJobFromID(id);
    if (!suspended_job) {
        return -1;
    }
    virJobType type = virJobObjJobType(suspended_job);
    LOCK_DOM_JOB(dom_job);
 retry:
    if (VIR_JOB_OBJ_ACTIVE(dom_job->current_job)) {
        if (virJobObjWait(&dom_job->current_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0) {
            goto error;
        }
    }
    if (type ^ dom_job->mask) {
        if (virJobObjWait(&dom_job->async_job,
                          &dom_job->lock,
                          dom_job->waitLimit) < 0){
            goto error;
        }
    }
    if (VIR_JOB_OBJ_ACTIVE(dom_job->current_job) ||
        type ^ dom_job->mask) {
        goto retry;
    }
    { /*start a new scope so the compilier doesn't complain about jumping
        over variable initialization*/
        int retval = virJobObjResumeIfNotAborted(suspended_job);
        if (retval) {
            memcpy(&dom_job->current_job, suspended_job, sizeof(virJobObj));
        }
        UNLOCK_DOM_JOB(dom_job);
        return retval;
    }
 error:
    UNLOCK_DOM_JOB(dom_job);
    return -1;
}
