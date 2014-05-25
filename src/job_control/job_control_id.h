#ifndef __JOB_CONTROL_H__
#define __JOB_CONTROL_H__

#include <config.h>

#include <stdlib.h>
typedef enum {
    VIR_JOB_NONE = 0, /*no job running*/
    VIR_JOB_QUERY, /* job will not change domain state (read-only)*/
    VIR_JOB_DESTROY, /* job will destroy the current domain */
    VIR_JOB_MODIFY, /* job may change domain state (read-write)*/
    VIR_JOB_LAST,
} virJobType; /*virDomainJobType is taken by an enum in libvirt.h*/

typedef enum {
    VIR_DOMAIN_JOB_NONE      = 0, /* No job is active */
    VIR_DOMAIN_JOB_BOUNDED, /* Job with a finite completion time */
    VIR_DOMAIN_JOB_UNBOUNDED, /* Job without a finite completion time */
    VIR_DOMAIN_JOB_COMPLETED, /* Job has finished, but isn't cleaned up */
    VIR_DOMAIN_JOB_FAILED, /* Job hit error, but isn't cleaned up */
    VIR_DOMAIN_JOB_CANCELLED, /* Job was aborted, but isn't cleaned up */
    VIR_DOMAIN_JOB_LAST
} virJobState;

typedef unsigned int virJobID;
/*typedef virJobInfo *virJobInfoPtr;*/

struct _virJobObj {
    virCond cond; /* Use to coordinate jobs */
    union {
        virDomainObjPtr dom;
        virStorageVolObjPtr vol;
        virStoragePoolObjPtr pool;
    } objPtr; /* the domain/storage vol/pool that the job is running in/on*/
    unsigned long long owner; /* Thread id which set current job */
    unsigned long long start; /* when the job started*/
    virJobID id; /* the job id */
    /* information about the job, this is a private struct, access to information
       about the job should be obtained by functions */
    virJobInfoPtr info; 
    virJobType type; /* The type of the job */
    virJobState state; /* The current state of the job */
    bool async; /* True if job is running asynchronously */
    bool asyncAbort; /* True if the job is running asynchronously and should be aborted */

};

int virJobObjInit(virJobObjPtr job);
int virJobObjFree(virJobObjPtr job);
virJobObjPtr virJobFromID(virJobID id);
virJobID virJobIDFromJob(virJobObjPtr job);
int virJobObjBegin(virJobObjPtr job, virJobType type);
int virJobObjEnd(virJobObjPtr job);
int virJobObjReset(virJobObjPtr job);
int virJobObjAbort(virJobObjPtr job);
