#ifndef THREAD_CLASS_H
#define THREAD_CLASS_H

#include "uthreads.h"
#include <setjmp.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>

/*
 * Class for a single thread
 */
class thread {
private:
    int ID;
    int num_of_quantum;
    bool ready_state;
    char *stack;
public:
    void (*func)();  //Function of thread
    sigjmp_buf env{};


    //-----------------------------------------Class Methods---------------------------------------------------
    thread(int ID, void (*func)());  //Constructor

    ~thread(); //Destructor

    int get_id() const;

    char *get_stack() const;

    void block();

    void ready();

    void run();

    bool is_blocked() const;

    bool is_ready() const;

    int get_num_of_quantums() const;


};


#endif //THREAD_CLASS_H
