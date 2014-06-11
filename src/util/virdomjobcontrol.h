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
/* This Code in intended to be used to replace the current job control
   code in the qemu and libxl drivers. The current job control internals
   should be able to be replaced with with calls to these functions, which
   will simplify job control, but also avoid modifying any code that uses
   the current job control functions
*/

/*these need to be able to form a mask*/
enum virDomainJobFeatures {
    VIR_DOM_JOB_UNSUPPORTED,
    VIR_DOM_JOB_SUPPORTED,
    VIR_DOM_JOB_ASYNC,
    VIR_DOM_JOB_SUSPEND,
    VIR_DOM_JOB_LAST
};
struct _virDomainJobObj {
/*    virDomainObjPtr dom;*/
    virMutex lock;
    virJobObj current_job;
    virJobObj async_job;
/* for now only one non async job can be run at one time
   (which is all qemu allows anyway) */
    unsigned long long mask; /* what jobs can be started currently */
    unsigned long long waitLimit; /* how long to wait on conition variables*/

    enum virDomainJobFeatures features;
};

typedef struct _virDomainJobObj virDomainJobObjh;
typedef virDomainJobObj *virDomainJobObjPtr;

/* should all of these take a virDomainJobObjPtr as a first argument?*/
int virDomainObjJobObjInit(virDomainJobObjPtr dom_job);
void virDomainObjJobObjFree(virDomainJobObjPtr dom_job);

virJobID virDomainObjBeginJob(virDomainJobObjPtr dom_job,
                           virJobType type);
virJobID virDomainObjBeginAsyncJob(virDomainJobObjPtr dom_job,
                                   virJobType type);
int virDomainObjEndJob(virDomainJobObjPtr dom_job);
int virDomainObjEndAsyncJob(virDomainJobObjPtr dom_job,
                            virJobID id);
int virDomainObjAbortJob(virDomainJobObjPtr dom_job);
int virDomainObjAbortAsynctJob(virDomainJobObjPtr dom_job,
                              virJobID id);

virJobID virDomainObjSuspendJob(virDomainJobObjPtr dom_job);
int virDomainObjResumeJob(virDomainJobObjPtr dom_job,
                          virJobID id);
int virDomainObjSetJobMask(virDomainJobObjPtr dom_job,
                           unsigned int job_mask);
int virDomainObjJobAllowed(virDomainJobObjPtr dom_job,
                           virJobType type);
void virDomainObjDisownAsyncJob(virDomainJobObjPtr dom_job);
int virDomainObjSetJobWaitTimeout(virDomainJobObjPtr dom_job,
                                  unsigned long long timeout);
