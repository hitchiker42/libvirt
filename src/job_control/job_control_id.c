int virJobObjInit(virJobObjPtr job){
    /* global job id, kept inside the function to prevent it
       from being modified elsewhere */
    static unsigned int id=0;

    /* Increment the id atomicially because multiple jobs can be 
       initialized at the same time, this doesn't check for overflow
       I don't know if it should or not*/
    job->id=virAtomicIntInc(&id);

    if (virCondInit(&job->cond) < 0){
        return -1;
    }
}

int virJobObjFree(virJobObjPtr job){
    virCondDestroy(&job->cond);
}
/*not sure how this will work yet*/
virJobID jobIDfromJobInternal(virJobObjPtr job);
virJobObjPtr jobfromJobIDInternal(virJobID id);
