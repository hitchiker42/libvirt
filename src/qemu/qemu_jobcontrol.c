#include <config.h>
#include "virdomjobcontrol.h"
int
qemuDomainObjInitJob(qemuDomainObjPrivatePtr priv)
{
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjJobObjInit(dom_job);
    /* get max queued jobs and other config values
       from the qemu config object
*/
}
int
qemuDomainObjFreeJob(qemuDomainObjPrivatePtr priv)
{
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjJobObjFree(dom_job);
}
void
qemuDomainObjSetAsyncJobMask(virDomainObjPtr obj,
                             unsigned long long allowedJobs)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjSetJobMask(dom_job, allowedJobs);
}

static bool
qemuDomainNestedJobAllowed(qemuDomainObjPrivatePtr priv, int job)
{
    /* this should return true if no other jobs are running*/
    return priv->jobs.mask & job;
}

bool
qemuDomainJobAllowed(qemuDomainObjPrivatePtr priv, int job)
{
    return virDomainObjJobAllowed(&priv->jobs, job);
}
static int ATTRIBUTE_NONNULL(1)
qemuDomainObjBeginJobInternal(virQEMUDriverPtr driver,
                              virDomainObjPtr obj,
                              virJobType job,
                              bool async)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &priv->jobs;
    int retval;
    virObjectRef(obj);
    if (async) {
        retval = virDomainObjBeginAsyncJob(dom_job, job);
    } else {
        retval = virDomainObjBeginJob(dom_job, job);
    }

    virObjectUnref(obj);
    return retval;
}

int
qemuDomainObjBeginJob(virQEMUDriverPtr driver,
                          virDomainObjPtr obj,
                          enum qemuDomainJob job)
{
    if (qemuDomainObjBeginJobInternal(driver, obj, job, false) < 0) {
        return -1;
    } else {
        return 0;
    }
}

int
qemuDomainObjBeginAsyncJob(virQEMUDriverPtr driver,
                               virDomainObjPtr obj,
                               enum qemuDomainAsyncJob asyncJob)
{
    if (qemuDomainObjBeginJobInternal(driver, obj, asyncJob, true) < 0) {
        return -1;
    } else {
        return 0;
    }
}
bool
qemuDomainObjEndJob(virQEMUDriverPtr driver, virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &obj->jobs;
    virDomainObjEndJob(dom_job);
    return virObjectUnref(obj);
}

bool
qemuDomainObjEndAsyncJob(virQEMUDriverPtr driver, virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &obj->jobs;
    virDomainObjEndAsyncJob(dom_job);
    return virObjectUnref(obj);
}
void
qemuDomainObjAbortAsyncJob(virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainObjAbortAsyncJob(&priv->jobs);
}
void
qemuDomainObjTransferJob(virDomainObjPtr)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &obj->jobs;
    unsigned long long thread_id = virThreadSelfID();
    virJobObjChangeOwner(dom_job->current_job, thread_id);
    return;
}
