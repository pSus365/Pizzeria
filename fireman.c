#include "pizzeria.h"
#include <stdio.h>
#include <stdlib.h>

static void onTermSignal(int sig) {
    if (sig == SIGTERM) {
        printf(CLR_FIREMAN "[Strażak] Otrzymałem SIGTERM – wychodzę, bo nie jestem już potrzebny.\n" CLR_RESET);
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, CLR_FIREMAN "[Strażak] Użycie: ./fireman_app <pid_kasjera> <pid_menadżera> <liczba_stolików>\n" CLR_RESET);
        exit(1);
    }
    pid_t cashierPid = (pid_t)atoi(argv[1]);
    pid_t managerPid = (pid_t)atoi(argv[2]);
    int   tableCount = atoi(argv[3]);

    srand(time(NULL));
    // Obsługa SIGTERM
    struct sigaction sa;
    sa.sa_handler = onTermSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror(CLR_FIREMAN "[Strażak] Błąd przy sigaction()" CLR_RESET);
        exit(1);
    }

    // Dołączamy do semafora i shm
    key_t kShm = ftok(".", SHM_GEN_CHAR);
    if (kShm == -1) {
        perror(CLR_FIREMAN "[Strażak] ftok() shm" CLR_RESET);
        exit(1);
    }
    key_t kSem = ftok(".", SEMAPHORE_GEN_CHAR);
    if (kSem == -1) {
        perror(CLR_FIREMAN "[Strażak] ftok() sem" CLR_RESET);
        exit(1);
    }

    int semId = accessSemaphore(kSem);
    int shmId = accessSharedMemory(kShm);

    DiningTable* tabPtr = (DiningTable*)shmat(shmId, NULL, 0);
    if (tabPtr == (void*)-1) {
        perror(CLR_FIREMAN "[Strażak] błąd shmat()" CLR_RESET);
        exit(1);
    }

    // Losowy czas do pożaru
    int randomDelay = rand() % 35 + 10;
    sleep(randomDelay);

    printf(CLR_FIREMAN "[Strażak] POŻAR wybucha!\n" CLR_RESET);
    // Informujemy kasjera i klientów
    kill(cashierPid, SIGUSR1);

    semaphoreP(semId, MUTEX_INDEX);
    for (int i = 0; i < tableCount; i++) {
        for (int j = 0; j < 4; j++) {
            if (tabPtr[i].occupant_pids[j] != 0) {
                if (kill(tabPtr[i].occupant_pids[j], SIGUSR1) == -1) {
                    if (errno != ESRCH) {
                        perror(CLR_FIREMAN "[Strażak] Błąd kill() do klienta" CLR_RESET);
                    }
                }
            }
        }
        tabPtr[i].total_seated = 0;
    }
    semaphoreV(semId, MUTEX_INDEX);

    // Informujemy menadżera
    kill(managerPid, SIGUSR1);

    if (shmdt(tabPtr) == -1) {
        perror(CLR_FIREMAN "[Strażak] Błąd shmdt()" CLR_RESET);
    }

    return 0;
}
