#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data *thread_func_args = (struct thread_data *) thread_param;
    struct timespec req, rem;

    thread_func_args->thread_complete_success = false;

    // wait
    req.tv_sec = 0;
    req.tv_nsec = 1000000*thread_func_args->wait_to_obtain_ms;
    int ret = nanosleep(&req, &rem);

    if (ret < 0) {
        int myerrno = errno;
        ERROR_LOG("sleep failure: errno: %d: %s", myerrno, strerror(myerrno));
        return thread_param;
    }

    // obtain mutex
    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (0 != ret) {
        ERROR_LOG("Unable to obtain mutex: %d: %s", ret, strerror(ret));
        return thread_param;
    }

    // wait
    req.tv_sec = 0;
    req.tv_nsec = 1000000*thread_func_args->wait_to_release_ms;
    ret = nanosleep(&req, &rem);

    if (ret < 0) {
        int myerrno = errno;
        ERROR_LOG("sleep failure: errno: %d: %s", myerrno, strerror(myerrno));
        return thread_param;
    }

    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if (0 != ret) {
        ERROR_LOG("sleep failure: : %d: %s", ret, strerror(ret));
        return thread_param;
    }

    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    struct thread_data *thread_func_args = malloc(sizeof(struct thread_data));
    if (NULL == thread_func_args) {
        ERROR_LOG("Unable to allocate memory");
        return false;
    }

    thread_func_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_func_args->wait_to_release_ms = wait_to_release_ms;
    thread_func_args->thread_complete_success = false;
    thread_func_args->mutex = mutex;
    int ret = pthread_create(thread, NULL, threadfunc, thread_func_args);

    if (0 != ret) {
        free(thread_func_args);
        ERROR_LOG("Unable to create a thread");
        return false;
    }

    return true;
}

