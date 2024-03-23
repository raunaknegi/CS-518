// File:thread-worker.c

//Group Members:
//Name: Anmol Arora 		Net ID: aa2640
//Name: Raunak Negi			Net ID: rn444
// iLab Server: ilab2.cs.rutgers.edu | ilab1.cs.rutgers.edu

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1


// defined timeQUANTUM to 12 ms,

#define QUANTUMTIME 12


#define READY 0
#define SCHEDULED 1
#define BLOCKED 2


/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>


typedef uint worker_t;

typedef struct TCB{
	/* add important states in a thread control block */
	// thread Id
	// thread status
	// thread context
	// thread stack
	// thread priority
	// And more ...

	// YOUR CODE HERE

	// thread Id
	worker_t t_id;

	// thread context
	ucontext_t t_context;

	// thread status
	int t_status;

	//thread priority
	int t_priority;

	// Number of QUANTUMs elapsed (for PSJF)
	int elapsed;

	//blocked thread
	int is_blocked;

	//thread yielding
	int is_yielded;

} tcb; 

/* mutex struct definition */
typedef struct worker_mutex_t {
	//flag is used to indicate whether mutex is available or nor
	//0 if available
	//1 is not available
	int locked;

	//pointer to the thread holding the mutex
	tcb * thread_control_block;

} worker_mutex_t;

//Helper function to define a tcb_node 
typedef struct tcb_node{
	tcb *thread_control_block;
	struct tcb_node * next;
}tcb_node;


//Helper function to define a queue for all the jobs
typedef struct queue{
	//head
	tcb_node * head;
	//tail
	tcb_node * tail;
} queue;


/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);



#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif

