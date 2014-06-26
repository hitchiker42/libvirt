/*
 * libxl_jobcontrol.c Implementation of the libxl job control functions
 * Using the new job control api.
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

#include <string.h>

#include "virdomjobcontrol.h"
#include "libxl_domain.h"
static int
libxlDomainObjInitJob(libxlDomainObjPrivatePtr priv)
{
    memset(&priv->job, 0, sizeof(priv->job));

    if (virCondInit(&priv->job.cond) < 0)
        return -1;

    return 0;
}

static void
libxlDomainObjResetJob(libxlDomainObjPrivatePtr priv)
{
    virDomainJobObjPtr dom_job = &priv->job;

    virDomainObjJobObjInit(dom_job);
    virDomainObjSetJobWaitTimeout(dom_job, (1000ull * 30));

    return;
}

/*
 * obj need not be locked before calling, libxlDriverPrivatePtr must NOT be locked
 *
 * This must be called by anything that will change the VM state
 * in any way
 *
 * Upon successful return, the object will have its ref count increased,
 * successful calls must be followed by EndJob eventually
 */
int
libxlDomainObjBeginJob(libxlDriverPrivatePtr driver ATTRIBUTE_UNUSED,
                       virDomainObjPtr obj,
                       virJobType job)
{
    libxlDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjBeginJob(dom_job, job);
}
bool
libxlDomainObjEndJob(libxlDriverPrivatePtr driver ATTRIBUTE_UNUSED,
                     virDomainObjPtr obj)
{
    libxlDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &priv->jobs;
    return virDomainObjEndJob(dom_job);
}
bool
libxlDomainCleanupJob(libxlDriverPrivatePtr driver,
                      virDomainObjPtr vm,
                      virDomainShutoffReason reason)
{
    libxlDomainObjPrivatePtr priv = obj->privateData;
    virDomainJobObjPtr dom_job = &priv->jobs;
    if (virDomainObjBeginJob(dom_job, VIR_JOB_DESTROY) < 0) {
        return true;
    }

    libxlDomainCleanup(driver, vm, reason);

    return virDomainObjEndJob(dom_job);
}
