#include "thread_class.h"

thread::thread(int ID, void (*func)()) : ID{ID}, num_of_quantum{0}, ready_state{true}, func{func}//, jump_buffer{0}
{
    try {
        stack = new char[STACK_SIZE];
    }
    catch (std::bad_alloc&) {
        fprintf(stderr, "system error: stack allocation failed");
        exit(1);
    }

} //Constructor

thread::~thread() {
    delete[] stack;
}

int thread::get_id() const {
    return ID;
}

char *thread::get_stack() const {
    return stack;
}

void thread::block() {
    ready_state = false;
}

void thread::ready() {
    ready_state = true;
}

void thread::run() {
    ready_state = true;
    ++num_of_quantum;
}

int thread::get_num_of_quantums() const {
    return num_of_quantum;
}

bool thread::is_blocked() const {
    return !ready_state;
}

bool thread::is_ready() const {
    return ready_state;
}


