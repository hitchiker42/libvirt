#include <config.h>
#include "virdomjobcontrol.h"
int
qemuDomainObjInitjob(qemuDomainObjPrivatePtr priv)
{
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjJobObjInit(dom_job);
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

int qemuDomainObjBeginJob(virQEMUDriverPtr driver,
                          virDomainObjPtr obj,
                          enum qemuDomainJob job)
{
    if (qemuDomainObjBeginJobInternal(driver, obj, job, false) < 0) {
        return -1;
    } else {
        return 0;
    }
}

int qemuDomainObjBeginAsyncJob(virQEMUDriverPtr driver,
                               virDomainObjPtr obj,
                               enum qemuDomainAsyncJob asyncJob)
{
    if (qemuDomainObjBeginJobInternal(driver, obj, asyncJob, true) < 0) {
        return -1;
    } else {
        return 0;
    }
}
bool qemuDomainObjEndJob(virQEMUDriverPtr driver, virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &obj->jobs;
    virDomainObjEndJob(dom_job);
    return virObjectUnref(obj);
}

bool qemuDomainObjEndJob(virQEMUDriverPtr driver, virDomainObjPtr obj)
{
    qemuDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &obj->jobs;
    virDomainObjEndAsyncJob(dom_job);
    return virObjectUnref(obj);
}
