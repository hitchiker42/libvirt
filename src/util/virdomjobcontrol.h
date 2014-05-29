/*
 * virdomjobcontrol.h Job control in domains
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


#include "virjobcontrol.h"

/*these need to be able to form a mask*/
enum virDomainJobFeatures {
    VIR_DOM_JOB_UNSUPPORTED,
    VIR_DOM_JOB_SUPPORTED,
    VIR_DOM_JOB_ASYNC,
    VIR_DOM_JOB_SUSPEND,
    VIR_DOM_JOB_LAST
};
struct _virDomainJobObj {
    virDomainObjPtr dom;
    virMutex lock;
    virCond cond;
#if 0
    /* the complicated generic way*/
    /* I'm using a virJobObj* instead of a virJobObjPtr to emphasize
       that this is an array*/
    virJobObj *jobs; /* all jobs in the domain (the actual job objects) */
    virJobID *jobs_running; /* jobs currently running */
    int num_jobs; /* number of total jobs*/
    int jobs_allocated; /* size of the jobs array*/
    int num_jobs_running; /* number of jobs running */
#else
    /* the eaiser way*/
    virJobObj current_job;
    virJobObj async_job;
#endif
/* for now only one non async job can be run at one time */
    unsigned long long mask; /* what jobs can be started currently */
    unsigned long long waitLimit; /* how long to wait on conition variables*/

    int num_jobs_queued; /* number of jobs waiting to run,
                            but not allowed because of the mask */
    int max_queued_jobs; /* The maximum number of jobs that can be queued
                            at one time*/
    enum virDomainJobFeatures features;
};

typedef struct _virDomainJobObj virDomainJobObjh;
typedef virDomainJobObj *virDomainJobObjPtr;

/* should all of these take a virDomainJobObjPtr as a first argument?*/
int virDomainObjJobObjInit(virDomainJobObjPtr dom_job);
void virDomainObjJobObjFree(virDomainJobObjPtr dom_job);

virJobID virDomainObjStartJob(virDomainJobObjPtr dom_job,
                           virJobType type);
virJobID virDomainObjStartAsyncJob(virDomainJobObjPtr dom_job,
                                   int mask,
                                   virJobType type);
int virDomainObjEndJob(virDomainJobObjPtr dom_job);
int virDomainObjEndAsyncJob(virDomainJobObjPtr dom_job,
                            virJobID id);
int virDomainObjAbortJob(virDomainJobObjPtr dom_job);
int virDomainObjAborAsynctJob(virDomainJobObjPtr dom_job,
                              virJobID id);

virJobID virDomainObjSuspendJob(virDomainJobObjPtr dom_job);
int virDomainObjResumeJob(virDomainJobObjPtr dom_job,
                          virJobID id);
int virDomainObjSetJobMask(virDomainJobObjPtr dom_job,
                           unsigned int job_mask);
int virDomainObjJobAllowed(virDomainJobObjPtr dom_job,
                           virJobType type);
