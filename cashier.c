#include "pizzeria.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// Zmienne globalne sterowane sygnałami
static volatile sig_atomic_t fireSignal    = 0;
static volatile sig_atomic_t closeIsNear   = 0;
static volatile unsigned long forcedFinish = ULONG_MAX;

// -------------------------------------
static void handleSignals(int sig) {
    if (sig == SIGUSR1) {
        fireSignal = 1;
        printf(CLR_CASHIER "[Kasjer] POŻAR! Sprawdzam, czy klienci opuścili lokal...\n" CLR_RESET);
    } else if (sig == SIGUSR2) {
        closeIsNear   = 1;
        forcedFinish  = (unsigned long)time(NULL) + TIME_BEFORE_CLOSE;
    }
}

// -------------------------------------
static void setupTables(DiningTable* t, int start, int end, int cap) {
    for (int i = start; i < end; i++) {
        for (int j = 0; j < 4; j++) {
            t[i].occupant_pids[j] = 0;
        }
        t[i].capacity     = cap;
        t[i].group_size   = 0;
        t[i].total_seated = 0;
    }
}

// -------------------------------------
static int findFreeTable(DiningTable* arr, int groupSize, int count) {
    if (closeIsNear) {
        return NEAR_CLOSING;
    }
    for (int i = 0; i < count; i++) {
        if ((arr[i].group_size == 0 || arr[i].group_size == groupSize) &&
            (arr[i].capacity - arr[i].total_seated - groupSize >= 0)) {
            return i;
        }
    }
    return NO_TABLE_FOUND;
}

static void seatGroupAtTable(DiningTable* arr, int tableIdx, const GroupOfClients* grp, int queueId) {
    if (arr[tableIdx].total_seated == 0) {
        arr[tableIdx].group_size = grp->size;
    }
    arr[tableIdx].total_seated += grp->size;

    int slot = 0;
    while (slot < 4 && arr[tableIdx].occupant_pids[slot] != 0) {
        slot++;
    }
    arr[tableIdx].occupant_pids[slot] = grp->groupPID;

    CommunicationMessage msg;
    msg.mtype       = grp->groupPID;
    msg.group       = *grp;
    msg.tableIndex  = tableIdx;

    printf(CLR_CASHIER "[Kasjer] Przydzielam stolik %d grupie PID(%d), liczba osób: %d\n" CLR_RESET,
           tableIdx, (int)grp->groupPID, grp->size);

    if (msgsnd(queueId, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror(CLR_CASHIER "[Kasjer] Błąd msgsnd() przy wysyłaniu nr stolika" CLR_RESET);
        exit(1);
    }
}

static void removeGroupFromTable(DiningTable* arr, int idx, pid_t gPID, int size) {
    for (int j = 0; j < 4; j++) {
        if (arr[idx].occupant_pids[j] == gPID) {
            arr[idx].occupant_pids[j] = 0;
            break;
        }
    }
    arr[idx].total_seated -= size;
    if (arr[idx].total_seated == 0) {
        arr[idx].group_size = 0;
    }
}

static void trySeatQueue(DiningTable* t, ClientsQueue* q, int tcount, int qid) {  //Staramy się rozładować kolejkę w razie możliwości
    int updated = 1;
    while (updated) {
        updated = 0;
        for (int i = 0; i < tcount; i++) {
            int freeSpace = t[i].capacity - t[i].total_seated;
            if (freeSpace <= 0) {  //stolik jest pełny i nie ma możliwości nikogo wpuścić
                continue;
            }
            int grpSize = t[i].group_size;

            if (freeSpace < grpSize && grpSize != 0) {  //stolik jest już zajęty przez grupy o konkretnym rozmiarze
                continue;
            }
            GroupOfClients* newG = dequeueSuitable(q, grpSize, freeSpace);  //wyszukuje pierwszą pasującą grupę
            if (newG) { //newG != 0 -> to znaczy, że w kolejce była jakaś grupa, którą możemy posadzić przy itym stoliku
                seatGroupAtTable(t, i, newG, qid);
                free(newG);
                updated = 1;
            }
        }
    }
}

static void sendClosingSoon(ClientsQueue* q, int queueId) {
    QueueNode* iter = q->head;
    while (iter) {
        GroupOfClients* g = &iter->data;
        CommunicationMessage msg;
        msg.mtype = g->groupPID;
        msg.group = *g;
        msg.tableIndex = NEAR_CLOSING;

        printf(CLR_CASHIER "[Kasjer] Informuję grupę PID(%d), że zaraz zamykamy.\n" CLR_RESET,
               (int)g->groupPID);

        msgsnd(queueId, &msg, sizeof(msg) - sizeof(long), 0);
        iter = iter->next;
    }
    q->currentSize = 0;
}

static void showCurrentTables(DiningTable* arr, int count) {
    printf(CLR_CASHIER "\n--- Stoliki w lokalu ---\n" CLR_RESET);
    for (int i = 0; i < count; i++) {
        printf(CLR_CASHIER "[Stol %2d] Kap: %d | Zaj: %d | GrupaSz: %d | PIDy: (" CLR_RESET, i, arr[i].capacity, arr[i].total_seated, arr[i].group_size);
        for (int j = 0; j < 4; j++) {
            if (arr[i].occupant_pids[j] != 0) {
                printf(CLR_CASHIER " %d " CLR_RESET, (int)arr[i].occupant_pids[j]);
            }
        }
        printf(CLR_CASHIER ")\n" CLR_RESET);
    }
    printf(CLR_CASHIER "************************\n\n" CLR_RESET);
}

// -------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 5) {
        fprintf(stderr, CLR_CASHIER "[Kasjer] Użycie: ./cashier_app x1 x2 x3 x4\n" CLR_RESET);
        exit(1);
    }

    int st1 = atoi(argv[1]);
    int st2 = atoi(argv[2]);
    int st3 = atoi(argv[3]);
    int st4 = atoi(argv[4]);
    int total = st1 + st2 + st3 + st4;

    // Obsługa sygnałów
    struct sigaction sa;
    sa.sa_handler = handleSignals;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    // Generujemy klucze
    key_t kSem = ftok(".", SEMAPHORE_GEN_CHAR);
    if (kSem == -1) {
        perror(CLR_CASHIER "[Kasjer] ftok() sem" CLR_RESET);
        exit(1);
    }
    key_t kShm = ftok(".", SHM_GEN_CHAR);
    if (kShm == -1) {
        perror(CLR_CASHIER "[Kasjer] ftok() shm" CLR_RESET);
        exit(1);
    }
    key_t kMsg = ftok(".", MSG_GEN_CHAR);
    if (kMsg == -1) {
        perror(CLR_CASHIER "[Kasjer] ftok() msg" CLR_RESET);
        exit(1);
    }

    // Tworzymy zasoby
    int semId = createSemaphore(kSem);
    int shmId = createSharedMemory(kShm, sizeof(DiningTable) * total);
    int msgId = createMessageQueue(kMsg);

    // Inicjalizujemy stoliki
    DiningTable* allTables = (DiningTable*)shmat(shmId, NULL, 0);
    if (allTables == (void*)-1) {
        perror(CLR_CASHIER "[Kasjer] błąd shmat()" CLR_RESET);
        exit(1);
    }
    setupTables(allTables, 0, st1, 1);
    setupTables(allTables, st1, st1+st2, 2);
    setupTables(allTables, st1+st2, st1+st2+st3, 3);
    setupTables(allTables, st1+st2+st3, st1+st2+st3+st4, 4); 

    // Kolejka oczekujących
    ClientsQueue waitingLine;
    initQueue(&waitingLine, QUEUE_LIMIT);

    // Statystyki dzienne
    int    soldItems[10] = {0};
    double totalRevenue  = 0.0;
    int    totalClients  = 0;

    printf(CLR_CASHIER "[Kasjer] Startuję z obsługą!\n" CLR_RESET);
    while (!fireSignal && (unsigned long)time(NULL) < forcedFinish) {
        if (!fireSignal && closeIsNear == 1 && queueSize(&waitingLine) > 0) {
            sendClosingSoon(&waitingLine, msgId);
        }

        CommunicationMessage msg;
        // --- Odbiór rezerwacji stolika ---
        int rc = msgrcv(msgId, &msg, sizeof(msg) - sizeof(long), REQUEST_TABLE, IPC_NOWAIT);
        if (rc != -1 && !fireSignal) {
            semaphoreP(semId, MUTEX_INDEX);
            if (queueSize(&waitingLine) > 0) {
                trySeatQueue(allTables, &waitingLine, total, msgId);
            }
            int tIdx = findFreeTable(allTables, msg.group.size, total);
            if (tIdx == NEAR_CLOSING) {
                msg.mtype      = msg.group.groupPID;
                msg.tableIndex = NEAR_CLOSING;
                printf(CLR_CASHIER "[Kasjer] Grupa PID(%d), zamykamy wkrótce, nie wpuszczam.\n" CLR_RESET,
                       (int)msg.group.groupPID);
                msgsnd(msgId, &msg, sizeof(msg) - sizeof(long), 0);
            } else if (tIdx == NO_TABLE_FOUND) {
                if (queueSize(&waitingLine) >= QUEUE_LIMIT) {
                    msg.mtype      = msg.group.groupPID;
                    msg.tableIndex = NO_TABLE_FOUND;
                    printf(CLR_CASHIER "[Kasjer] Grupa PID(%d), kolejka jest przepełniona.\n" CLR_RESET,
                           (int)msg.group.groupPID);
                    msgsnd(msgId, &msg, sizeof(msg) - sizeof(long), 0);
                } else {
                    enqueueGroup(&waitingLine, &msg.group);
                    printQueue(&waitingLine);
                }
            } else {
                seatGroupAtTable(allTables, tIdx, &msg.group, msgId);
            }
            semaphoreV(semId, MUTEX_INDEX);
        } else if (rc == -1) {
            if (errno != ENOMSG && errno != EINTR) {
                perror(CLR_CASHIER "[Kasjer] Błąd msgrcv() rezerwacja" CLR_RESET);
                exit(1);
            }
        }

        // --- Odbiór zamówień ---
        rc = msgrcv(msgId, &msg, sizeof(msg) - sizeof(long), SEND_ORDER, IPC_NOWAIT);
        if (rc != -1 && !fireSignal) {
            for (int i = 0; i < msg.group.size; i++) {
                soldItems[msg.orderedItems[i]]++;
                totalRevenue += pizzaMenu[msg.orderedItems[i]].cost;
            }
            totalClients += msg.group.size;
            semaphoreP(semId, MUTEX_INDEX);
            showCurrentTables(allTables, total);
            semaphoreV(semId, MUTEX_INDEX);
        } else if (rc == -1) {
            if (errno != ENOMSG && errno != EINTR) {
                perror(CLR_CASHIER "[Kasjer] Błąd msgrcv() zamówienie" CLR_RESET);
                exit(1);
            }
        }

        // --- Odbiór wyjścia klientów ---
        rc = msgrcv(msgId, &msg, sizeof(msg) - sizeof(long), LEAVE_TABLE, IPC_NOWAIT);
        if (rc != -1 && !fireSignal) {
            semaphoreP(semId, MUTEX_INDEX);
            removeGroupFromTable(allTables, msg.tableIndex, msg.group.groupPID, msg.group.size);
            if (queueSize(&waitingLine) > 0) {
                trySeatQueue(allTables, &waitingLine, total, msgId);
            }
            semaphoreV(semId, MUTEX_INDEX);
        } else if (rc == -1) {
            if (errno != ENOMSG && errno != EINTR) {
                perror(CLR_CASHIER "[Kasjer] Błąd msgrcv() wyjście klienta" CLR_RESET);
                exit(1);
            }
        }
    }

    if (!fireSignal) {
        printf(CLR_CASHIER "[Kasjer] Pozwalam dokończyć jedzenie tym, co jeszcze siedzą.\n" CLR_RESET);
    }

    // Oczekiwanie aż wszystkie stoliki będą puste
    while (1) {
        int allFree = 1;
        for (int i = 0; i < total; i++) {
            if (allTables[i].total_seated != 0) {
                allFree = 0;
                break;
            }
        }
        if (allFree) {
            break;
        }
        if (!fireSignal) {
            CommunicationMessage exitMsg;
            if (msgrcv(msgId, &exitMsg, sizeof(exitMsg) - sizeof(long), LEAVE_TABLE, IPC_NOWAIT) == -1) {
                if (errno == ENOMSG || errno == EINTR) {
                    continue;
                } else {
                    perror(CLR_CASHIER "[Kasjer] Błąd msgrcv() w fazie końcowej" CLR_RESET);
                    exit(1);
                }
            } else {
                semaphoreP(semId, MUTEX_INDEX);
                removeGroupFromTable(allTables, exitMsg.tableIndex, exitMsg.group.groupPID, exitMsg.group.size);
                semaphoreV(semId, MUTEX_INDEX);
            }
        }
    }

    // Generowanie raportu
    int fd = open("daily_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        perror(CLR_CASHIER "[Kasjer] Błąd przy otwarciu pliku raportu" CLR_RESET);
        exit(1);
    }
    char line[256];
    snprintf(line, sizeof(line), "----- Dzienny raport pizzerii -----\n");
    write(fd, line, strlen(line));

    snprintf(line, sizeof(line), "Liczba obsłużonych osób: %d\n", totalClients);
    write(fd, line, strlen(line));

    snprintf(line, sizeof(line), "Całkowity utarg: %.2lf zł\n", totalRevenue);
    write(fd, line, strlen(line));

    snprintf(line, sizeof(line), "Sprzedane produkty:\n");
    write(fd, line, strlen(line));

    for (int i = 0; i < 10; i++) {
        snprintf(line, sizeof(line), "  %s: %d\n", pizzaMenu[i].name, soldItems[i]);
        write(fd, line, strlen(line));
    }
    close(fd);

    sleep(1);
    printf(CLR_CASHIER "[Kasjer] Kończę pracę.\n" CLR_RESET);

    // Usuwamy kolejkę
    deleteMessageQueue(msgId);
    // Odłączamy shm
    if (shmdt(allTables) == -1) {
        perror(CLR_CASHIER "[Kasjer] Błąd shmdt()" CLR_RESET);
    }
    return 0;
}
