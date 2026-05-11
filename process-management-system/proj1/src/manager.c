/**
 * @mainpage Process Simulation
 *
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef OPENMP
#include <omp.h>
#endif
#include "proc_structs.h"
#include "proc_syntax.h"
#include "logger.h"
#include "manager.h"

#define LOWEST_PRIORITY INT_MAX // 0 is highest, setting INT_kAX as lowest

/* --- function prototypes ------------------------------------------------- */

static pcb_queue_t terminatedq;
static pcb_queue_t waitingq;
static pcb_queue_t readyq;
static resource_t* system_resources;
static int total_jobs = 0;

// locks to be used  for threads for safe concurrent access
#ifdef OPENMP
omp_lock_t ready_lock;
omp_lock_t waiting_lock;
omp_lock_t terminated_lock;
omp_lock_t resource_lock;
#endif

bool_t terminate();
void schedule_fcfs(int num_threads);
void schedule_rr(int quantum, int num_threads);
void schedule_priority(int num_threads);
bool_t higher_priority(int, int);

void execute_instr(pcb_t* proc);
void request_resource(pcb_t* proc);
void release_resource(pcb_t* proc);
bool_t acquire_resource(pcb_t* proc, char* resource_name);

void enqueue_pcb(pcb_t* proc, pcb_queue_t* queue, int status);
pcb_t* dequeue_pcb(pcb_queue_t* queue);

int get_num_threads(int num_args, char** argv);
char* get_data(int num_args, char** argv);
int get_algo(int num_args, char** argv);
int get_time_quantum(int num_args, char** argv);
void print_args(int num_thr, char* data, int sched, int tq);

int main(int argc, char** argv)
{
	int num_thr      = get_num_threads(argc, argv);
	char* data       = get_data(argc, argv);
	int scheduler    = get_algo(argc, argv);
	int time_quantum = get_time_quantum(argc, argv);
	print_args(num_thr, data, scheduler, time_quantum);
	bool_t success = FALSE;

	if (strcmp(data, "generate") == 0) {
#ifdef DEBUG_MNGR
		printf("****Generate processes and initialise the system\n");
#endif
		success = init_loader_from_generator();
	} else {
#ifdef DEBUG_MNGR
		printf("Parse process file and initialise the system: %s \n", data);
#endif
		success = init_loader_from_files(data);
	}

	if (success) {
		init_system();
		system_resources = get_resources();
		//  OpenMP locks
#ifdef OPENMP
		omp_init_lock(&ready_lock);
		omp_init_lock(&waiting_lock);
		omp_init_lock(&terminated_lock);
		omp_init_lock(&resource_lock);
#endif
		printf("***********Scheduling processes************\n");
		schedule_processes(num_thr, scheduler, time_quantum);
		// Destroying OpenMP locks
#ifdef OPENMP
		omp_destroy_lock(&ready_lock);
		omp_destroy_lock(&waiting_lock);
		omp_destroy_lock(&terminated_lock);
		omp_destroy_lock(&resource_lock);
#endif
		dealloc_data_structures();
	} else {
		printf("Error: no processes to schedule\n");
	}

	return EXIT_SUCCESS;
}

/**
 * @brief The linked list of loaded processes is moved to the readyqueue.
 *    The waiting and terminated queues are intialised to empty
 */
void init_system(void)
{
	readyq.first = longterm_scheduler();
	// Initializing last
	readyq.last = NULL;

	pcb_t* temp = readyq.first;

	while (temp) {
		// Updating the states of each process pcb added to readyq
		temp->state = READY;
		// Updating  counter used to detect termination
		total_jobs++;
		if (!temp->next) {
			readyq.last = temp;
		}
		temp = temp->next;
	}

	waitingq.last     = NULL;
	waitingq.first    = NULL;
	terminatedq.last  = NULL;
	terminatedq.first = NULL;

	log_queue(readyq.first, "Ready");
	log_queue(waitingq.first, "Waiting");
	log_queue(terminatedq.first, "Terminated");
	log_msg("\n");
}

/** @brief Schedules each instruction of each process */
void schedule_processes(int num_thr, schedule_t sched_type, int quantum)
{
	switch (sched_type) {
	case PRIOR:
		schedule_priority(num_thr);
		break;
	case RR:
		schedule_rr(quantum, num_thr);
		break;
	case FCFS:
		schedule_fcfs(num_thr);
		break;
	default:
		break;
	}
}

/** @brief Return true when there are no more processes to schedule */
bool_t terminate()
{

#ifdef OPENMP
	omp_set_lock(&ready_lock);
#endif
	// processes to schedule exist
	if (readyq.first != NULL) {
		return FALSE;
	}

	int waiting_count    = 0;
	int terminated_count = 0;

	// terminated_lock
#ifdef OPENMP
	omp_set_lock(&terminated_lock);
#endif
	// processes in the terminated queue
	pcb_t* temp = terminatedq.first;
	while (temp) {
		terminated_count++;
		temp = temp->next;
	}
	// Unsetting terminated_lock and setting waiting_lock
#ifdef OPENMP
	omp_unset_lock(&terminated_lock);
	omp_set_lock(&waiting_lock);
#endif

	// processes in the waiting queue
	temp = waitingq.first;
	while (temp) {
		waiting_count++;
		temp = temp->next;
	}
#ifdef OPENMP
	omp_unset_lock(&waiting_lock);
#endif

	// !readyq AND number in terminated + waiting queue = to total number
#ifdef OPENMP
	omp_unset_lock(&ready_lock);
#endif
	return (terminated_count + waiting_count == total_jobs);
}

/**
 * @brief Call the longterm schedule to check for new arrivals
 * If there are new arrivals, call
 *  log_pcbs("New arrivals in ready queue", new_arrivals);
 */
void load_new_processes(void)
{
	pcb_t* new_arrivals = longterm_scheduler();

	if (new_arrivals) {
		pcb_t* temp = new_arrivals;
		while (temp) {
			temp->state = READY;
#ifdef OPENMP
			omp_set_lock(&ready_lock);
#endif
			enqueue_pcb(temp, &readyq, READY);
			total_jobs++;
#ifdef OPENMP
			omp_unset_lock(&ready_lock);
#endif
			temp = temp->next;
		}
		log_pcbs("New arrivals in ready queue", new_arrivals);
	}
}

/** Schedules processes using FCFS scheduling
 *@param the number of threads
 */
void schedule_fcfs(int num_threads)
{

	// Setting num_threads

#ifdef OPENMP
#pragma omp parallel num_threads(num_threads)
	{
#endif

		// until the termination condition is met
		while (!terminate()) {
			// remove the process from the ready queue
			pcb_t* pcb = NULL;
#ifdef OPENMP
			omp_set_lock(&ready_lock);
#endif
			pcb = dequeue_pcb(&readyq);
#ifdef OPENMP
			omp_unset_lock(&ready_lock);
#endif

			if (!pcb) {
				load_new_processes();
				// check again for processes
				continue;
			}
			// change it’s state to Running
			pcb->state = RUNNING;
			log_running(pcb, "Running");

			while (pcb->next_instruction) {
				// execute the instructions
				execute_instr(pcb);
				// If the process is moved to waitingq, stop execution
				if (pcb->state == WAITING) {
					break;
				}
				load_new_processes();
			}
			// no more instructions, move to terminated queue
			if (!pcb->next_instruction) {

				pcb->state = TERMINATED;
#ifdef OPENMP
				omp_set_lock(&terminated_lock);
#endif

				enqueue_pcb(pcb, &terminatedq, TERMINATED);
#ifdef OPENMP
				omp_unset_lock(&terminated_lock);
#endif
				log_terminated(pcb->process->name);
			}
		}
#ifdef OPENMP
	}
#endif
}

/** Schedules processes using the Round-Robin scheduler.
 *@param quantum instructions a process executes before being preempted
 *@param the number of threads
 */
void schedule_rr(int quantum, int num_threads)
{

	// seting num_threads
#ifdef OPENMP
#pragma omp parallel num_threads(num_threads)
	{
#endif
		// until the termination condition is met
		while (!terminate()) {
			// remove the process from the ready queue
			pcb_t* pcb = NULL;
#ifdef OPENMP
			omp_set_lock(&ready_lock);
#endif
			pcb = dequeue_pcb(&readyq);
#ifdef OPENMP
			omp_unset_lock(&ready_lock);
#endif
			if (!pcb) {
				load_new_processes();
				// check again for processes
				continue;
			}
			// change it’s state to Running
			pcb->state = RUNNING;
			log_running(pcb, "Running");

			int executed_instructions = 0;

			// Execute up to quantum instructions
			while (pcb->next_instruction && executed_instructions < quantum) {
				execute_instr(pcb);
				executed_instructions++;
				load_new_processes();

				// If the process is moved to waitingq, stop execution
				if (pcb->state == WAITING) {
					break;
				}
			}

			// no more instructions, move to terminated queue
			if (!pcb->next_instruction) {
				pcb->state = TERMINATED;
#ifdef OPENMP
				omp_set_lock(&terminated_lock);
#endif
				enqueue_pcb(pcb, &terminatedq, TERMINATED);
#ifdef OPENMP
				omp_unset_lock(&terminated_lock);
#endif
				log_terminated(pcb->process->name);

			} else if (executed_instructions == quantum &&
			 pcb->state != WAITING) {
				pcb->state = READY;
#ifdef OPENMP
				omp_set_lock(&ready_lock);
#endif
				enqueue_pcb(pcb, &readyq, READY);
#ifdef OPENMP
				omp_unset_lock(&ready_lock);
#endif
				//  new process
				continue;
			}
		}
#ifdef OPENMP
	}
#endif
}

/** Schedules processes using priority scheduling with preemption
 *@param the number of threads
 */
void schedule_priority(int num_threads)

 {
#ifdef OPENMP
#pragma omp parallel num_threads(num_threads)
  {
#endif
   // running process
   pcb_t* running_pcb = NULL;
// until the termination condition is met
while (!terminate()) {
#ifdef OPENMP
	omp_set_lock(&waiting_lock);
#endif
	// Check waiting queue for processes that are now READY
	pcb_t* temp = waitingq.first;
	pcb_t* prev = NULL;
	while (temp) {
		pcb_t* next_waiting = temp->next;
		if (temp->state == READY) {
#ifdef OPENMP
			omp_set_lock(&ready_lock);
#endif
			enqueue_pcb(temp, &readyq, READY);
#ifdef OPENMP
			omp_unset_lock(&ready_lock);
#endif
			// Remove from waiting queue
			if (prev) {
				prev->next = next_waiting;
			} else {
				waitingq.first = next_waiting;
			}
			if (waitingq.last == temp) {
				waitingq.last = prev;
			}
		} else {
			prev = temp;
		}
		temp = next_waiting;
	}
#ifdef OPENMP
	omp_unset_lock(&waiting_lock);

	omp_set_lock(&ready_lock);
#endif

	// no processes in the ready queue
	if (!readyq.first) {
#ifdef OPENMP
		omp_unset_lock(&ready_lock);
#endif
		load_new_processes();
		continue;
	}
	// finding highest priority in readyq
	pcb_t* highest_pcb  = NULL;
	pcb_t* previous     = NULL;
	pcb_t* current      = readyq.first;
	pcb_t* highest_prev = NULL;

	while (current) {
		if (!highest_pcb ||
		 higher_priority(current->priority, highest_pcb->priority)) {
			highest_pcb  = current;
			highest_prev = previous;
		}
		previous = current;
		current  = current->next;
	}

	// no process available, continue
	if (!highest_pcb) {
#ifdef OPENMP
		omp_unset_lock(&ready_lock);
#endif
		continue;
	}

	// dispatch the process
	if (highest_prev) {
		highest_prev->next = highest_pcb->next;
	} else {
		readyq.first = highest_pcb->next;
	}
	if (readyq.last == highest_pcb) {
		readyq.last = highest_prev;
	}
	highest_pcb->next = NULL;
#ifdef OPENMP
	omp_unset_lock(&ready_lock);
#endif

	// currently running process back to the readyq if not highest
	// priority
	if (running_pcb &&
	 higher_priority(highest_pcb->priority, running_pcb->priority)) {
		running_pcb->state = READY;
#ifdef OPENMP
		omp_set_lock(&ready_lock);
#endif
		enqueue_pcb(running_pcb, &readyq, READY);
#ifdef OPENMP
		omp_unset_lock(&ready_lock);
#endif
		running_pcb = NULL;
	}

	// highest priority runs, its status updated
	highest_pcb->state = RUNNING;
	log_running(highest_pcb, "Running");
	running_pcb = highest_pcb;

	// execute the instructions until wait, termination, preemption
	while (running_pcb->next_instruction) {
		execute_instr(running_pcb);

		// If the process is moved to waitingq, stop execution
		if (running_pcb->state == WAITING) {
			running_pcb = NULL;
			break;
		}

		// preemption
		load_new_processes();
#ifdef OPENMP
		omp_set_lock(&waiting_lock);
#endif
		// Check waiting queue for processes that have become READY
		temp = waitingq.first;
		prev = NULL;
		while (temp) {
			pcb_t* next_waiting = temp->next;
			if (temp->state == READY) {
#ifdef OPENMP
				omp_set_lock(&ready_lock);
#endif
				enqueue_pcb(temp, &readyq, READY);
#ifdef OPENMP
				omp_unset_lock(&ready_lock);
#endif
				// Remove from waiting queue
				if (prev) {
					prev->next = next_waiting;
				} else {
					waitingq.first = next_waiting;
				}
				if (waitingq.last == temp) {
					waitingq.last = prev;
				}
			} else {
				prev = temp;
			}
			temp = next_waiting;
		}
#ifdef OPENMP
		omp_unset_lock(&waiting_lock);

		omp_set_lock(&ready_lock);
#endif
		pcb_t* new_highest = NULL;
		current            = readyq.first;
		while (current) {
			if (!new_highest ||
			 higher_priority(current->priority, new_highest->priority)) {
				new_highest = current;
			}
			current = current->next;
		}
#ifdef OPENMP
		omp_unset_lock(&ready_lock);
#endif
		if (new_highest &&
		 (!running_pcb ||
		  higher_priority(new_highest->priority, running_pcb->priority))) {
			if (running_pcb) {
				running_pcb->state = READY;
#ifdef OPENMP
				omp_set_lock(&ready_lock);
#endif
				enqueue_pcb(running_pcb, &readyq, READY);
#ifdef OPENMP
				omp_unset_lock(&ready_lock);
#endif
			}
			running_pcb = NULL;
			break;
		}
	}

	// no more instructions, move to terminated queue
	if (running_pcb && !running_pcb->next_instruction) {
		running_pcb->state = TERMINATED;
#ifdef OPENMP
		omp_set_lock(&terminated_lock);
#endif
		enqueue_pcb(running_pcb, &terminatedq, TERMINATED);
#ifdef OPENMP
		omp_unset_lock(&terminated_lock);
#endif
		log_terminated(running_pcb->process->name);
		running_pcb = NULL;
	}
}
#ifdef OPENMP
}
#endif
}

/** @brief Return TRUE if pr1 has a higher priority than pr2 */
bool_t higher_priority(int pr1, int pr2)
{
	// note the lower the value, the higher the priority(pr1 over pr2)
	return pr1 < pr2;
}

/** Call the correct function to execute the next instruction of the process
 *  If there is no instruction to execute, call:
 *   log_msg("Error: No instruction to execute");
 *  After successful execution, call:
 *   log_running((pcb, "Running");
 *   log_queue((readyq.first, "Ready");
 *   log_queue((waitingq.first, "Waiting");
 *   log_queue((terminatedq.first, "Termianted");
 *   log_msg("\n");
 **/
void execute_instr(pcb_t* pcb)
{
	//  no instruction to execute
	if (!pcb || !pcb->next_instruction) {
		log_msg("Error: No instruction to execute");
		return;
	}
	instr_t* instr = pcb->next_instruction;
	pcb->state     = RUNNING;
	switch (instr->type) {
	case REQ_OP:
		request_resource(pcb);
		break;
	case REL_OP:
		release_resource(pcb);
		break;
	case SEND_OP:
		break;
	case RECV_OP:
		break;
	default:
		log_msg("Unknown instruction type\n");
		break;
	}
#ifdef OPENMP
	omp_set_lock(&ready_lock);
	omp_set_lock(&waiting_lock);
	omp_set_lock(&terminated_lock);
#endif
	log_running(pcb, "Running");
	log_queue(readyq.first, "Ready");
	log_queue(waitingq.first, "Waiting");
	log_queue(terminatedq.first, "Termianted");
#ifdef OPENMP
	omp_unset_lock(&terminated_lock);
	omp_unset_lock(&waiting_lock);
	omp_unset_lock(&ready_lock);
#endif
	log_msg("\n");

	pcb->next_instruction = instr->next;
}

/**
 * @brief Handle the request resource instruction
 *
 * If the resource could not be acquired move the process to the waiting
 * queue If the resource was successfully acquired call:
 *  log_request_acquired(cur_pcb->process->name, instr->resource_name);
 *  log_avail_resources(system_resources);
 *  log_msg("\n");
 */
void request_resource(pcb_t* cur_pcb)
{
	// Null process check
	if (!cur_pcb || !cur_pcb->next_instruction) {
		return;
	}

	char* resource_name = cur_pcb->next_instruction->resource_name;

	//  attempt acquire
	bool_t acquired = acquire_resource(cur_pcb, resource_name);

	// resource was successfully acquired
	if (acquired) {
		log_request_acquired(cur_pcb->process->name, resource_name);
		log_avail_resources(system_resources);
		log_msg("\n");
	} else {
		// Resource is already allocated
		cur_pcb->state = WAITING;
#ifdef OPENMP
		omp_set_lock(&waiting_lock);
#endif
		enqueue_pcb(cur_pcb, &waitingq, WAITING);
#ifdef OPENMP
		omp_unset_lock(&waiting_lock);
#endif
	}
}

/**
 * @brief Acquire a resource for a process if it is available
 * NB: Do not remove the resource from the system_resources list
 *     Update the allocated field
 */
bool_t acquire_resource(pcb_t* cur_pcb, char* resource_name)
{
	// hold the result
	bool_t result = FALSE;
#ifdef OPENMP
	omp_set_lock(&resource_lock);
#endif
	resource_t* resource = system_resources;

	// Find the specified resource
	while (resource) {
		// If the resource is found
		if (strcmp(resource->name, resource_name) == 0) {
			// If the resource is not allocated
			if (resource->allocated == NULL) {
				// Update the allocated field
				resource->allocated = cur_pcb;
				result              = TRUE;
			}
			break;
		}
		resource = resource->next;
	}
#ifdef OPENMP
	omp_unset_lock(&resource_lock);
#endif
	return result;
}

/**
 * @brief Execute the release instruction for the process
 *  Update the allocated field
 *  Find a process that is waiting for a resource with the same name and
 * move it to the ready queue
 *
 * If the release was successful, call:
 *  log_release_released(pcb->process->name, resource_name);
 *  log_avail_resources(system_resources);
 *  log_msg("\n");
 * If the release was not successful, call:
 *  log_release_error(pcb->process->name, resource_name);
 *
 */
void release_resource(pcb_t* pcb)
{

	// Null process check
	if (!pcb || !pcb->next_instruction ||
	 !pcb->next_instruction->resource_name) {
		return;
	}

	char* resource_name = pcb->next_instruction->resource_name;
#ifdef OPENMP
	omp_set_lock(&resource_lock);
#endif
	resource_t* res = system_resources;
	while (res) {
		// resource found
		if (strcmp(res->name, resource_name) == 0) {
			break;
		}
		res = res->next;
	}
	// Check if process owns the resource before releasing
	if (!res || res->allocated != pcb) {
#ifdef OPENMP
		omp_unset_lock(&resource_lock);
#endif
		log_release_error(pcb->process->name, resource_name);
		return;
	}

	// If resource is found
	// resource is available again
	res->allocated = NULL;
#ifdef OPENMP
	omp_unset_lock(&resource_lock);

	omp_set_lock(&waiting_lock);
#endif
	// find a process waiting
	pcb_t* waiting_pcb  = waitingq.first;
	pcb_t* prev_waiting = NULL;
	while (waiting_pcb) {
		if (strcmp(waiting_pcb->next_instruction->resource_name,
		     resource_name) == 0) {
			break;
		}
		prev_waiting = waiting_pcb;
		waiting_pcb  = waiting_pcb->next;
	}
	// If a process was waiting, remove it from waiting queue
	if (waiting_pcb) {
#ifdef OPENMP
		omp_set_lock(&resource_lock);
#endif
		res->allocated = waiting_pcb;
#ifdef OPENMP
		omp_unset_lock(&resource_lock);
#endif

		if (prev_waiting) {
			prev_waiting->next = waiting_pcb->next;
		} else {
			waitingq.first = waiting_pcb->next;
		}
		if (waitingq.last == waiting_pcb) {
			waitingq.last = prev_waiting;
		}
		// update status and add to ready queue
		waiting_pcb->state = READY;
		if (waiting_pcb->next_instruction) {
			waiting_pcb->next_instruction = waiting_pcb->next_instruction->next;
		}
#ifdef OPENMP
		omp_set_lock(&ready_lock);
#endif
		enqueue_pcb(waiting_pcb, &readyq, READY);
#ifdef OPENMP
		omp_unset_lock(&ready_lock);
#endif
	}
#ifdef OPENMP
	omp_unset_lock(&waiting_lock);
#endif
	log_release_released(pcb->process->name, resource_name);
	log_avail_resources(system_resources);
	log_msg("\n");
}

/**
 * @brief Enqueue process <code>pcb</code> to <code>queue</code>
 * Log the enqueue operation appropiately, depending on <code>status</code>
   log_request_ready(pcb->process->name);
   log_request_waiting(pcb->process->name,
 pcb->next_instruction->resource_name); log_terminated(pcb->process->name);
 */
void enqueue_pcb(pcb_t* pcb, pcb_queue_t* queue, int status)
{
	if (queue == NULL || pcb == NULL) {
		return;
	}

	if (queue->last == NULL) {
		queue->first = pcb;
		queue->last  = pcb;
	} else {
		queue->last->next = pcb;
		queue->last       = pcb;
	}

	pcb->next  = NULL;
	pcb->state = status;

	// Log the enqueue operation appropiately, depending on status

	switch (status) {
	case READY:
		log_request_ready(pcb->process->name);
		break;
	case WAITING:
		log_request_waiting(pcb->process->name,
		 pcb->next_instruction->resource_name);
		break;
	case TERMINATED:
		log_terminated(pcb->process->name);
		break;
	default:
		break;
	}
}

/** Dequeue process pcb from queue <code>queue</code>. */
pcb_t* dequeue_pcb(pcb_queue_t* queue)
{
	if (queue && queue->first) {
		// Dequeuing first PCB
		pcb_t* pcb   = queue->first;
		queue->first = queue->first->next;

		// If the queue becomes empty
		if (queue->first == NULL) {
			queue->last = NULL;
		}

		// Reset
		pcb->next = NULL;

		return pcb;
	} else {
		return NULL;
	}
}

/**
 * @brief detect deadlock
 * If deadlock is detected, call
 *  log_deadlock_detected();
 */
struct pcb_t* detect_deadlock(void)
{
	/*  Implement: Future improvement */
	return NULL;
}

/** @brief Deallocate the queues */
void free_manager(void)
{
	log_queue(readyq.first, "Ready");
	log_queue(waitingq.first, "Waiting");
	log_queue(terminatedq.first, "Terminated");

	// open MP code
#ifdef OPENMP
	omp_set_lock(&ready_lock);
	omp_set_lock(&waiting_lock);
	omp_set_lock(&terminated_lock);
#endif

#ifdef DEBUG_MNGR
	printf("\nFreeing the queues...\n");
#endif
	dealloc_pcbs(readyq.first);
	dealloc_pcbs(waitingq.first);
	dealloc_pcbs(terminatedq.first);
	// Unsetting OPENMP locks
#ifdef OPENMP
	omp_unset_lock(&terminated_lock);
	omp_unset_lock(&waiting_lock);
	omp_unset_lock(&ready_lock);
#endif
}

/** @brief Retrieve the number of threads to create from the list of
 * arguments
 */
int get_num_threads(int num_args, char** argv)
{
	if (num_args > 1)
		return atoi(argv[1]);
	else
		return 1;
}

/** @brief Retrieve the name of a process file or the codename "generate"
 * from the list of arguments */
char* get_data(int num_args, char** argv)
{
	char* data_origin = "generate";
	if (num_args > 2)
		return argv[2];
	else
		return data_origin;
}

/** @brief Retrieve the scheduler algorithm type from the list of arguments
 */
int get_algo(int num_args, char** argv)
{
	if (num_args > 3)
		return atoi(argv[3]);
	else
		return 1;
}

/** @brief Retrieve the time quantum from the list of arguments */
int get_time_quantum(int num_args, char** argv)
{
	if (num_args > 4)
		return atoi(argv[4]);
	else
		return 1;
}

/** @brief Print the arguments of the program */
void print_args(int num_thr, char* data, int sched, int tq)
{
	printf("Arguments: num_threads = %d, data = %s, scheduler = %s, time "
	       "quantum = %d\n",
	 num_thr, data,
	 (sched == 0) ?
	  "priority" :
	  (sched == 1) ?
	  "RR" :
	  "FCFS",
	 tq);
}
