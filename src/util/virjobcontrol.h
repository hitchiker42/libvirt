/*
 * virjobcontrol.h Core implementation of job control
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

#ifndef __JOB_CONTROL_H__
#define __JOB_CONTROL_H__

#include <config.h>

#include <stdlib.h>

#include "virthread.h"
typedef enum {
    VIR_JOB_NONE = 0, /*no job running*/
    VIR_JOB_QUERY, /* job will not change domain state (read-only)*/
    VIR_JOB_DESTROY, /* job will destroy the current domain */
    VIR_JOB_MODIFY, /* job may change domain state (read-write)*/
    VIR_JOB_LAST,
} virJobType; /*virDomainJobType is taken by an enum in libvirt.h*/

typedef enum {
    VIR_JOB_STATE_NONE      = 0, /* No job is active */
    VIR_JOB_STATE_BOUNDED, /* Job with a finite completion time */
    VIR_JOB_STATE_UNBOUNDED, /* Job without a finite completion time */
    VIR_JOB_STATE_SUSPENDED, /* Job is Suspended and can be resumed */
    VIR_JOB_STATE_COMPLETED, /* Job has finished, but isn't cleaned up */
    VIR_JOB_STATE_FAILED, /* Job hit error, but isn't cleaned up */
    VIR_JOB_STATE_CANCELLED, /* Job was aborted, but isn't cleaned up */
    VIR_JOB_STATE_LAST
} virJobState;

enum virDomainJobProgressType {
    VIR_DOMAIN_JOB_PROGRESS_TIME=0,
    VIR_DOMAIN_JOB_PROGRESS_MEM,
    VIR_DOMAIN_JOB_PROGRESS_FILES,
    VIR_DOMAIN_JOB_PROGRESS_DATA,
};

typedef unsigned int virJobID;
typedef struct _virJobObj virJobObj;
typedef virJobObj *virJobObjPtr;
typedef struct _virJobInfo virJobInfo;
typedef virJobInfo *virJobInfoPtr;

typedef int
(*virJobCallback)(virJobObjPtr job, void *privateData);

VIR_ENUM_IMPL(virJobType, VIR_JOB_LAST,
    "none",
    "query",
    "destroy",
    "modify",
);

VIR_ENUM_IMPL(virJobState, VIR_JOB_STATE_LAST,
   "none" ,
   "bounded" ,
   "unbounded" ,
   "suspended" ,
   "completed" ,
   "failed" ,
   "cancelled" ,
   "last",
);

struct _virJobObj {
    virCond cond; /* Use to coordinate jobs */
    union {
        virDomainObjPtr dom;
        virStorageVolObjPtr vol;
        virStoragePoolObjPtr pool;
    } objPtr; /* the domain/storage vol/pool that the job is running in/on*/
    unsigned long long owner; /* Thread id which set current job */
    unsigned long long start; /* when the job started*/  
    /* information about the job, this is a private struct, access to information
       about the job should be obtained by functions */
    virJobInfoPtr info;
    virJobID id; /* the job id */
    virJobType type; /* The type of the job */
    virJobState state; /* The current state of the job */
    bool async; /* True if job is running asynchronously */
    bool asyncAbort; /* True if the job is running asynchronously and should be aborted */
    void *privateData;
};

int virJobObjInit(virJobObjPtr job);
int virJobObjFree(virJobObjPtr job);
int virJobObjBegin(virJobObjPtr job, virJobType type);
int virJobObjEnd(virJobObjPtr job);
int virJobObjReset(virJobObjPtr job);
int virJobObjAbort(virJobObjPtr job);
int virJobObjSuspend(virJobObjPtr job);



virJobObjPtr virJobFromID(virJobID id);
virJobID virJobIDFromJob(virJobObjPtr job);
