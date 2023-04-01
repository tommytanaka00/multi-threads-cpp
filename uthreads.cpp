#include "thread_class.h"
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <queue>
#include <vector>
#include <cstdlib>
#include <list>
#include <map>


#define SYS_ERROR_MESSAGE "system error: "
#define LIB_ERROR_MESSAGE "thread library error: "


typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

char sigpromask_ERROR_msg[] = "sigpromask error\n";
char sigaction_ERROR_msg[] = "sigaction error\n";
char sigaddset_ERROR_msg[] = "sigaddset error\n";
char sigemptyset_ERROR_msg[] = "sigemptyset error\n";

//-----------------------------------------Global Variables-----------------------------------------------------

/* A dummy function for the main thread */
void main_thread_empty_function() {}

/* The set containing SIGVTALRM for blocking and unblocking */
sigset_t set_of_blocked_signals;

/* The map of all the threads that exist currently */
std::map<int, thread *> all_threads;

/* A queue of all the ready threads */
std::list<thread *> ready_threads;

/* A list for all the locked threads */
std::list<thread *> locked_threads;

/* A min-heap for the minimum available ID */
std::priority_queue<int, std::vector<int>, std::greater<int>> min_available_ID;

/* A lock for the mutex */
int locking_thread_id = -1;

/* A vector for all the threads that need to be deleted */
thread* thread_to_delete;

/* Number of total quantums */
int total_quanta = 0;

/* For the timer */
struct sigaction sa = {{nullptr}};
struct itimerval timer{};

//-----------------------------------------Helper Functions-----------------------------------------------------
/* Prints error message warning for all signal functions if the
 * return calue of system function is -1 and exits with 1*/
void sigcheck(int ret_val_of_SYS_func, const char* message)
{
    if (ret_val_of_SYS_func < 0)
    {
        fprintf(stderr, SYS_ERROR_MESSAGE);
        fprintf(stderr, "%s", message);
        exit(1);
    }
}

/* Prints error message warning that thread tid does not exist
 * and returns -1; */
int thread_does_not_exist_ERROR(int tid)
{
    fprintf(stderr, LIB_ERROR_MESSAGE);
    fprintf(stderr, "No thread with ID %d exists.\n", tid);
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return -1;
}



/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

/* Gets the minimum available ID for
   when creating a new thread */
int get_min_available_ID() {
    if (min_available_ID.empty()) {
        return (int) all_threads.size();
    }
    int ID = min_available_ID.top();
    min_available_ID.pop();
    return ID;
}

/**
 * Function that SIGVTALRM calls.
 * Switches the thread to the next thread in ready list.
 */
void switchThreads(int sig) {
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM
    if (sig){} //To remove "sig not used"

    int ret_val = sigsetjmp(ready_threads.front()->env, 1);

    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM

    if (ret_val == 1) {  // If we jumped from siglongjump to there ^
        //todo: reset timer?

        // Reset the virtual timer
        if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
            fprintf(stderr, SYS_ERROR_MESSAGE);
            fprintf(stderr, "setitimer error.\n");
            exit(1);
        }

        sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
        return;  // go back to where the thread left off
    }

    if (ready_threads.front()->is_blocked()) {
        ready_threads.pop_front();
    }
    else {
        ready_threads.push_back(ready_threads.front());
        ready_threads.pop_front();
    }

    if (thread_to_delete != nullptr) {
        delete thread_to_delete;
        thread_to_delete = nullptr;
    }

    ready_threads.front()->run();
    ++total_quanta;

    // Reset the virtual timer
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        fprintf(stderr, SYS_ERROR_MESSAGE);
        fprintf(stderr, "setitimer error.\n");
        exit(1);
    }
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM

    siglongjmp(ready_threads.front()->env, 1);

}


//-----------------------------------------Library Functions-----------------------------------------------------

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if (quantum_usecs < 1) {
        fprintf(stderr, LIB_ERROR_MESSAGE); //(" parameter is not positive")
        fprintf(stderr, " parameter is not positive\n");
        return -1;
    }
    int spawned = uthread_spawn(main_thread_empty_function);  //spawn main thread
    if (spawned == -1)  //If failed to spawn
    {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, "failed to spawn main thread.\n");
        return -1;
    }

    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &switchThreads;
    sigcheck(sigaction(SIGVTALRM, &sa, nullptr), sigaction_ERROR_msg);

    // Configure the timer to expire after 1 sec... */
    timer.it_value.tv_sec = 0;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs;        // first time interval, microseconds part

    // configure the timer to expire every quantum_usec sec after that.
    timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usecs;    // following time intervals, microseconds part

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        fprintf(stderr, SYS_ERROR_MESSAGE);
        fprintf(stderr, "setitimer error.\n");
        exit(1);
    }

    // Add SIGVTALRM to set of signals that we will be able to block
    sigcheck(sigaddset(&set_of_blocked_signals, SIGVTALRM), sigaddset_ERROR_msg);

    // Increment quantum of main thread
    ready_threads.front()->run();
    ++total_quanta;

    return 0;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)()) {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM

    if (all_threads.size() >= MAX_THREAD_NUM) {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, " cannot add any more threads; max limit reached.\n");
        return -1;
    }

    int ID = get_min_available_ID();
    auto *thread_ptr = new thread(ID, f);

    // If failed to allocate stack, terminate whole thing todo: or only terminate this one thread?
    if (!thread_ptr->get_stack()) {
        uthread_terminate(0);
    }

    ready_threads.push_back(thread_ptr);  //push to ready list.

    all_threads[ID] = thread_ptr;  //Add to all the threads

    address_t sp, pc;

    sp = (address_t) thread_ptr->get_stack() + STACK_SIZE - sizeof(address_t);
    pc = (address_t) thread_ptr->func;
    sigsetjmp(all_threads[thread_ptr->get_id()]->env, 1);
    all_threads[thread_ptr->get_id()]->env->__jmpbuf[JB_SP] = (long) translate_address(sp);
    all_threads[thread_ptr->get_id()]->env->__jmpbuf[JB_PC] = (long) translate_address(pc);
    sigcheck(sigemptyset(&all_threads[thread_ptr->get_id()]->env->__saved_mask), sigemptyset_ERROR_msg);


    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return ID;
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid) {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM
    if (all_threads[tid] == nullptr)
    {
        return thread_does_not_exist_ERROR(tid);
    }
    if (tid == 0) {
        //release every thread and everything
        ready_threads.clear();
        for (auto &iter : all_threads) {
            delete iter.second;
        }
        all_threads.clear();
        if (thread_to_delete != nullptr) {
            delete thread_to_delete;
            thread_to_delete = nullptr;
            locked_threads.clear();
        }
        exit(0);
    }

    auto *thread_pt = all_threads[tid];

    all_threads.erase(tid); //Remove from map

    min_available_ID.push(tid); // Add to MinHeap

    if (thread_pt->get_id() == locking_thread_id) {
        locking_thread_id = -1;
        for (auto &locked: locked_threads){
            locked->ready();
        }
        ready_threads.merge(locked_threads);
    }

    if (ready_threads.front()->get_id() == tid) {
        thread_to_delete = thread_pt;
        thread_pt->block();
        switchThreads(0);
    }

    if (thread_pt->is_blocked()) {
        for (auto iter = locked_threads.begin(); iter != locked_threads.end(); ++iter) {
            if ((*iter)->get_id() == tid) {
                ready_threads.erase(iter);
                break;
            }
        }
    } else {
        for (auto iter = ready_threads.begin(); iter != ready_threads.end(); ++iter) {
            if ((*iter)->get_id() == tid) {
                ready_threads.erase(iter);
                break;
            }
        }
    }
    delete thread_pt;
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return 0;
}


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid) {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM

    if (all_threads[tid] == nullptr) {
        return thread_does_not_exist_ERROR(tid);
    }
    if (tid == 0) {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, "Cannot block main thread.\n");
        sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
        return -1;
    }

    //Idea: send a signal and so as to stop the current running thread (if tid is the running thread)
    all_threads[tid]->block();

    if (ready_threads.front()->get_id() == tid) {  //If called
        switchThreads(0);
    } else {
        for (auto thrd = ready_threads.begin(); thrd != ready_threads.end(); ++thrd) {
            if ((*thrd)->get_id() == tid) {
                ready_threads.erase(thrd);
                break;
            }
        }
    }
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return 0;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM

    if (all_threads[tid] == nullptr)  //this is the idea, how do we do it??
    {
        return thread_does_not_exist_ERROR(tid);
    }
    if (all_threads[tid]->is_blocked()) {
        all_threads[tid]->ready();
        ready_threads.push_back(all_threads[tid]);
    }

    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return 0;
}


/*
 * Description: This function tries to acquire a mutex.
 * If the mutex is unlocked, it locks it and returns.
 * If the mutex is already locked by different thread, the thread moves to BLOCK state.
 * In the future when this thread will be back to RUNNING state,
 * it will try again to acquire the mutex.
 * If the mutex is already locked by this thread, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_lock() {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM


    if (locking_thread_id == ready_threads.front()->get_id()) {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, "Cannot lock the same thread twice.\n");
        sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
        return -1;
    }
    while (locking_thread_id != -1) {
        locked_threads.push_back(ready_threads.front());
        ready_threads.front()->block();
        switchThreads(0);
        // This is because switchThreads unblocks the signal so we have to immediately block
        sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM
        if (locking_thread_id == -1)
        {
            break;
        }
    }

    locking_thread_id = ready_threads.front()->get_id();

    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return 0;
}


/*
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock() {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM

    if (locking_thread_id == -1)
    {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, "Mutex already unlocked.\n");
        sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
        return -1;
    }

    // If other thread already locked the mutex
    if (ready_threads.front()->get_id() != locking_thread_id) {
        fprintf(stderr, LIB_ERROR_MESSAGE);
        fprintf(stderr, "Cannot unlock mutex that was locked by another thread.\n");
        sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
        return -1;
    }
    locking_thread_id = -1;
    for (auto &locked: locked_threads){
        locked->ready();
    }
    ready_threads.merge(locked_threads);
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return 0;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid() {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM
    int cur_id = ready_threads.front()->get_id();
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return cur_id;
}


/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums() {
    //Block SIGVTALRM until we finish with this function
    return total_quanta;
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid) {
    //Block SIGVTALRM until we finish with this function
    sigcheck(sigprocmask(SIG_BLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg);  // Block SIGVTALRM
    if (all_threads[tid] == nullptr) {
        return thread_does_not_exist_ERROR(tid);
    }
    int q = all_threads[tid]->get_num_of_quantums();
    sigcheck(sigprocmask(SIG_UNBLOCK, &set_of_blocked_signals, nullptr), sigpromask_ERROR_msg); // Unblock SIGVTALRM
    return q;
}
