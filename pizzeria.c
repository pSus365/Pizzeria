#include "pizzeria.h"
#include <string.h>

// --------------------- Definicja menu ---------------------
MenuItem pizzaMenu[10] = {
    {"Pizza Simple",       33.99},
    {"Pizza Caprese",      42.99},
    {"Pizza Napoli",       44.99},
    {"Pizza Pepperoni",    37.99},
    {"Pizza Spicy Salami", 42.99},
    {"Pizza Hawaii",       44.99},
    {"Pizza Diablo",       46.99},
    {"Pizza Mare e Monti", 49.99},
    {"Pizza Rustica",      44.99},
    {"Pizza Veggie",       44.99}
};

// --------------------- Semafory ---------------------
int createSemaphore(key_t key) {
    int semId = semget(key, 1, IPC_CREAT | 0600);
    if (semId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semget() przy tworzeniu semafora" CLR_RESET);
        exit(1);
    }
    // Ustawiamy wartość semafora na 1
    if (semctl(semId, MUTEX_INDEX, SETVAL, 1) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semctl() przy inicjalizacji semafora" CLR_RESET);
        exit(1);
    }
    return semId;
}

int accessSemaphore(key_t key) {
    int semId = semget(key, 0, 0);
    if (semId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semget() przy dołączaniu do semafora" CLR_RESET);
        exit(1);
    }
    return semId;
}

void removeSemaphore(int semId) {
    if (semctl(semId, 0, IPC_RMID) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semctl() przy usuwaniu semafora" CLR_RESET);
    }
}

// --------------------- Pamięć współdzielona ---------------------
int createSharedMemory(key_t key, size_t size) {
    int shmId = shmget(key, size, IPC_CREAT | 0600);
    if (shmId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd shmget() przy tworzeniu pamięci współdzielonej" CLR_RESET);
        exit(1);
    }
    return shmId;
}

int accessSharedMemory(key_t key) {
    int shmId = shmget(key, 0, 0);
    if (shmId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd shmget() przy dołączaniu do pamięci współdzielonej" CLR_RESET);
        exit(1);
    }
    return shmId;
}

void deleteSharedMemory(int shmId, void* addr) {
    if (shmdt(addr) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd shmdt() przy odłączaniu pamięci" CLR_RESET);
    }
    if (shmctl(shmId, IPC_RMID, NULL) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd shmctl() przy usuwaniu segmentu pamięci współdzielonej" CLR_RESET);
    }
}

// --------------------- Kolejka komunikatów ---------------------
int createMessageQueue(key_t key) {
    int msgId = msgget(key, IPC_CREAT | 0600);
    if (msgId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd msgget() przy tworzeniu kolejki" CLR_RESET);
        exit(1);
    }
    return msgId;
}

int accessMessageQueue(key_t key) {
    int msgId = msgget(key, 0);
    if (msgId == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd msgget() przy dołączaniu do kolejki" CLR_RESET);
        exit(1);
    }
    return msgId;
}

void deleteMessageQueue(int msgId) {
    if (msgctl(msgId, IPC_RMID, NULL) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd msgctl() przy usuwaniu kolejki komunikatów" CLR_RESET);
    }
}

// --------------------- Operacje semaforowe ---------------------
void semaphoreP(int semId, int semNum) {
    struct sembuf s;
    s.sem_num = semNum;
    s.sem_op  = -1;
    s.sem_flg = 0;
    if (semop(semId, &s, 1) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semop(P)" CLR_RESET);
        exit(1);
    }
}

void semaphoreV(int semId, int semNum) {
    struct sembuf s;
    s.sem_num = semNum;
    s.sem_op  = 1;
    s.sem_flg = 0;
    if (semop(semId, &s, 1) == -1) {
        perror(CLR_CASHIER "[pizzeria.c] Błąd semop(V)" CLR_RESET);
        exit(1);
    }
}

/**
 * Wypisuje na ekran informację o wybranej pizzy (nazwa + cena),
 * używając indeksu w globalnej tablicy pizzaMenu.
 * @param id Indeks w tablicy pizzaMenu (0-9).
 */

// --------------------- Funkcja wypisująca zamówienie jednej osoby ---------------------
void showChosenPizza(int id) {
    printf(CLR_CLIENT "Wybiera: %s (%.2lf zł)\n" CLR_RESET, pizzaMenu[id].name, pizzaMenu[id].cost);
}

/**
 * Inicjuje pustą kolejkę oczekujących.
 * @param q Wskaźnik na strukturę kolejki.
 * @param limit Maksymalny rozmiar kolejki (maxSize).
 */

// --------------------- Kolejka oczekujących (implementacja) ---------------------
void initQueue(ClientsQueue* q, int limit) {
    q->head         = NULL;
    q->maxSize      = limit;
    q->currentSize  = 0;
}

/**
 * Zwraca aktualny rozmiar kolejki (liczbę czekających grup).
 * @param q Wskaźnik na strukturę kolejki.
 * @return Liczba elementów w kolejce (q->currentSize).
 */

int queueSize(const ClientsQueue* q) {
    return q->currentSize;
}

/**
 * Dodaje nową grupę do końca kolejki, jeśli jest miejsce.
 * @param q Wskaźnik na strukturę kolejki.
 * @param g Grupa (pid, size) do wstawienia.
 * @note Nie sprawdza limitu, zakładamy że wywołujący zrobi to wcześniej.
 */

void enqueueGroup(ClientsQueue* q, const GroupOfClients* g) {
    QueueNode* node = (QueueNode*)calloc(1, sizeof(QueueNode));
    node->data = *g; // kopia struktury GroupOfClients
    node->next = NULL;

    if (q->head == NULL) {
        q->head = node;
    } else {
        QueueNode* temp = q->head;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = node;
    }
    q->currentSize++;
}

/**
 * Wyszukuje i usuwa z kolejki pierwszą grupę, która pasuje do wymagań:
 * - Jeśli neededSize == 0, to każda grupa o size <= freeSeats może wejść.
 * - Jeśli neededSize != 0, to tylko grupa z size == neededSize i size <= freeSeats.
 * Usuwa ją z listy i zwraca kopię zaalokowaną dynamicznie.
 * Jeśli nie znajdzie, zwraca NULL.
 *
 * @param q Wskaźnik na kolejkę.
 * @param neededSize Rozmiar grupy "dominującej" w stoliku (0 oznacza pusty stolik).
 * @param freeSeats Liczba wolnych miejsc w stoliku.
 * @return Wskaźnik na strukturę GroupOfClients lub NULL (jeśli brak odpowiedniej grupy).
 */

GroupOfClients* dequeueSuitable(ClientsQueue* q, int neededSize, int freeSeats) {
    if (q->head == NULL) {
        return NULL;
    }

    QueueNode* prev = NULL;
    QueueNode* curr = q->head;

    while (curr != NULL) {
        GroupOfClients* gr = &curr->data;

        int canEnter = 0;

        if (neededSize == 0) {
            // Jeżeli stolik nie ma przypisanego group_size, wpuścimy każdą grupę, jeśli się zmieści
            if (gr->size <= freeSeats) {
                canEnter = 1;
            }
        } else {
            // Tylko taką samą liczbę osób, co aktualnie przypisana stolikowi
            if (gr->size == neededSize && gr->size <= freeSeats) {
                canEnter = 1;
            }
        }

        if (canEnter) {
            // Wyciągamy z kolejki
            if (prev == NULL) {
                q->head = curr->next;
            } else {
                prev->next = curr->next;
            }
            q->currentSize--;

            GroupOfClients* ret = (GroupOfClients*)malloc(sizeof(GroupOfClients));
            *ret = *gr; 
            free(curr);
            return ret;
        }

        // Nie pasuje? Przechodzimy do kolejnego węzła
        prev = curr;
        curr = curr->next;
    }
    // Jeśli przejdziemy całą pętlę i nic nie znajdziemy, zwracamy NULL
    return NULL;
}

void printQueue(const ClientsQueue* q) {
    printf(CLR_CASHIER "--- Kolejka przed pizzerią ---\n" CLR_RESET);
    QueueNode* tmp = q->head;
    if (!tmp) {
        printf(CLR_CASHIER "[Kolejka] Pusto!\n" CLR_RESET);
        return;
    }
    int idx = 1;
    while (tmp) {
        printf(CLR_CASHIER "%d) Grupa PID(%d) | Osób: %d\n" CLR_RESET,
               idx, (int)tmp->data.groupPID, tmp->data.size);
        idx++;
        tmp = tmp->next;
    }
    printf(CLR_CASHIER "--------------------------------\n" CLR_RESET);
}

void clearQueue(ClientsQueue* q) {
    QueueNode* tmp = q->head;
    while (tmp) {
        QueueNode* toDelete = tmp;
        tmp = tmp->next;
        free(toDelete);
    }
    q->head = NULL;
    q->currentSize = 0;
}
