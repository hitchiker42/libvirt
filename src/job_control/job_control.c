/*
 * job_control.c Core implementation of job control
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

#include <stdlib.h>

int
virDomainObjInitJob(virDomainJobObjPtr dom)
{
    memset(&dom->job, 0, sizeof(dom->job));
    /*no need for a goto error in code this short*/
    if (virCondInit(&priv->job.cond) < 0){
        return -1;
    }
    if (virCondInit(&priv->job.asyncCond) < 0) {
        virCondDestroy(&priv->job.cond);
        return -1;
    }
    return 0;
}
void
virDomainObjFreeJob(virDomainObjPtr dom)
{
    virCondDestroy(&dom->job.cond);
    virCondDestroy(&dom->job.asyncCond);
}
/*I imagine since this is static I don't need to worry abount naming it
  something complicated
 */
static void resetJob(virJobObjPtr job)
{
    job->active=VIR_JOB_NONE;
    job->owner=0;
}
static void resetAsyncJob(virJobObjPtr job)
{
    job->asyncJob=VIR_JOB_NONE;
    job->asyncOwner=0;
}
static void virJobObjResetAsyncJob(virJobObjPtr job);
static int virDomainObjBeginJobInternal(virDomainObjPtr dom,
                                        virDomainJobObjPtr job,
                                        enum virDomainJobType job_type,
                                        bool async){
    unsigned long long now;
    unsigned long long then;
    if (virTimeMillisNow(&now) < 0) {
        return -1;
    }
    
    /* currently libxl and qemu have macros for job wait time, so this field 
     will need to be added, Probably as part of the job object*/
    then = now + job->jobWaitTime;

    virObjRef(dom);
    
    /* wait for the current job to finish*/
    while (job->active) {
        VIR_DEBUG("Wait normal job condition for starting job: %s",
                  virDomainJobTypeToString(job_type));
        if (virCondWaitUntil(&job->cond, &dom->parent.lock, then) < 0){
            goto error;
        }
    }



/*this presumably isn't necessary since EndJob will reset the job */
    resetJob(job);
    if (!async){
        /*TODO: debug message*/
        job->active=job_type;
        job->owner=virThreadSelfID();
    } else {
        /*TODO: debug message*/
        virJobObjResetAsyncJob(job);
        job->asyncJob=job_type;
        /*this is what the qemu code does, presumably it's close 
          enough to the current time*/
        job->start=now; 
    }
    return 0;
 error:
    /*TODO: warning message*/
    if (errno == ETIMEDOUT){
        virReportError(VIR_ERR_OPERATION_TIMEOUT,
                       "%s", _("cannot acquire state change lock"));
    } else {
        virReportSystemError(errno,
                             "%s", _("cannot acquire job mutex"));
    }

    virObjectUnref(obj);
    return -1;
    
}
int virDomainObjBeginAsyncJob(virDomainObjPtr dom,
                              enum virDomainJobType job)
{
    virDomainJobObjPtr obj;
    if (!(obj=virDomainObjGetJobObj(dom))){
        return -1;
    }
    if (!obj->asyncAllowed){
        return -1;
    }
    if (virDomainObjBeginJobInternal(dom,obj,job,1) <0 ){
        return -1;
    } else {
        return 0;
    }
}
int virDomainObjBeginJob(virDomainObjPtr dom,
                         enum virDomainJobType job)
{
    virDomainJobObjPtr obj;
    if (!(obj=virDomainObjGetJobObj(dom))){
        return -1;
    }
    if (virDomainObjBeginJobInternal(dom,obj,job,0) <0 ){
        return -1;
    } else {
        return 0;
    }
}
int virDomainEndJob(virDomainObjPtr dom)
{e
    virDomainJobObjPtr job;
    if (!(job=virDomainObjGetJobObj(dom))){
        return -1;
    }
    if (job->active == VIR_JOB_NONE){
        return -1;
    }
/* may or may not be necessary
    if(job->owner != virThreadSelfId()){
        return -1;
    }
*/
    /*TODO: debug message*/
    resetJobObj(job);
    virCondSignal(&job->cond);

    return virObjectUnref(dom);
}

int virDomainEndAsyncJob(virDomainObjPtr dom)
{
    virDomainJobObjPtr job;
    if (!(job=virDomainObjGetJobObj(dom))){
        return -1;
    }
    if (job->active == VIR_JOB_NONE){
        return -1;
    }
    /*TODO: debug message*/
    resetJob(job);
    virCondSignal(&job->cond);

    return virObjectUnref(dom);
}

    
/*
  usage idea
{
    // some variables declaration

    if (!(vm = qemuDomObjFromDomain(dom)))
        return -1;

    // some preparation here

    if (qemuDomainObjBeginJob(driver, vm, QEMU_JOB_MODIFY) < 0)
        goto cleanup;

    if (!virDomainObjIsActive(vm)) {
        virReportError(VIR_ERR_OPERATION_INVALID,
                       "%s", _("domain is not running"));
        goto endjob;
    }

    // Some actual work here

    ret = 0;

endjob:
    if (!qemuDomainObjEndJob(driver, vm))
        vm = NULL;

cleanup:
    if (vm)
        virObjectUnlock(vm);
    // More of cleanup work here
    return ret;
}
 */
