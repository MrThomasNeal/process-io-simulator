#include "coursework.h"
#include "linkedlist.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>
#include <string.h>

// SEMAPHORES
sem_t readyQueueSem;
sem_t queueFillSem;
sem_t queueNotEmptySem;
sem_t terminationQueueSem;
sem_t mutex;
sem_t cpuSemaphore;
sem_t pidSemaphore;
sem_t genStart;
sem_t simStart;
sem_t termStart;
sem_t diskControllerSem;
sem_t diskStart;
sem_t readyIOsem;
sem_t hardDriveSem;
sem_t terminatorWakeup;

// LINKED LISTS
LinkedList readyQueue = LINKED_LIST_INITIALIZER;
LinkedList terminationQueue = LINKED_LIST_INITIALIZER;
LinkedList hardDriveQueue = LINKED_LIST_INITIALIZER;
LinkedList readyIOqueue = LINKED_LIST_INITIALIZER;

// TO KEEP TRACK OF HOW FULL THE QUEUES ARE
int readyQueueCounter = 1;
int terminationQueueCounter = 1;
int simulatorQueueCounter = 1;
int generatorActive = 1;
int simsFinished = 0;

// FUNCTION DEFINITIONS
void* processGenerator();
void* processSimulator(void *);
void* processTerminator();
void* diskController();
int queueLength(LinkedList);
void outputArray();
void removeFirstPID();
void outputArray();
int getFirstElement();
void addToPool(int);
void printAddQueue(Process*, int);
void printRemoveQueue(Process*, int);
void simulateIOfunction(Process*);

// PID POOL
int PID[SIZE_OF_PROCESS_TABLE];
int PIDArrayLength = 0;

// MAIN FUNCTION
int main() {

    pthread_t processGeneratorThread;
    pthread_t processSimulatorThread[NUMBER_OF_CPUS];
    pthread_t processTerminatorThread;
    pthread_t processDiskThread;

    int processGenThread;
    int processSimThread;
    int processTermThread;
    int i, x;

    sem_init(&readyQueueSem, 0, 1); 
    sem_init(&queueFillSem, 0, MAX_CONCURRENT_PROCESSES);
    sem_init(&terminationQueueSem, 0, 1);
    sem_init(&queueNotEmptySem, 0, 0);
    sem_init(&mutex, 0, 1);
    sem_init(&pidSemaphore, 0, 1);
    sem_init(&cpuSemaphore, 0, 1);
    sem_init(&genStart, 0, 0);
    sem_init(&simStart, 0, 0);
    sem_init(&termStart, 0, 0);
    sem_init(&diskStart, 0, 0);
    sem_init(&diskControllerSem, 0, 0);
    sem_init(&readyIOsem, 0, 1);
    sem_init(&hardDriveSem, 0, 1);
    sem_init(&terminatorWakeup, 0, 0);

    for(x = 0; x<SIZE_OF_PROCESS_TABLE; x++) {
        PID[x] = x;
        PIDArrayLength++;
    }

    processGenThread = pthread_create(&processGeneratorThread, NULL, processGenerator, NULL);
    if(processGenThread == 0) {
        printf("[Generator] Thread created successfully\n");
    } else {
        printf("[Generator] Thread creation failed\n");
    }

    for(i = 0; i < NUMBER_OF_CPUS; i++) {
        processSimThread = pthread_create(&processSimulatorThread[i], NULL, processSimulator, (void *)i);
        if(processSimThread == 0) {
            printf("[Simulator %d] Thread created successfully\n", i);
        } else {
            printf("[Simulator %d] Thread creation failed\n", i);
        }
    }

    processTermThread = pthread_create(&processTerminatorThread, NULL, processTerminator, NULL);
    if(processTermThread == 0) {
        printf("[Terminator] Thread created successfully\n");
    } else {
        printf("[Terminator] Thread creation failed\n");
    }

    processDiskThread = pthread_create(&processDiskThread, NULL, diskController, NULL);
    if(processDiskThread == 0) {
        printf("[Disk Controller] Thread created successfully\n");
    } else {
        printf("[Disk Controller] Thread created successfully\n");
    }

    sem_post(&genStart);
    for(i = 0; i < NUMBER_OF_CPUS; i++) {
        sem_post(&simStart);
    }
    sem_post(&termStart);
    sem_post(&diskStart);

    pthread_exit(NULL);
}
//

// PROCESS GENERATOR FUNCTION
void * processGenerator() {
    sem_wait(&genStart);

    Process *p;
    int processesGenerated = 0;

    printf("STARTING GENERATOR\n");

    while(true) {
        sem_wait(&queueFillSem); // -1 - WILL RUN UNTIL MAX_CONCURRENT_PROCESSES TIMES, WILL WAIT FOR QUEUESPACE TO BE WOKEN UP
        sem_wait(&readyQueueSem);
        if(queueLength(readyQueue) <= 32) {
            sem_post(&readyQueueSem);
            if(processesGenerated >= NUMBER_OF_PROCESSES) {
                printf("PROCESS GENERATOR FINISHED\n");
                generatorActive = 0;
                pthread_exit(NULL);
                return 0;
            } else {
                sem_wait(&pidSemaphore);
                // GET FIRST PID IN THE POOL, GENERATE PROCESS, REMOVE IT FROM THE POOL
                int newPID = getFirstElement();
                p = generateProcess(newPID);
                removeFirstPID();
                sem_post(&pidSemaphore);

                processesGenerated++;
                sem_wait(&readyQueueSem);
                addLast(p, &readyQueue);
                printf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d]\n", queueLength(readyQueue), newPID);
                sem_post(&readyQueueSem);
                printf("GENERATOR - ADMITTED: [PID = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n", p->iPID, p->iBurstTime, p->iRemainingBurstTime);
                sem_post(&queueNotEmptySem);
            }
        } else {
            sem_post(&readyQueueSem);
        }
    }
}
//

// PROCESS SIMULATOR FUNCTION
void* processSimulator(void *cpuNumber) {
    sem_wait(&simStart);

    long int responseTime, turnAroundTime;
    int queueFillSemValueBefore, queueFillSemValueAfter;

    int cpuID = (int*)cpuNumber; 
    int processIO, termCount = 0;

    long int averageResponseTime = 0, averageTurnaroundTime = 0;

    Process *pIO, *p;
    Element *head, *IOhead;

    bool run = true;

    printf("STARTING SIMULATOR %d\n", cpuID);

    while(true) {
        sem_wait(&cpuSemaphore);
        //printf("6\n");
        if(generatorActive == 0 && queueLength(readyQueue) == 0 && queueLength(readyIOqueue) == 0 && queueLength(hardDriveQueue) == 0) {
            printf("PROCESS SIMULATOR %d FINISHED: [AverageResponseTime = %ld, AverageTurnaroundTime = %ld]\n", cpuID, (averageResponseTime/termCount), (averageTurnaroundTime/termCount));
            sem_post(&cpuSemaphore);
            simsFinished++;
            if(simsFinished == NUMBER_OF_CPUS) {
                sem_post(&diskControllerSem);
            }
            pthread_exit(NULL);
        }
        //printf("4\n");
        sem_wait(&readyIOsem);
        sem_wait(&readyQueueSem);

        IOhead = getHead(readyIOqueue);
        head = getHead(readyQueue);

        if(queueLength(readyIOqueue) != 0) {
            processIO = 1;
        } else if (queueLength(readyQueue) != 0) {
            processIO = 0;
        }

        //printf("5\n");
        sem_post(&readyIOsem);
        sem_post(&readyQueueSem);
        //printf("1\n");
        if(head != NULL || IOhead != NULL) {
            //printf("7\n");
            sem_wait(&readyQueueSem);
            sem_wait(&readyIOsem);
            if(processIO == 1) {
                p = (Process *)IOhead->pData;
                IOhead = getNext(IOhead);
            } else if (processIO == 0) {
                p = (Process *)head->pData;
                head = getNext(head);
            }
            runPreemptiveProcess(p, true, false);
            printf("SIMULATOR - CPU %d: [PID = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n", cpuID, p->iPID, p->iBurstTime, p->iRemainingBurstTime);
            if(p->iState == TERMINATED) {
                responseTime = getDifferenceInMilliSeconds(p->oTimeCreated, p->oLastTimeRunning);
                turnAroundTime = getDifferenceInMilliSeconds(p->oFirstTimeRunning, p->oLastTimeRunning);

                averageResponseTime = averageResponseTime + responseTime;
                averageTurnaroundTime = averageTurnaroundTime + turnAroundTime;
                termCount++;

                if(processIO == 0) {
                    removeFirst(&readyQueue); // REMOVE FROM READY QUEUE
                    printRemoveQueue(p, 1);
                } else {
                    removeFirst(&readyIOqueue);
                    printRemoveQueue(p, 4);
                }
                printf("SIMULATOR - TERMINATED: [PID = %d, ResponseTime = %ld, TurnAroundTime = %ld]\n", p->iPID, responseTime, turnAroundTime);
                sem_wait(&terminationQueueSem);
                addLast(p, &terminationQueue); // ADD TO TERMINATION 
                printAddQueue(p, 2);
                sem_post(&terminationQueueSem);
                sem_post(&terminatorWakeup); // WAKES UP TERM DAEMON TO CLEAR ENTRY
            } else if(p->iState == BLOCKED) {
                printf("SIMULATOR - I/O BLOCKED: [PID = %d, Device = %d, Type = %d]\n", p->iPID, p->iRW, p->iDeviceType);

                if(processIO == 0) {
                    removeFirst(&readyQueue); // REMOVE FROM READY QUEUE
                    printRemoveQueue(p, 1);
                } else {
                    removeFirst(&readyIOqueue); // REMOVE FROM IO QUEUE
                    printRemoveQueue(p, 4);
                }
                addLast(p, &hardDriveQueue);
                printAddQueue(p, 3);

                sem_post(&diskControllerSem);
            } else {
                if(processIO == 0) {
                    removeFirst(&readyQueue); // REMOVE FROM READY QUEUE
                    printRemoveQueue(p, 1);
                    addLast(p, &readyQueue); // ADD TO END OF READY QUEUE
                    printAddQueue(p, 1);
                } else {
                    removeFirst(&readyIOqueue);
                    printRemoveQueue(p, 4);
                    addLast(p, &readyQueue);
                    printAddQueue(p, 1);
                }
            }
            sem_post(&readyQueueSem);
            sem_post(&readyIOsem);
        } 
        sem_post(&cpuSemaphore);
    }

}
//

// PROCESS TERMINATOR FUNCTION
void* processTerminator() {
    sem_wait(&termStart);

    int processesCleared = 0;
    Element *head;
    Process *p;

    while(true) {
        if(processesCleared == NUMBER_OF_PROCESSES) {
            printf("PROCESS TERMINATOR FINISHED\n");
            pthread_exit(NULL);
            return 0;
        }
        sem_wait(&terminatorWakeup);
        sem_wait(&terminationQueueSem);
        head = getHead(terminationQueue);
        if(head != NULL) {
            p = (Process *)head->pData;
            removeFirst(&terminationQueue);
            printRemoveQueue(p, 2);
            processesCleared++;
            printf("TERMINATION DAEMON - CLEARED: [#iTerminated = %d, PID = %d]\n", processesCleared, p->iPID);
            // GIVE PID BACK TO POOL
            sem_wait(&pidSemaphore);
            addToPool(p->iPID);
            sem_post(&pidSemaphore);
            sem_post(&queueFillSem);
        } else {
            break;
        }
        head = getNext(head);
        sem_post(&terminationQueueSem);
    }

}

// DISK CONTROLLER FUNCTION
void* diskController() {

    int track;
    int prevTrack = -1;

    Element *head, *tempHead;
    Process *p;


    bool runState;

    sem_wait(&diskStart);

    while(true) {
        sem_wait(&diskControllerSem); //
        if(simsFinished == NUMBER_OF_CPUS) {
            printf("DISK CONTROLLER FINISHED\n");
            pthread_exit(NULL);
        }
        sem_wait(&hardDriveSem);
        sem_wait(&readyIOsem);
        head = getHead(hardDriveQueue);
        tempHead = getHead(hardDriveQueue);
        p = (Process *)head->pData;
        track = p->iTrack;

        runState = true;
        if(queueLength(hardDriveQueue) == 1) {
            runState = false;
        }

        simulateIOfunction(p);

        // CODE BELOW IMPLEMENTS LOOK-SCAN ALGORITHM
        if(track > prevTrack) {

            for(int i = prevTrack + 1; i <= track; i++) {
                while(runState = true) {
                    tempHead = getNext(tempHead);
                    if(tempHead == NULL) {
                        runState = false;
                        break;
                    }
                    p = (Process *)tempHead->pData;
                    if(p->iTrack == i) {
                        simulateIOfunction(p);
                    }
                    tempHead = getNext(tempHead);
                }
            }
            printf("Track moved from %d to %d\n", prevTrack, track);

        } else {

            for(int i = prevTrack -1; i >= track; i--) {
                while(runState = true) {
                    tempHead = getNext(tempHead);
                    if(tempHead == NULL) {
                        runState = false;
                        break;
                    }
                    //printf("--\n");
                    p = (Process *)tempHead->pData;
                    if(p->iTrack == i) {
                        simulateIOfunction(p);
                    }
                    tempHead = getNext(tempHead);
                }
            }
            printf("Track moved from %d to %d\n", prevTrack, track);
        }
        prevTrack = track;
        //
    
        //printf("2\n");
        if(queueLength(hardDriveQueue) != 0) {
            head = getNext(head);
        }
        //printf("3\n");
        sem_post(&hardDriveSem);
        sem_post(&readyIOsem);
    }

}

// FUNCTION TO SIMULATE BLOCKED IO PROCESSES
void simulateIOfunction(Process *p) {

    simulateIO(p);
    if(p->iRW == READ) {
        printf("HARD DRIVE: reading track %d\n", p->iTrack);
    } else if(p->iRW == WRITE) {
            printf("WRITER - Write request received\n");
            printf("WRITER - writing\n");
    }
    if(p->iState == READY) {
        removeFirst(&hardDriveQueue);
        printRemoveQueue(p, 3);
        addLast(p, &readyIOqueue);
        printAddQueue(p, 4);
    }
}

// FUNCTION FOR OUTPUTTING QUEUE ADDS
void printAddQueue(Process *p, int queueChoice) {
    if(queueChoice == 1) {
        printf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d]\n", queueLength(readyQueue), p->iPID);
    } else if(queueChoice == 2) {
        printf("QUEUE - ADDED: [Queue = TERMINATED, Size = %d, PID = %d]\n", queueLength(terminationQueue), p->iPID);
    } else if(queueChoice == 3) {
        printf("QUEUE - ADDED: [Queue = HARD DRIVE, Size = %d, PID = %d]\n", queueLength(hardDriveQueue), p->iPID);
    } else if(queueChoice == 4) {
        printf("QUEUE - ADDED: [Queue = READY_IO, Size = %d, PID = %d]\n", queueLength(readyIOqueue), p->iPID);
    }
}

// FUNCTION FOR OUTPUTTING QUEUE REMOVALS
void printRemoveQueue(Process *p, int queueChoice) {
    if(queueChoice == 1) {
        printf("QUEUE - REMOVED: [Queue = READY, Size = %d, PID = %d]\n", queueLength(readyQueue), p->iPID);
    } else if(queueChoice == 2) {
        printf("QUEUE - REMOVED: [Queue = TERMINATED, Size = %d, PID = %d]\n", queueLength(terminationQueue), p->iPID);
    } else if(queueChoice == 3) {
        printf("QUEUE - REMOVED: [Queue = HARD DRIVE, Size = %d, PID = %d]\n", queueLength(hardDriveQueue), p->iPID);
    } else if(queueChoice == 4) {
        printf("QUEUE - REMOVED: [Queue = READY_IO, Size = %d, PID = %d]\n", queueLength(readyIOqueue), p->iPID);
    }
}

// FUNCTION TO OUTPUT THE ARRAY CONTENTS, USED FOR TESTING
void outputArray() {

    int x;

    for(x = 0; x<PIDArrayLength; x++) {
        printf("%d, ", PID[x]);
    }
    printf("\n");
}

// FUNCTION TO REMOVE FIRST ELEMENT FROM LIST
void removeFirstPID() {

    int x;

    for(x = 0; x<PIDArrayLength-1; x++) {
        PID[x] = PID[x+1];
    }

    PIDArrayLength--;
}

// FUNCTION TO ADD A PID BACK INTO THE POOL
void addToPool(int pid) {

    PID[PIDArrayLength] = pid;
    PIDArrayLength++;

}

// FUNCTION TO RETURN FIRST ELEMENT IN THE LIST
int getFirstElement() {
    int firstElement = PID[0];
    return firstElement;
}


// FUNCTION TO RETURN LENGTH OF A PROVIDED LINKED LIST
int queueLength(LinkedList queue) {
    int i = 0;
    Element * head = getHead(queue);
    while(head != NULL) {
        i++;
        head = getNext(head);
    }
    return i;
}
