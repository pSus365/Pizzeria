#include "pizzeria.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <sched.h>   // <-- potrzebne do sched_yield()

static volatile sig_atomic_t fireEvent = 0;

// Obsługa sygnału pożaru
static void signalFire(int sig) {
    if (sig == SIGUSR1) {
        fireEvent = 1;
    }
}

// Walidacja argumentów: X1, X2, X3, X4
static int validateArgs(int argc, char* argv[]) {
    if (argc != 5) {
        fprintf(stderr, CLR_MGR "Użycie: ./manager_app X1 X2 X3 X4\n" CLR_RESET);
        exit(1);
    }
    int x1 = atoi(argv[1]);
    int x2 = atoi(argv[2]);
    int x3 = atoi(argv[3]);
    int x4 = atoi(argv[4]);
    if ((x1 < 0 || x2 < 0 || x3 < 0 || x4 < 0) || (x1 + x2 + x3 + x4) == 0) {
        fprintf(stderr, CLR_MGR "Błędne argumenty (≥0 i co najmniej jedna wartość > 0)\n" CLR_RESET);
        exit(1);
    }
    return x1 + x2 + x3 + x4;
}

// Wyświetlenie raportu na koniec (plik daily_report.txt)
static void displayReport() {
    int fd = open("daily_report.txt", O_RDONLY);
    if (fd == -1) {
        perror(CLR_MGR "[Manager] Brak pliku raportu lub błąd otwarcia" CLR_RESET);
        return;
    }
    char buffer[256];
    int  bytes;
    while ((bytes = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    close(fd);
}

// --------------------------------------------------------
int main(int argc, char* argv[]) {
    // Sprawdzamy poprawność argumentów i wyliczamy liczbę stolików
    int totalTables = validateArgs(argc, argv);
    pid_t managerPid = getpid();
    srand(time(NULL));

    // Ustawienie obsługi sygnału pożaru (SIGUSR1)
    struct sigaction sa;
    sa.sa_handler = signalFire;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror(CLR_MGR "[Manager] Błąd ustawienia sigaction" CLR_RESET);
        exit(1);
    }

    // Uruchomienie kasjera (cashier_app)
    pid_t cashierPid = fork();
    if (cashierPid == -1) {
        perror(CLR_MGR "[Manager] Błąd fork() podczas tworzenia kasjera" CLR_RESET);
        exit(1);
    }
    if (cashierPid == 0) {
        // Proces potomny – kasjer
        execl("./cashier_app", "cashier_app", argv[1], argv[2], argv[3], argv[4], NULL);
        perror(CLR_MGR "[Manager] Nie udało się uruchomić kasjera" CLR_RESET);
        exit(1);
    }

    // Pętla oczekiwania na zasoby (semafor, shm)
    key_t keySem = ftok(".", SEMAPHORE_GEN_CHAR);
    if (keySem == -1) {
        perror(CLR_MGR "[Manager] Błąd ftok() dla semafora" CLR_RESET);
        exit(1);
    }
    key_t keyShm = ftok(".", SHM_GEN_CHAR);
    if (keyShm == -1) {
        perror(CLR_MGR "[Manager] Błąd ftok() dla shm" CLR_RESET);
        exit(1);
    }

    // 1) Czekamy, aż semafor zostanie utworzony przez kasjera
    int semId = -1;
    while (1) {
        semId = semget(keySem, 0, 0);  // próba dołączenia do istniejącego semafora
        if (semId != -1) {
            break; // semafor istnieje
        }
        if (errno != ENOENT) {
            perror(CLR_MGR "[Manager] Błąd semget() podczas czekania na kasjera" CLR_RESET);
            exit(1);
        }
        // aby uniknąć 100% CPU, oddajemy kwant czasu innym procesom
        sched_yield();
    }
    
    // 2) Czekamy, aż pamięć współdzielona zostanie utworzona
    int shmId = -1;
    while (1) {
        shmId = shmget(keyShm, 0, 0);
        if (shmId != -1) {
            break; // pamięć istnieje
        }
        if (errno != ENOENT) {
            perror(CLR_MGR "[Manager] Błąd shmget() podczas czekania na kasjera" CLR_RESET);
            exit(1);
        }
        sched_yield();
    }

    // Uruchamiamy strażaka (fireman_app)
    pid_t firemanPid = fork();
    if (firemanPid == -1) {
        perror(CLR_MGR "[Manager] Błąd fork() podczas tworzenia strażaka" CLR_RESET);
        exit(1);
    }
    if (firemanPid == 0) {
        char bufCashier[30], bufManager[30], bufTables[30];
        snprintf(bufCashier,  sizeof(bufCashier), "%d", cashierPid);
        snprintf(bufManager,  sizeof(bufManager), "%d", managerPid);
        snprintf(bufTables,   sizeof(bufTables), "%d", totalTables);

        execl("./fireman_app", "fireman_app", bufCashier, bufManager, bufTables, NULL);
        perror(CLR_MGR "[Manager] Nie udało się uruchomić strażaka" CLR_RESET);
        exit(1);
    }

    // Dołączamy się do pamięci współdzielonej
    DiningTable* tables = (DiningTable*)shmat(shmId, NULL, 0);
    if (tables == (void*)-1) {
        perror(CLR_MGR "[Manager] Błąd przy shmat()" CLR_RESET);
        exit(1);
    }

    unsigned long closeTime = time(NULL) + RUNTIME_LIMIT;
    int totalActive = 0;
    int notifiedClose = 0;

    // Pętla generowania klientów
    while (!fireEvent && (!notifiedClose || time(NULL) < closeTime)) {
        // Tworzymy 1-3 osobową grupę
        int groupSize = rand() % 3 + 1;

        if (totalActive < MAX_CUSTOMERS && !fireEvent) {
            totalActive++;
            pid_t childPid = fork();
            if (childPid == -1) {
                perror(CLR_MGR "[Manager] Błąd fork() przy tworzeniu klienta" CLR_RESET);
                exit(1);
            }
            if (childPid == 0) {
                // Proces klienta
                char sizeBuf[10];
                snprintf(sizeBuf, sizeof(sizeBuf), "%d", groupSize);
                execl("./client_app", "client_app", sizeBuf, NULL);
                perror(CLR_MGR "[Manager] Nie udało się uruchomić klienta" CLR_RESET);
                exit(1);
            }
        }

        // Odstęp między kolejnymi grupami (0.5 - 1.5 s)
        int pauseMicroSec = (rand() % 1001 + 500) * 1000;
        usleep(pauseMicroSec);

        // Ostrzegamy kasjera o zbliżającym się zamknięciu
        if (!notifiedClose && (closeTime - TIME_BEFORE_CLOSE <= time(NULL))) {
            notifiedClose = 1;
            printf(CLR_MGR "[Manager] Ostrzegam kasjera: niedługo zamykamy!\n" CLR_RESET);
            kill(cashierPid, SIGUSR2);
        }

        // Zbieramy procesy-zombie
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            totalActive--;
        }
    }

    // Czekamy aż kasjer się zakończy
    while (waitpid(cashierPid, NULL, 0) == -1) {
        if (errno == EINTR)  continue;
        if (errno == ECHILD) break;
        perror(CLR_MGR "[Manager] Błąd waitpid() dla kasjera" CLR_RESET);
        break;
    }

    // Jeśli nie było pożaru, a kasjer już nie żyje, to strażak może być niepotrzebny
    if (!fireEvent && kill(cashierPid, 0) != 0) {
        kill(firemanPid, SIGTERM);
    }

    // Zbieramy resztę dzieci
    while (wait(NULL) > 0);

    // Usuwamy pamięć współdzieloną i semafor
    deleteSharedMemory(shmId, tables);
    removeSemaphore(semId);

    // Wyświetlamy końcowy raport
    printf(CLR_MGR "[Manager] Końcowy raport z dnia:\n" CLR_RESET);
    displayReport();

    return 0;
}
