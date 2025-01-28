#ifndef PIZZERIA_H
#define PIZZERIA_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// --------------------- Makra kolorów terminala ---------------------
#define CLR_MGR     "\033[1;31m"  // intensywny czerwony
#define CLR_CASHIER "\033[1;32m"  // intensywny zielony
#define CLR_CLIENT  "\033[1;34m"  // intensywny niebieski
#define CLR_FIREMAN "\033[1;35m"  // intensywny magenta
#define CLR_RESET   "\033[0m"     // reset do domyślnych kolorów

// --------------------- Makra i stałe do IPC ---------------------
#define MUTEX_INDEX          0
#define SEMAPHORE_GEN_CHAR  'A'
#define SHM_GEN_CHAR        'B'
#define MSG_GEN_CHAR        'C'

// Typy wiadomości do kolejki
#define REQUEST_TABLE        1
#define SEND_ORDER           2
#define LEAVE_TABLE          3

// Specjalne kody (brak stolika / zamykamy lokal)
#define NO_TABLE_FOUND      -1
#define NEAR_CLOSING        -2

// Rozmiary i czasy (można dostosować do wymagań)
#define TIME_BEFORE_CLOSE    5
#define RUNTIME_LIMIT       60  // pizzeria działa 60 sekund lub dopoki strazak nie oglosi pozaru
//#define RUNTIME_LIMIT       300
#define MAX_CUSTOMERS      400
#define QUEUE_LIMIT         30


// --------------------- Struktury ---------------------

// Opis pojedynczego dania w menu:
typedef struct {
    const char* name;
    double      cost;
} MenuItem;

// Informacje o stoliku:
typedef struct {
    int   capacity;         // liczba krzeseł
    pid_t occupant_pids[4]; // do 4 grup na jednym stoliku
    int   group_size;       // wielkość głównej grupy
    int   total_seated;     // ile osób faktycznie przy nim siedzi
} DiningTable;

// Reprezentuje grupę gości (proces-klienta):
typedef struct {
    int   size;     
    pid_t groupPID; 
} GroupOfClients;

// Komunikat przesyłany przez kolejkę (klient <-> kasjer)
typedef struct {
    long  mtype;
    GroupOfClients group;
    int   tableIndex;
    int   orderedItems[3];
} CommunicationMessage;

// Pomocnicza struktura do zamówień wewnątrz procesu klienta
typedef struct {
    int* selection;
    int  count;
} GroupOrder;

// --------------------- Deklaracja menu i funkcji ---------------------
extern MenuItem pizzaMenu[10];

// Funkcje do semaforów, shm i msg
int  createSemaphore(key_t key);
int  accessSemaphore(key_t key);
void removeSemaphore(int semId);

int  createSharedMemory(key_t key, size_t size);
int  accessSharedMemory(key_t key);
void deleteSharedMemory(int shmId, void* addr);

int  createMessageQueue(key_t key);
int  accessMessageQueue(key_t key);
void deleteMessageQueue(int msgId);

void semaphoreP(int semId, int semNum);
void semaphoreV(int semId, int semNum);

// Wypis informacji o wybranej pizzy
void showChosenPizza(int id);

// --------------------- Definicje kolejki oczekujących ---------------------

typedef struct _QueueNode {
    GroupOfClients       data;
    struct _QueueNode*   next;
} QueueNode;

typedef struct {
    QueueNode* head;
    int        maxSize;
    int        currentSize;
} ClientsQueue;

// Funkcje obsługi kolejki
void initQueue(ClientsQueue* q, int limit);
void enqueueGroup(ClientsQueue* q, const GroupOfClients* g);
GroupOfClients* dequeueSuitable(ClientsQueue* q, int neededSize, int freeSeats);
int  queueSize(const ClientsQueue* q);
void clearQueue(ClientsQueue* q);
void printQueue(const ClientsQueue* q);

#endif // PIZZERIA_H
