#include <config.h>
/*#if 0
#define CALL_JOB_CALLBACK(callback,job)                 \
    if(callback){                                       \
        int retval=callback(job,job->privateData);      \
        if(retval <= 0){                                \
            goto error;                                 \
        }                                               \
    }
#define CALL_BEGIN_JOB_CALLBACK(callback,type,job)      \
    if(callback){                                       \
        int retval=callback(job,type,job->privateData); \
        if(retval <= 0){                                \
            goto error;                                 \
        }                                               \
    }
/* Since the job functions tend to use positive values as success
   and negitive or zero values as failure the callbacks should
   follow the same rules. * /
typedef int
(*virJobCallback)(virJobObjPtr job, void *privateData);
typedef int
(*virJobBeginCallback)(virJobObjPtr job,
                       virJobType type,
                       void *privateData);

int virJobObjInit2(virJobObjPtr job, void *privateData);
int virJobObjFree2(virJobObjPtr job, virJobCallback freeCallback);
int virJobObjCleanup2(virJobObjPtr job, virJobCallback cleanupCallback);
int virJobObjBegin2(virJobObjPtr job,
                   virJobType type,
                   virJobBeginCallback beginCallback);
int virJobObjEnd2(virJobObjPtr job, virJobCallback endCallback);
int virJobObjReset2(virJobObjPtr job, virJobCallback resetCallback);
int virJobObjAbort2(virJobObjPtr job, virJobCallback abortCallback);
int virJobObjSuspend2(virJobObjPtr job, virJobCallback suspendCallback);
int virJobObjResume2(virJobObjPtr job, virJobCallback resumeCallback);


int
virJobObjInit2(virJobObjPtr job, void *privateData)
{

    job->id=virAtomicIntInc(&global_id);

    if (virCondInit(&job->cond) < 0){
        return -1;
    }
    if (virMUtexInit(&job->lock) < 0){
        virCondDestroy(&job->cond);
        return -1;
    }

    job->privateData=privateData;

    jobAlistAdd(job_alist,job);

    return id;
}
void
virJobObjFree2(virJobObjPtr job, virJobCallback freeCallback)
{
    CALL_JOB_CALLBACK(freeCallback,job);
    virCondDestroy(&job->cond);
    jobAlistRemove(job->id);
    VIR_FREE(job);
}


int
virJobObjBegin2(virJobObjPtr job,
               virJobType type,
               virJobBeginCallback beginCallback)
{
    LOCK_JOB(job);
    CALL_BEGIN_JOB_CALLBACK(beginCallback, type, job);
    /*This should ultimately do more then this * /
    unsigned long long now;
    if (job->id <= 0) {
        goto error; /* job wasn't initialiazed* /
    }
    if (virTimeMillisNow(&now) < 0) {
        goto error;
    }
    job->type=type;
    job->flags|=VIR_JOB_FLAG_ACTIVE;
    job->start=now;
    job->owner=virThreadSelfID();
    return job->id;
 error:
    UNLOCK_JOB(job);
    return -1;
}

void
virJobObjCleanup2(virJobObjPtr job, virJobCallback cleanupCallback)
{
    CALL_JOB_CALLBACK(cleanupCallback,job);
    virCondDestroy(&job->cond);
    jobAlistRemove(job->id);
}

int
virJobObjEnd2(virJobObjPtr job, virJobCallback endCallback)
{
    CALL_JOB_CALLBACK(endCallback, job);
    if(job->type == VIR_JOB_NONE){
        return -1;
    }
    virJobObjReset(job);
    virCondSignal(&job->cond);

    return job->id;
}
int
virJobObjReset2(virJobObjPtr job, virJobCallback resetCallback)
{
    LOCK_JOB(job);
    CALL_JOB_CALLBACK(resetCallback, job);

    job->type=VIR_JOB_NONE;
    job->flags=VIR_JOB_FLAG_NONE;
    job->owner=0;
    job->async=0;
    job->asyncAbort=0;
    UNLOCK_JOB(job);

    return job->id;
}

int
virJobObjAbort2(virJobObjPtr job, virJobCallback abortCallback)
{
    LOCK_JOB(job);
    CALL_JOB_CALLBACK(abortCallback, job);

    SET_FLAG(job,ABORTED);
    UNLOCK_JOB(job);

    return job->id;
}
int
virJobObjWait2(virJobObjPtr job,
               unsigned long long limit)
{
    JOB_LOCK(job);
    int retval;
    while (CHECK_FLAG(job, ACTIVE)) {
        if (limit) {
            retval = virCondWaitUntil(&job->cond,&job->lock,limit);
        } else {
            retval = virCondWait(&job->cond,&job->lock);
        }
        if(retval < 0) {goto error;}
    }
    JOB_UNLOCK(job);
    return job->id;

 error:
    JOB_UNLOCK(job);
    if (errno == ETIMEDOUT) {
        return -2;
    } else {
        return -1;
    }
}
#endif
*/
