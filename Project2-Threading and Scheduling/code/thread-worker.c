// File:thread-worker.c

//Group Members:
//Name: Anmol Arora 		Net ID: aa2640
//Name: Raunak Negi			Net ID: rn444
// iLab Server: ilab2.cs.rutgers.edu | ilab1.cs.rutgers.edu

#include "thread-worker.h"
// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define STACK_SIZE SIGSTKSZ
#define MAX_THREADS 200
#define RESET_JOBS 100

// Structure to compute timestamp of day
struct timeval tv;

// to genrartae and allocate unique therad id for each thread
worker_t threadID = 1;
// thread ids 2,3,4,--- are used for the scheduler threads

// highest priority in case of MLFQ (level 4)
// only que for P
struct queue *ready_queue; 

// 4 level MLFQ
struct queue *MLFQ_Priority1; // least priority
struct queue *MLFQ_Priority2;
struct queue *MLFQ_Priority3;

// Queue for all threads that get blocked
struct queue *blocked_threads = NULL;

// current thread which is working
tcb *working_thread = NULL;

long tot_cntx_switches = 0;

// long long int avg_turn_time;
// long long int avg_resp_time;

float avg_turn_time = 0.0;
float avg_resp_time = 0.0;

// state of the thread blocked or not
int thread_blocked_state = 0;

// guard condition for the scheduler to check if the thread has yielded or not in the current time quantum
int thread_yeilded = 0;
int main_thread_begin = 0;

// context of the scheduler
ucontext_t context_scheduler;
// thread of the scheduler
// worker *thread_scheduler;
tcb *thread_scheduler;

// signal handler
struct sigaction sig_action;

// initializing the timer
struct itimerval timer;

// thread exit value storage 
void *exit_threads[MAX_THREADS];
// thread termination t_status storage
int finished_threads[MAX_THREADS];

// Arrival time of thread
struct timespec arrival_time[MAX_THREADS] = {0};

// Schedule time for thread
struct timespec schedule_time[MAX_THREADS] = {0};

// Completion time for thread
struct timespec completion_time[MAX_THREADS] = {0};


void free_jobs();
void deallocate_thread(tcb *thread);
static void sched_rr(queue *queue);
static void sched_mlfq();
static void sched_psjf();
static void schedule();
void set_signal_quantum_timer(int signum);
void enqueue(tcb *newThread, queue *queue);

bool thread_initialized = false;

// context of the main thread
ucontext_t main_context;
// thread of the main thread
tcb *main_thread;


// Function to initialize a queue
queue* initialize_queue() {
    return (queue *)malloc(sizeof(queue));
}

// Function to initialize signal handlers
void initialize_signal_handlers() {
    struct sigaction sig_action;
    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_handler = &set_signal_quantum_timer;
    sigaction(SIGPROF, &sig_action, NULL);
}

void initialize_reset_timer() {
    struct sigaction sig_action;
    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_handler = &reset_MLFQs;
    sigaction(SIGPROF, &sig_action, NULL);
}

void setResetTimer(struct itimerval timer, int resetTime) {
    timer.it_value.tv_usec = resetTime * 1000;
    timer.it_value.tv_sec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);
}

// Function to initialize thread exit values and statuses
void initialize_thread_statuses_and_values() {
    for (int i = 0; i < MAX_THREADS; i++) {
        finished_threads[i] = 0;
        exit_threads[i] = NULL;
    }
}

// Function to initialize the scheduler context
void initialize_scheduler_context() {
    if (getcontext(&context_scheduler) < 0) {
        perror("getcontext");
        exit(1);
    }

    void *stack = malloc(STACK_SIZE);
    context_scheduler.uc_link = NULL;
    context_scheduler.uc_stack.ss_sp = stack;
    context_scheduler.uc_stack.ss_size = STACK_SIZE;
    context_scheduler.uc_stack.ss_flags = 0;

    // Modifying the context of the scheduler
    makecontext(&context_scheduler, schedule, 0);
}

// initializing 
void initialize()
{
    if (thread_initialized == false) {
        thread_initialized = true;

        // Initialize queues
        ready_queue = initialize_queue();  // Level 4
        MLFQ_Priority1 = initialize_queue();	   // Level 1
        MLFQ_Priority2 = initialize_queue();	   // Level 2
        MLFQ_Priority3 = initialize_queue();	   // Level 3
        blocked_threads = initialize_queue();   //blocked threads

        // Initialize signal handlers
        initialize_signal_handlers();

        // Initialize thread exit values and statuses
        initialize_thread_statuses_and_values();

        // Initialize the scheduler context
        initialize_scheduler_context();
    }
}


/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr,
				  void *(*function)(void *), void *arg)
{

	// - create Thread Control Block (TCB)
	// - create and initialize the context of this worker thread
	// - allocate space of stack for this thread to run
	// after everything is set, push this thread into run queue and
	// - make it ready for the execution.

	initialize();

	// Initializing main thread context and adding to shceduler queue
	if (main_thread_begin == 0)
	{

		getcontext(&main_context);
		if (main_thread_begin == 0)
		{	
			//setup TCB Values
			main_thread = (tcb *)malloc(sizeof(tcb));
			main_thread->t_id = threadID;
			threadID++;
			main_thread->t_status = READY;
			main_thread->t_context = main_context;
			main_thread->t_priority = 4;
			enqueue(main_thread, ready_queue);
			// set the locked to 1 since main thread has been added to the scheduler queue
			main_thread_begin = 1;

            // incrment total  context switches
			tot_cntx_switches++;

			// Transfer to scheduler context
			setcontext(&context_scheduler);
		}
	}

	// create a new thread control block
	tcb *newThread = (tcb *)malloc(sizeof(tcb));

	// initializing the context of the thread
	ucontext_t new_context;
	if (getcontext(&new_context) < 0)
	{
		perror("getcontext");
		exit(1);
	}

	//Setup stack for the current thread
	void *thread_stack = malloc(STACK_SIZE);
	new_context.uc_link = NULL;
	new_context.uc_stack.ss_sp = thread_stack;
	new_context.uc_stack.ss_size = STACK_SIZE;
	new_context.uc_stack.ss_flags = 0;

	// Modifying the context of thread by passing the function and arguments
	if (arg == NULL)
	{
		makecontext(&new_context, (void *)function, 0);
	}
	else
	{
		makecontext(&new_context, (void *)function, 1, arg);
	}

	// assigning a unique id to this thread
	*thread = threadID;
	newThread->t_id = *thread;
	// post incrementing the thread id
	threadID++;

	// setting the t_status of the thread to ready
	newThread->t_status = READY;

	// setting the t_priority of the thread to 4
	newThread->t_priority = 4;

	// setting the context of the thread
	newThread->t_context = new_context;

	// Add to job queue
	enqueue(newThread, ready_queue);

	return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{

	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	working_thread->t_status = READY;

	//lets schedular know that thread has yielded before its time slice
	working_thread->is_yielded=1;

	// Disabling the timer for thread and then performing context switch to scheduler context
	timer.it_value.tv_usec = 0;
	timer.it_value.tv_sec = 0;

	swapcontext(&(working_thread->t_context), &context_scheduler);
	// increment context switch count
	tot_cntx_switches++;

	return 0;
};

/* terminate a thread */
void worker_exit(void *pointer_val)
{
	// - de-allocate any dynamic memory created when starting this thread

	// Assigning the pointer_val to thread exit value for each thread

	int current_id = working_thread->t_id;

	if (pointer_val != NULL) {
		exit_threads[current_id] = pointer_val;
	} else {
		exit_threads[current_id] = NULL;
	}

	// Indicating this thread has exited
	finished_threads[current_id] = 1;

	deallocate_thread(working_thread);

	// Making current thread to null
	working_thread = NULL;

	// Disabling the timer for thread and then performing context switch to scheduler context
	timer.it_value.tv_usec = 0;
	timer.it_value.tv_sec = 0;

	struct timespec currentTime;
	clock_gettime(CLOCK_REALTIME, &currentTime);

	//thread left, so its completed
	completion_time[current_id] = currentTime;

	float turnaroundTime_thread = (completion_time[current_id].tv_sec - arrival_time[current_id].tv_sec) * 1000 + ((completion_time[current_id].tv_nsec - arrival_time[current_id].tv_nsec) / 1000000);
	float responseTime_thread = (schedule_time[current_id].tv_sec - arrival_time[current_id].tv_sec) * 1000 + ((schedule_time[current_id].tv_nsec - arrival_time[current_id].tv_nsec) / 1000000);

	// Main thread is not used here to calculate response and turnaround time

	
	if (threadID <= 1) {
		avg_turn_time = avg_turn_time + (turnaroundTime_thread);
		avg_resp_time = avg_resp_time + (responseTime_thread);
	} else {
		avg_turn_time = ((avg_turn_time * (threadID - 1)) + turnaroundTime_thread) / threadID;
		avg_resp_time = ((avg_resp_time * (threadID - 1)) + responseTime_thread) / threadID;
	}

 	// increment context switch count
	tot_cntx_switches++;
	setcontext(&context_scheduler);
		
};

void deallocate_thread(tcb *thread)
{
	// Freeing the stack and thread control block
	free((thread->t_context).uc_stack.ss_sp);
	free(thread);
}

/* Wait for thread termination */
int worker_join(worker_t thread, void **pointer_val)
{

	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread

	//waiting till thread is terminated
	while (finished_threads[thread] != 1)
	{
	}

	// If the pointer has something, assign exit value to the thread
	if (pointer_val != NULL)
	{

		*pointer_val = exit_threads[thread];
	}

	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
					  const pthread_mutexattr_t *mutexattr)
{
	// Checking if the mutex is null
	if (mutex == NULL)
	{
		return -1;
	}

	// Initializing the mutex locked to 0
	mutex->locked = 0;
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{

	if (!(__atomic_test_and_set(&mutex->locked, 1) != 1))
	{
		// Block the thread if mutex isnt available
		working_thread->t_status = BLOCKED;
		enqueue(working_thread, blocked_threads);

		// Set thread to blocked to not have it scheduled
		working_thread->is_blocked=1;
        
		// increment the counter
		tot_cntx_switches++;
		// switch the context to schedular
		swapcontext(&(working_thread->t_context), &context_scheduler);
	
	}

	// if mutex is there, lock it
	mutex->thread_control_block = working_thread;
	mutex->locked = 1;
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{	
	mutex->locked = 0;
	mutex = NULL;
	// Putting threads into run queue
	free_jobs();
	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
	// - de-allocate dynamic memory created in worker_mutex_init
	if (mutex == NULL)
	{
		return -1;
	}
	else
	{
		return 0;
	}
	return 0;
};

/* scheduler */
static void schedule()
{

// - schedule policy
#ifndef MLFQ
	// Choose PSJF 
	// CODE 1
	sched_psjf(ready_queue);
#else
	// Choose MLFQ
	// CODE 2
	sched_mlfq();
#endif
}

//Helper function to determine the shortest job with least elapsed time
tcb_node* identifySmallestElapsedNode(tcb_node *head) {
    if (!head) return NULL;

    tcb_node *smallestNode = head;

    for (tcb_node *current = head; current; current = current->next) {
        if (current->thread_control_block->elapsed < smallestNode->thread_control_block->elapsed) {
            smallestNode = current;
        }
    }

    return smallestNode;
}

// Helper function to adjust the queue if the smallest node is not at the head.
void adjustQueueForSmallestNode(tcb_node *head, tcb_node *smallestNode) {
    if (head == smallestNode) return;

    tcb_node *prev = NULL;
    for (tcb_node *current = head; current && current != smallestNode; prev = current, current = current->next);
    
	if (prev) {
        prev->next = smallestNode->next;
    }
    
    smallestNode->next = head;
    head = smallestNode;
}

//Helper function to check for thread yielding and thread execution (incrementing elapsed time)
void handleCurrentThread(queue *jobs_queue){
	working_thread->t_status = READY;

	if(working_thread->is_yielded==0)
	{
		// Increment elapsed time after time quantum is finished.
		working_thread->elapsed++;
	}
	else
	{
		// Reset to 0 for the next thread.
		working_thread->is_yielded=0;
	}

	// Back to the jobs_queue 
	enqueue(working_thread, jobs_queue);
}

//Helper function to handle timer functions for our quantum timer
void setQuantumTimer(struct itimerval timer, int quantumTime) {
    timer.it_value.tv_usec = quantumTime * 1000;
    timer.it_value.tv_sec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);
}

static void sched_psjf(queue *jobs_queue)
{
    // Check if the current thread is blocked or has left.
	if(working_thread !=NULL && working_thread->is_blocked==0)
    {
		handleCurrentThread(jobs_queue);
    }

	if (jobs_queue->head) {
		// Identify the smallest elapsed node.
		tcb_node *smallestNode = identifySmallestElapsedNode(jobs_queue->head);

		// Adjust jobs_queue to have smallestNode at the head.
		adjustQueueForSmallestNode(jobs_queue->head, smallestNode);

		// Dejobs_queue the thread with the smallest elapsed time.
		tcb_node *job = jobs_queue->head;
		jobs_queue->head = jobs_queue->head->next;

		if (!jobs_queue->head) {
			jobs_queue->tail = NULL;
		}

		// Schedule the dequeued thread.
		working_thread = job->thread_control_block;
		working_thread->t_status = SCHEDULED;
		working_thread->is_blocked=0;

		struct itimerval timer;
		setQuantumTimer(timer, QUANTUMTIME);

		// Record the time the thread is scheduled.
		struct timespec currentTime;
		clock_gettime(CLOCK_REALTIME, &currentTime);
		schedule_time[working_thread->t_id] = currentTime;

		// Update counters and set the context.
		tot_cntx_switches++;
		setcontext(&(working_thread->t_context));
	}
}

//Helper function to remove the first element
void dequeue_and_adjust(queue *queue, tcb_node **job) {
    *job = queue->head;
    queue->head = queue->head->next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    (*job)->next = NULL;
}

//Helper function to block the thread
void set_thread_state(tcb *thread) {
    thread->is_blocked = 0;
    thread->t_status = SCHEDULED;
}

//helper function to set the quantum timer
void set_quantum_timer() {
    timer.it_value.tv_usec = QUANTUMTIME * 1000;
    timer.it_value.tv_sec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);
}

//Helper function to get the scheduled time
void record_schedule_time(tcb *thread, struct timespec *time) {
    clock_gettime(CLOCK_REALTIME, time);
    schedule_time[thread->t_id] = *time;
}

//Round Robin for MLFQs
static void sched_rr(queue *queue)
{
	if (queue->head != NULL) {
        tcb_node *job;
        dequeue_and_adjust(queue, &job);
        
        working_thread = job->thread_control_block;
        set_thread_state(working_thread);
        
        free(job);
        set_quantum_timer();

        struct timespec currentTime;
        record_schedule_time(working_thread, &currentTime);
        tot_cntx_switches++;
        setcontext(&(working_thread->t_context));
    }
}

// Helper function to determine the appropriate queue for the thread based on its priority.
queue* determine_queue(int threadPriority, bool is_yielded) {
    if (is_yielded) {
        switch (threadPriority) {
            case 1: return MLFQ_Priority1;
            case 2: return MLFQ_Priority2;
            case 3: return MLFQ_Priority3;
            default: return ready_queue;
        }
    } else {
        switch (threadPriority) {
            case 1: return MLFQ_Priority1;
            case 2: working_thread->t_priority = 1; return MLFQ_Priority1;
            case 3: working_thread->t_priority = 2; return MLFQ_Priority2;
            default: working_thread->t_priority = 3; return MLFQ_Priority3;
        }
    }
}

//Helper function to reset priorities once we add all jobs to ready_queue
void reset_all_priorities(){
	tcb_node * start=ready_queue->head;
	while(start!=NULL){
		start->thread_control_block->t_priority=4;
		start=start->next;
	}
}

//Helper function to reset all the jobs to ready_queue
void reset_MLFQs(){
	queue* queues[] = { ready_queue, MLFQ_Priority3, MLFQ_Priority2, MLFQ_Priority1 };
	int num_queues = sizeof(queues) / sizeof(queues[0]);

	for (int i = 1; i < num_queues; i++) {
			if (queues[i]->head != NULL) {  // If the current MLFQ is not empty
				if (queues[0]->tail != NULL) {  // If ready_queue is not empty
					queues[0]->tail->next = queues[i]->head;
					queues[0]->tail = queues[i]->tail;
				} else {  // If ready_queue is empty
					queues[0]->head = queues[i]->head;
					queues[0]->tail = queues[i]->tail;
				}
				// Clear the current MLFQ
				queues[i]->head = NULL;
				queues[i]->tail = NULL;
        }
	}
	reset_all_priorities();
}

static void sched_mlfq()
{
	if(working_thread !=NULL && working_thread->is_blocked==0)
	{
		// t_priority of the current thread
		int threadPriority = working_thread->t_priority;
		// making the thread t_status to READY
		working_thread->t_status = READY;

		queue* target_queue = determine_queue(threadPriority, working_thread->is_yielded);
		enqueue(working_thread, target_queue);
		if (working_thread->is_yielded) {
        	working_thread->is_yielded = 0;
    	}
	}

	// Jobs Execution: ready_queue > MLFQ_Priority3 > MLFQ_Priority2 > MLFQ_Priority1

	queue* queues[] = { ready_queue, MLFQ_Priority3, MLFQ_Priority2, MLFQ_Priority1 };
	int num_queues = sizeof(queues) / sizeof(queues[0]);

	for (int i = 0; i < num_queues; i++) {
		if (queues[i]->head != NULL) {
			sched_rr(queues[i]);
			break;  // Exit once a non-empty queue is found and scheduled
		}
	}

	//reset all jobs

	//reset_MLFQs()
}

// DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{
	fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
	fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
	fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}

void set_signal_quantum_timer(int signum)
{
	// incrementing context switch
	tot_cntx_switches++;
	// once the QUANTUM is over, swap the context to scheduler
	swapcontext(&(working_thread->t_context), &context_scheduler);

}

//Helper function to create a node of type tcb_node
tcb_node *createNode(tcb *thread){
	tcb_node *temp = (tcb_node *)malloc(sizeof(tcb_node));
	temp->thread_control_block = thread;
	temp->next = NULL;
	return temp;
}

//Helper function to add threads to a job queue
void enqueue(tcb *newThread, struct queue *jobs_queue)
{
	tcb_node *temp = createNode(newThread);

	if ((jobs_queue->tail) != NULL)
	{
		// make the new thread as the next of the tail
		jobs_queue->tail->next = temp;
		// make the new thread as the tail
		jobs_queue->tail = temp;
	}
	else
	{
		// make the new thread as the head and tail
		jobs_queue->tail = temp;
		jobs_queue->head = temp;
	}

	struct timespec currentTime;
	clock_gettime(CLOCK_REALTIME, &currentTime);

	arrival_time[temp->thread_control_block->t_id] = currentTime;
}


// free threads in the blocked list
void free_jobs()
{
	// Traverse the blocked list and free the jobs in there
	tcb_node *curr = blocked_threads->head;
	tcb_node *prev;
	prev = curr;
	while (curr != NULL)
	{

		curr->thread_control_block->t_status = READY;
#ifndef MLFQ

		enqueue(curr->thread_control_block, ready_queue);

#else

		int threadPriority = curr->thread_control_block->t_priority;

		if (threadPriority == 4)
		{
			enqueue(curr->thread_control_block, ready_queue);
		}
		else if (threadPriority == 3)
		{
			enqueue(curr->thread_control_block, MLFQ_Priority3);
		}
		else if (threadPriority == 2)
		{
			enqueue(curr->thread_control_block, MLFQ_Priority2);
		}
		else
		{
			enqueue(curr->thread_control_block, MLFQ_Priority1);
		}

#endif

		curr = curr->next;
		// free up the memory 
		free(prev);
		prev = curr;
	}

	blocked_threads->head = NULL;
	blocked_threads->tail = NULL;
}
