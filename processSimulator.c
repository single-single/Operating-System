#include "coursework.c"
#include "linkedlist.c"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>

void * processGenerator(void);
void * processSchedulerLong(void);
void * processSchedulerShort(void);
void * processRunner(int *);
void * processBooster(void);
void * processTerminator(void);

void printHeadersSVG(void);
void printProcessSVG(int, struct process *, struct timeval, struct timeval);
void printPrioritiesSVG(void);
void printRasterSVG(void);
void printFootersSVG(void);

int processCounter = 0;
long int totalResponse = 0;
long int totalTurnaround = 0;

// Initialize the progress table
struct process * processTable[SIZE_OF_PROCESS_TABLE];

// Initialize free, new, ready, terminated and readyInOrder(for "CPUs" use) queues
struct element * freeHead = NULL;
struct element * freeTail = NULL;
struct element * newHead = NULL;
struct element * newTail = NULL;
struct element * readyHead = NULL;
struct element * readyTail = NULL;
struct element * terminatedHead = NULL;
struct element * terminatedTail = NULL;
struct element * readyInOrder[MAX_PRIORITY][2];

sem_t generateProduce;
sem_t longProduce;
sem_t shortProduce;
sem_t lowerShortProduce;
sem_t runProduce;
sem_t terminateProduce;
sem_t newMutex;
sem_t readyMutex;
sem_t orderMutex;
sem_t terminatedMutex;
sem_t freeMutex;
sem_t processTableMutex;

struct timeval oBaseTime;

int main()
{	
	int i = 0;
	int freeArr[SIZE_OF_PROCESS_TABLE];

	for (i=0; i<SIZE_OF_PROCESS_TABLE; i++) {
		freeArr[i] = i;
	}

	// Store PIDs in the free queue
	for (i=0; i<SIZE_OF_PROCESS_TABLE; i++) {
		addLast(&freeArr[i], &freeHead, &freeTail);
	}

	// Initialize the readyInOrder linked list so that nodes can be added
	for (i=0; i<MAX_PRIORITY; i++) {
		readyInOrder[i][0] = NULL;
		readyInOrder[i][1] = NULL;
	}

    sem_init(&generateProduce, 0, 0);
    sem_init(&longProduce, 0, 0);
    sem_init(&shortProduce, 0, 0);
    sem_init(&lowerShortProduce, 0, 0);
    sem_init(&runProduce, 0, 0);
    sem_init(&terminateProduce, 0, 0);
    sem_init(&newMutex, 0, 1);
    sem_init(&readyMutex, 0, 1);
    sem_init(&orderMutex, 0, 1);
    sem_init(&terminatedMutex, 0, 1);
    sem_init(&freeMutex, 0, 1);
    sem_init(&processTableMutex, 0, 1);
    
	pthread_t generate;
	pthread_t schedule_long;
	pthread_t schedule_short;
	pthread_t run[NUMBER_OF_CPUS];
	pthread_t boost;
	pthread_t terminate;
	
	gettimeofday(&oBaseTime, NULL);

	printHeadersSVG();
	printPrioritiesSVG();
	printRasterSVG();

	pthread_create(&generate, NULL, (void *) processGenerator, NULL);
	pthread_create(&schedule_long, NULL, (void *) processSchedulerLong, NULL);
	pthread_create(&schedule_short, NULL, (void *) processSchedulerShort, NULL);
	// Each CPU has a separate thread
	int cpus[NUMBER_OF_CPUS];
	for (i=0; i<NUMBER_OF_CPUS; i++) {
		cpus[i] = i;
		pthread_create(&run[i], NULL, (void *) processRunner, &cpus[i]);
	}
	pthread_create(&boost, NULL, (void *) processBooster, NULL);
	pthread_create(&terminate, NULL, (void *) processTerminator, NULL);
	
	pthread_join(generate, NULL);
	pthread_join(schedule_long, NULL);
	pthread_join(schedule_short, NULL);
	for (i=0; i<NUMBER_OF_CPUS; i++) {
		pthread_join(run[i], NULL);
	}
	pthread_join(boost, NULL);
	pthread_join(terminate, NULL);

	sem_destroy(&generateProduce);
    sem_destroy(&longProduce);
    sem_destroy(&shortProduce);
    sem_destroy(&lowerShortProduce);
    sem_destroy(&runProduce);
    sem_destroy(&terminateProduce);
    sem_destroy(&newMutex);
    sem_destroy(&readyMutex);
    sem_destroy(&orderMutex);
    sem_destroy(&terminatedMutex);
    sem_destroy(&freeMutex);
    sem_destroy(&processTableMutex);

	printFootersSVG();

	return 0;
}

/*
 * This thread adds processes to the system as soon as there is capacity.
 * It obtains unused PIDs from the free queue, uses these PIDs to create processes and adds them to the new queue.
 */

void * processGenerator()
{
    int generateCounter = 0;
	while(1) {
		if (generateCounter == NUMBER_OF_PROCESSES) {
			break;
		}
		if (freeHead != NULL) {
            sem_wait(&freeMutex);
			int * pid = removeFirst(&freeHead, &freeTail);
            sem_post(&freeMutex);
			struct process * prcs = generateProcess(pid);
			printf("TXT: Generated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
			* prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
			// Add the new process to the process table
            sem_wait(&processTableMutex);
			processTable[*prcs->pPID] = prcs;
            sem_post(&processTableMutex);
            sem_wait(&newMutex);
			addLast(prcs, &newHead, &newTail);
            sem_post(&newMutex);
            generateCounter++;
            sem_post(&generateProduce);
		} else {
			sem_wait(&terminateProduce);
		}
	}
	return ((void *) 0);
}

/*
 * This thread admits processes to the system.
 * It removes the processes from the new queue and adds them to the ready queue.
 */

void * processSchedulerLong()
{
    sem_wait(&generateProduce);
    int longCounter = 0;
	while(1) {
		usleep(LONG_TERM_SCHEDULER_INTERVAL);
		if (longCounter == NUMBER_OF_PROCESSES) {
			break;
		}
		if (newHead != NULL) {
            sem_wait(&newMutex);
			struct process * prcs = removeFirst(&newHead, &newTail);
            sem_post(&newMutex);
			printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
			* prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
            sem_wait(&readyMutex);
			addLast(prcs, &readyHead, &readyTail);
            sem_post(&readyMutex);
            longCounter++;
            sem_post(&longProduce);
		} else {
			sem_wait(&generateProduce);
		}
	}
	return ((void *) 0);
}

/*
 * This thread admits processes to the CPU.
 * It removes the processes from the new queue and adds them to a priority queue for CPU use.
 */

void * processSchedulerShort()
{
    sem_wait(&longProduce);
    int shortCounter = 0;
	while(1) {
		if (shortCounter == NUMBER_OF_PROCESSES) {
			break;
		}
		if (readyHead != NULL) {
            sem_wait(&readyMutex);
			struct process * prcs = removeFirst(&readyHead, &readyTail);
            sem_post(&readyMutex);
            sem_wait(&orderMutex);
			addLast(prcs, &readyInOrder[prcs->iPriority][0], &readyInOrder[prcs->iPriority][1]);
            sem_post(&orderMutex);
            shortCounter++;
            sem_post(&shortProduce);
            if (prcs->iPriority >= (MAX_PRIORITY/2)) {
                sem_post(&lowerShortProduce);
            }
		} else {
			sem_wait(&longProduce);
		}
	}
	return ((void *) 0);
}

/*
 * This thread runs processes on the CPU.
 * It runs the processes according to the priority level and moves them to the terminated queue when the process ends.
 * It is able to simulate multiple “CPUs”.
 */

void * processRunner(int * cpuAddress)
{
    sem_wait(&shortProduce);
	struct timeval oStartTime;
	struct timeval oEndTime;
	int i = 0;
	int j = 0;
	int cpu = * cpuAddress;
	int preemptPriority = -1;
	while(1) {
		if (processCounter == NUMBER_OF_PROCESSES) {
			break;
		}
		while (i < MAX_PRIORITY && readyInOrder[i][0] != NULL) {
			if (i < (MAX_PRIORITY/2)) {
				// If a higher priority process is detected, preempt the current process and execute the new process
				for (j=0; j<i; j++) {
					if (readyInOrder[j][0] != NULL) {
						preemptJob(readyInOrder[i][0]->pData);
						preemptPriority = j;
						break;
					}
				}
                sem_wait(&orderMutex);
				struct process * prcs = removeFirst(&readyInOrder[i][0], &readyInOrder[i][1]);
                sem_post(&orderMutex);
				if (prcs == NULL) {
					break;
				} else {
					runNonPreemptiveJob(prcs, &oStartTime, &oEndTime);
				}
                int response = getDifferenceInMilliSeconds(prcs->oTimeCreated, prcs->oFirstTimeRunning);
                int turnaround = getDifferenceInMilliSeconds(prcs->oTimeCreated, prcs->oLastTimeRunning);
				// If the running time is too short to be perceptible, ignore this run
				if (prcs->iPreviousBurstTime == prcs->iInitialBurstTime && prcs->iPreviousBurstTime == prcs->iRemainingBurstTime) {
                    sem_wait(&orderMutex);
					addFirst(prcs, &readyInOrder[i][0], &readyInOrder[i][1]);
                    sem_post(&orderMutex);
                } else {
                    if (prcs->iPreviousBurstTime == prcs->iInitialBurstTime) {
                        if (prcs->iRemainingBurstTime == 0) {
                            totalResponse = totalResponse + response;
                            totalTurnaround = totalTurnaround + turnaround;
                            printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d, Turnaround Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, response, turnaround);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&terminatedMutex);
                            addLast(prcs, &terminatedHead, &terminatedTail);
                            sem_post(&terminatedMutex);
                            sem_post(&runProduce);
                        } else {
                            totalResponse = totalResponse + response;
                            printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, response);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&orderMutex);
                            addFirst(prcs, &readyInOrder[i][0], &readyInOrder[i][1]);
                            sem_post(&orderMutex);
                        }
                    } else {
                        if (prcs->iRemainingBurstTime == 0) {
                            totalTurnaround = totalTurnaround + turnaround;
                            printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, turnaround);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&terminatedMutex);
                            addLast(prcs, &terminatedHead, &terminatedTail);
                            sem_post(&terminatedMutex);
                            sem_post(&runProduce);
                        } else {
                            printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&orderMutex);
                            addFirst(prcs, &readyInOrder[i][0], &readyInOrder[i][1]);
                            sem_post(&orderMutex);
                        }
                    }
                }
			} else {
				// If a higher priority process is detected, the new process will be executed after the current time slice is run
				for (j=0; j<i; j++) {
					if (readyInOrder[j][0] != NULL) {
						preemptPriority = j;
						break;
					}
				}
                sem_wait(&orderMutex);
				struct process * prcs = removeFirst(&readyInOrder[i][0], &readyInOrder[i][1]);
                sem_post(&orderMutex);
				if (prcs == NULL) {
					break;
				} else {
					runPreemptiveJob(prcs, &oStartTime, &oEndTime);
				}
                int response = getDifferenceInMilliSeconds(prcs->oTimeCreated, prcs->oFirstTimeRunning);
                int turnaround = getDifferenceInMilliSeconds(prcs->oTimeCreated, prcs->oLastTimeRunning);
				// If the running time is too short to be perceptible, ignore this run
				if (prcs->iPreviousBurstTime == prcs->iInitialBurstTime && prcs->iPreviousBurstTime == prcs->iRemainingBurstTime) {
                    sem_wait(&orderMutex);
					addFirst(prcs, &readyInOrder[i][0], &readyInOrder[i][1]);
                    sem_post(&orderMutex);
                } else {
                    if (prcs->iPreviousBurstTime == prcs->iInitialBurstTime) {
                        if (prcs->iRemainingBurstTime == 0) {
                            totalResponse = totalResponse + response;
                            totalTurnaround = totalTurnaround + turnaround;
                            printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d, Turnaround Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, response, turnaround);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&terminatedMutex);
                            addLast(prcs, &terminatedHead, &terminatedTail);
                            sem_post(&terminatedMutex);
                            sem_post(&runProduce);
                        } else {
                            totalResponse = totalResponse + response;
                            printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, response);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&orderMutex);
                            addLast(prcs, &readyInOrder[prcs->iPriority][0], &readyInOrder[prcs->iPriority][1]);
                            sem_post(&orderMutex);
                        }
                    } else {
                        if (prcs->iRemainingBurstTime == 0) {
                            totalTurnaround = totalTurnaround + turnaround;
                            printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime, turnaround);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&terminatedMutex);
                            addLast(prcs, &terminatedHead, &terminatedTail);
                            sem_post(&terminatedMutex);
                            sem_post(&runProduce);
                        } else {
                            printf("TXT: Consumer %d, Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
                            cpu+1, * prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
							printProcessSVG(cpu+1, prcs, oStartTime, oEndTime);
                            sem_wait(&orderMutex);
                            addLast(prcs, &readyInOrder[prcs->iPriority][0], &readyInOrder[prcs->iPriority][1]);
                            sem_post(&orderMutex);
                        }
                    }
                }
			}
			// If a higher priority process is detected, execute that process
			if (preemptPriority != -1) {
				i = preemptPriority;
				preemptPriority = -1;
			}
		}
		if (readyInOrder[i][0] == NULL && i<(MAX_PRIORITY-1)) {
			i++;
		}
	}
	return ((void *) 0);
}

/*
 * This thread boosts jobs with a low priority at regular intervals to the highest RR level.
 * It removes jobs from lower priority queues and adding them to the end of the highest round robin priority queue.
 */

void * processBooster()
{
    sem_wait(&lowerShortProduce);
	while (1) {
		usleep(BOOST_INTERVAL);
		if (processCounter == NUMBER_OF_PROCESSES) {
			break;
		}
		int i = 0;
		for (i=(MAX_PRIORITY/2); i<MAX_PRIORITY; i++) {
			if (readyInOrder[i][0] != NULL && readyInOrder[i][1] != NULL) {
                sem_wait(&orderMutex);
				struct process * prcs = removeFirst(&readyInOrder[i][0], &readyInOrder[i][1]);
                sem_post(&orderMutex);
				printf("TXT: Boost: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
				* prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
                sem_wait(&orderMutex);
				addLast(prcs, &readyInOrder[(MAX_PRIORITY/2)][0], &readyInOrder[(MAX_PRIORITY/2)][1]);
                sem_post(&orderMutex);
			}
		}
	}
	return ((void *) 0);
}

/*
 * This thread posts processing process control blocks and releasing PIDs when no longer required.
 * It removes the terminated processes from the terminated queue and re-adds these released PIDs to the free queue.
 * This thread calculates the final average response and turnaround times as well.
 */

void * processTerminator()
{
    sem_wait(&runProduce);
	double averageResponse;
	double averageTurnaround;
	while (1) {
		usleep(TERMINATION_INTERVAL);
		if (processCounter == NUMBER_OF_PROCESSES) {
			averageResponse = totalResponse / (float) NUMBER_OF_PROCESSES;
			averageTurnaround = totalTurnaround / (float) NUMBER_OF_PROCESSES;
			printf("TXT: Average response time = %.6lf, Average turnaround time = %.6lf\n", averageResponse, averageTurnaround);
			break;
		}
		if (terminatedHead != NULL) {
            sem_wait(&terminatedMutex);
			struct process * prcs = removeFirst(&terminatedHead, &terminatedTail);
            sem_post(&terminatedMutex);
			printf("TXT: Terminated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n",
			* prcs->pPID, prcs->iPriority, prcs->iPreviousBurstTime, prcs->iRemainingBurstTime);
			// Remove the terminated process from the process table
            sem_wait(&processTableMutex);
			processTable[*prcs->pPID] = NULL;
            sem_post(&processTableMutex);
            sem_wait(&freeMutex);
			addLast(prcs->pPID, &freeHead, &freeTail);
			free(prcs);
            sem_post(&freeMutex);
            processCounter++;
            sem_post(&terminateProduce);
		} else {
			sem_wait(&runProduce);
		}
	}
	int i = 0;
	for (i=0; i<NUMBER_OF_PROCESSES; i++) {
		removeFirst(&freeHead, &freeTail);
	}
	return ((void *) 0);
}

/*********************************************** SVG Functions ***********************************************/

void printHeadersSVG()
{
	printf("SVG: <!DOCTYPE html>\n");
	printf("SVG: <html>\n");
	printf("SVG: <body>\n");
	printf("SVG: <svg width=\"%d\" height=\"%d\">\n", ((NUMBER_OF_CPUS*3+25)*NUMBER_OF_PROCESSES)/NUMBER_OF_CPUS, MAX_PRIORITY*NUMBER_OF_CPUS*18);
}

void printProcessSVG(int iCPUId, struct process * pProcess, struct timeval oStartTime, struct timeval oEndTime)
{
	int iXOffset = getDifferenceInMilliSeconds(oBaseTime, oStartTime) + 30;
	int iYOffsetPriority = (pProcess->iPriority + 1) * 16 - 12;
	int iYOffsetCPU = (iCPUId - 1) * (MAX_PRIORITY * 18 + 50);
	int iWidth = getDifferenceInMilliSeconds(oStartTime, oEndTime);
	printf("SVG: <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"8\" style=\"fill:rgb(%d,0,%d);stroke-width:1;stroke:rgb(255,255,255)\"/>\n", iXOffset /* x */, iYOffsetCPU + iYOffsetPriority /* y */, iWidth, *(pProcess->pPID) - 1 /* rgb */, *(pProcess->pPID) - 1 /* rgb */);
}

void printPrioritiesSVG()
{
	for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
	{
		for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 4;
			int iYOffsetCPU = (iCPU - 1) * (MAX_PRIORITY * 18 + 50);
			printf("SVG: <text x=\"0\" y=\"%d\" fill=\"black\">%d</text>", iYOffsetCPU + iYOffsetPriority, iPriority);
		}
	}
}

void printRasterSVG()
{
	for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
	{
		for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
		{
			int iYOffsetPriority = (iPriority + 1) * 16 - 8;
			int iYOffsetCPU = (iCPU - 1) * (MAX_PRIORITY * 18 + 50);
			printf("SVG: <line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:rgb(125,125,125);stroke-width:1\" />", 16, iYOffsetCPU + iYOffsetPriority, ((NUMBER_OF_CPUS*3+25)*NUMBER_OF_PROCESSES)/NUMBER_OF_CPUS, iYOffsetCPU + iYOffsetPriority);
		}
	}
}

void printFootersSVG()
{
	printf("SVG: Sorry, your browser does not support inline SVG.\n");
	printf("SVG: </svg>\n");
	printf("SVG: </body>\n");
	printf("SVG: </html>\n");
}