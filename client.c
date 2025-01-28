#include "pizzeria.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t localMutex;

/**
 * Handler sygnału SIGUSR1 (pożar).
 * Wypisuje komunikat i wywołuje exit(0),
 * co oznacza nagłą ucieczkę z lokalu.
 *
 * @param sig Numer sygnału (oczekiwany SIGUSR1).
 */

// Sygnał pożaru
static void handleFireSignal(int sig) {
    if (sig == SIGUSR1) {
        printf(CLR_CLIENT "[Grupa PID(%d)] W lokalu wybuchł pożar! Uciekamy!\n" CLR_RESET, getpid());
        exit(0);
    }
}

/**
 * Wątek reprezentujący jedną osobę w grupie.
 * - Blokuje mutex localMutex,
 * - Szuka wolnego miejsca w tablicy go->selection,
 * - Wpisuje losowy indeks pizzy (rand()%10),
 * - Wypisuje informację o wybranej pizzy,
 * - Zwalnia mutex i kończy.
 *
 * @param arg Wskaźnik na strukturę GroupOrder (zawiera tablicę orders i liczbę osób).
 * @return Zwraca NULL (kończący wątek).
 */

// Wątek reprezentujący jedną osobę w grupie
void* singlePersonOrder(void* arg) {
    GroupOrder* go = (GroupOrder*)arg;
    pthread_mutex_lock(&localMutex);

    int idx = 0;
    while (idx < go->count && go->selection[idx] != -1) {
        idx++;
    }
    // Losujemy pizzę
    go->selection[idx] = rand() % 10;
    printf(CLR_CLIENT "[Grupa PID(%d), wątek %lu] ", getpid(), (unsigned long)pthread_self());
    showChosenPizza(go->selection[idx]);

    pthread_mutex_unlock(&localMutex);
    pthread_exit(NULL);
}

/**
 * Sprawdza czy program klienta ma 1 argument:
 * <liczba_osób_w_grupie> (1..3).
 * Jeśli błędnie, wypisuje komunikat i exit(1).
 *
 * @param argc liczba argumentów,
 * @param argv tablica argumentów.
 */

static void usageCheck(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, CLR_CLIENT "Użycie: ./client_app <liczba_osób_w_grupie>\n" CLR_RESET);
        exit(1);
    }
    int n = atoi(argv[1]);
    if (n < 1 || n > 3) {
        fprintf(stderr, CLR_CLIENT "Grupa może liczyć 1-3 osoby.\n" CLR_RESET);
        exit(1);
    }
}

/**
 * Główna funkcja klienta (grupy 1-3 osób):
 * 1) Sprawdza argument (usageCheck).
 * 2) Ustawia handler SIGUSR1 (pożar).
 * 3) Dołącza do kolejki msgQueue utworzonej przez kasjera.
 * 4) Wysyła REQUEST_TABLE, czeka na odpowiedź:
 *    - NO_TABLE_FOUND => wychodzi,
 *    - NEAR_CLOSING   => wychodzi,
 *    - w przeciwnym razie otrzymuje tableIndex (>=0).
 * 5) Tworzy wątki (po 1 na osobę w grupie), każdy losuje pizzę.
 * 6) Wysyła SEND_ORDER z informacjami o zamówionych pizzach.
 * 7) Symuluje czas jedzenia (sleep).
 * 8) Wysyła LEAVE_TABLE, by zwolnić stolik.
 * 9) Kończy proces.
 *
 * @param argc Liczba argumentów (powinno być 2).
 * @param argv [1] -> liczba osób w grupie (1..3).
 * @return 0 przy pomyślnym zakończeniu.
 */

int main(int argc, char* argv[]) {
    usageCheck(argc, argv);
    srand(time(NULL));

    // Obsługa sygnału pożaru
    struct sigaction sa;
    sa.sa_handler = handleFireSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror(CLR_CLIENT "[Klient] Błąd sigaction() sygnału pożaru" CLR_RESET);
        exit(1);
    }

    // Dołączenie do kolejki komunikatów
    key_t msgKey = ftok(".", MSG_GEN_CHAR);
    if (msgKey == -1) {
        perror(CLR_CLIENT "[Klient] Błąd ftok() dla kolejki" CLR_RESET);
        exit(1);
    }
    int msgId = accessMessageQueue(msgKey);

    int groupSize = atoi(argv[1]);
    pid_t myPid   = getpid();

    // Wysyłamy zapytanie o stolik
    CommunicationMessage req;
    req.mtype = REQUEST_TABLE;
    req.group.size = groupSize;
    req.group.groupPID = myPid;
    req.tableIndex = -1;
    for (int i = 0; i < 3; i++) {
        req.orderedItems[i] = -1;
    }

    printf(CLR_CLIENT "[Grupa PID(%d)] Mamy %d osób i chcemy stolik.\n" CLR_RESET, (int)myPid, groupSize);
    if (msgsnd(msgId, &req, sizeof(req) - sizeof(long), 0) == -1) {
        if (errno == EIDRM || errno == EINVAL) {
            exit(0); // kolejka usunięta
        }
        perror(CLR_CLIENT "[Klient] Błąd msgsnd() rezerwacji stolika" CLR_RESET);
        exit(1);
    }

    // Odbiór odpowiedzi
    CommunicationMessage resp;
    if (msgrcv(msgId, &resp, sizeof(resp) - sizeof(long), myPid, 0) == -1) {
        if (errno == EIDRM) {
            exit(0);
        }
        perror(CLR_CLIENT "[Klient] Błąd msgrcv() stolik" CLR_RESET);
        exit(1);
    }

    if (resp.tableIndex == NO_TABLE_FOUND) {
        printf(CLR_CLIENT "[Grupa PID(%d)] Zrezygnowaliśmy, kolejka za długa.\n" CLR_RESET, (int)myPid);
        exit(0);
    } else if (resp.tableIndex == NEAR_CLOSING) {
        printf(CLR_CLIENT "[Grupa PID(%d)] Lokal się zamyka, odchodzimy.\n" CLR_RESET, (int)myPid);
        exit(0);
    }

    // Ustawiamy wątki dla każdej osoby w grupie
    if (pthread_mutex_init(&localMutex, NULL) != 0) {
        perror(CLR_CLIENT "[Klient] Błąd pthread_mutex_init()" CLR_RESET);
        exit(1);
    }

    int* myOrders = (int*)malloc(sizeof(int) * groupSize);
    for (int i = 0; i < groupSize; i++) {
        myOrders[i] = -1;
    }

    GroupOrder go;
    go.selection = myOrders;
    go.count = groupSize;

    pthread_t* threads = (pthread_t*)calloc(groupSize, sizeof(pthread_t));
    for (int i = 0; i < groupSize; i++) {
        if (pthread_create(&threads[i], NULL, singlePersonOrder, &go) != 0) { // tworzę wątki które wykonują funkcję singlePersonOrder()
            perror(CLR_CLIENT "[Klient] Błąd pthread_create()" CLR_RESET);
            exit(1);
        }
    }
    for (int i = 0; i < groupSize; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror(CLR_CLIENT "[Klient] Błąd pthread_join()" CLR_RESET);
            exit(1);
        }
    }

    // Wysyłamy zamówienie do kasjera
    CommunicationMessage orderMsg;
    orderMsg.mtype = SEND_ORDER;
    orderMsg.group.size = groupSize;
    orderMsg.group.groupPID = myPid;
    orderMsg.tableIndex = resp.tableIndex;

    double sumCost = 0.0;
    for (int i = 0; i < groupSize; i++) {
        orderMsg.orderedItems[i] = myOrders[i];
        sumCost += pizzaMenu[myOrders[i]].cost;
    }

    if (msgsnd(msgId, &orderMsg, sizeof(orderMsg) - sizeof(long), 0) == -1) {
        perror(CLR_CLIENT "[Klient] Błąd msgsnd() zamówienie" CLR_RESET);
        exit(1);
    }

    printf(CLR_CLIENT "[Grupa PID(%d)] Złożyliśmy zamówienie (%.2f zł) i zajmujemy stolik nr %d.\n" CLR_RESET,
           (int)myPid, sumCost, resp.tableIndex);

    // Symulacja jedzenia
    int eatingDuration = rand() % 6 + 6;
    sleep(eatingDuration);

    // Zwalniamy stolik
    CommunicationMessage leaveMsg;
    leaveMsg.mtype = LEAVE_TABLE;
    leaveMsg.group.size = groupSize;
    leaveMsg.group.groupPID = myPid;
    leaveMsg.tableIndex = resp.tableIndex;
    for (int i = 0; i < 3; i++) {
        leaveMsg.orderedItems[i] = -1;
    }

    if (msgsnd(msgId, &leaveMsg, sizeof(leaveMsg) - sizeof(long), 0) == -1) {
        if (errno != EIDRM && errno != EINVAL) {
            perror(CLR_CLIENT "[Klient] Błąd msgsnd() przy wychodzeniu" CLR_RESET);
        }
    }

    printf(CLR_CLIENT "[Grupa PID(%d)] Kończymy posiłek i zwalniamy stolik nr %d.\n" CLR_RESET,
           (int)myPid, resp.tableIndex);

    pthread_mutex_destroy(&localMutex);
    free(myOrders);
    free(threads);

    return 0;
}
