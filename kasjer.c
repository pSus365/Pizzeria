#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

// Definicje kolorów ANSI
#define RESET   "\033[0m"
#define BLUE    "\033[34m"

// Struktura komunikatu
struct msgbuf {
    long type;
    char mtext[256];
};

// Menu pizzerii z cenami
struct menu_item {
    char name[50];
    double price;
} menu_items[] = {
    {"Margherita", 20.0},
    {"Pepperoni", 25.0},
    {"Hawaiian", 22.5},
    {"Veggie", 18.0},
    {"BBQ Chicken", 27.0},
    {"Four Cheese", 24.0}
};
const int menu_size = sizeof(menu_items) / sizeof(menu_items[0]);

// Struktura kasjera
struct kasjer_info {
    int tables[4];                // Aktualny stan stolików
    int initial_tables[4];        // Początkowy stan stolików
    double revenue;               // Całkowity utarg
    int total_orders;             // Łączna liczba zamówień
    int tables_assigned[4];       // Liczba przydzielonych stolików dla każdej wielkości grupy
    pthread_mutex_t lock;         // Mutex do synchronizacji
    int accept_new_orders;        // Flaga akceptacji nowych zamówień
    int stop;                      // Flaga zakończenia pracy
} kasjer;

// Flaga do sygnalizacji zakończenia pętli
volatile sig_atomic_t global_stop = 0;

// Funkcja obsługi sygnału SIGINT
void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf(BLUE "\nOdebrano sygnał SIGINT (Ctrl+C). Kończenie pracy kasjera...\n" RESET);
        global_stop = 1;
    }
}

// Inicjalizacja kasjera
void init_kasjer(struct kasjer_info* kasjer, int a1, int a2, int a3, int a4) {
    kasjer->tables[0] = a1;
    kasjer->tables[1] = a2;
    kasjer->tables[2] = a3;
    kasjer->tables[3] = a4;
    kasjer->initial_tables[0] = a1;
    kasjer->initial_tables[1] = a2;
    kasjer->initial_tables[2] = a3;
    kasjer->initial_tables[3] = a4;
    kasjer->revenue = 0.0;
    kasjer->total_orders = 0;
    for (int i = 0; i < 4; i++) {
        kasjer->tables_assigned[i] = 0;
    }
    kasjer->accept_new_orders = 1;
    kasjer->stop = 0;
    if (pthread_mutex_init(&kasjer->lock, NULL) != 0) {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }
}

// Funkcja wysyłająca komunikat
void send_message(int mqid, long type, const char* message) {
    struct msgbuf buffer;
    buffer.type = type;
    strncpy(buffer.mtext, message, sizeof(buffer.mtext) - 1);
    buffer.mtext[sizeof(buffer.mtext) - 1] = '\0';

    if (msgsnd(mqid, &buffer, strlen(buffer.mtext) + 1, 0) < 0) {
        perror(BLUE "msgsnd" RESET);
        // Nie kończ programu, aby kontynuować obsługę innych klientów
    }
}

// Funkcja generująca raport
void generate_report(struct kasjer_info* kasjer) {
    printf(BLUE "\n===== Raport Działania Pizzerii =====\n" RESET);
    printf(BLUE "Całkowity utarg: %.2f PLN\n" RESET, kasjer->revenue);
    printf(BLUE "Łączna liczba zamówień: %d\n" RESET, kasjer->total_orders);
    printf(BLUE "Przydzielone stoliki:\n" RESET);
    for (int i = 0; i < 4; i++) {
        printf(BLUE "  %d-osobowe: %d\n" RESET, i + 1, kasjer->tables_assigned[i]);
    }
    printf(BLUE "=====================================\n" RESET);

    // Zapisanie raportu do pliku
    FILE* report_file = fopen("report.txt", "a");
    if (report_file == NULL) {
        perror(BLUE "fopen" RESET);
        return;
    }

    fprintf(report_file, "\n===== Raport Działania Pizzerii =====\n");
    fprintf(report_file, "Całkowity utarg: %.2f PLN\n", kasjer->revenue);
    fprintf(report_file, "Łączna liczba zamówień: %d\n", kasjer->total_orders);
    fprintf(report_file, "Przydzielone stoliki:\n");
    for (int i = 0; i < 4; i++) {
        fprintf(report_file, "  %d-osobowe: %d\n", i + 1, kasjer->tables_assigned[i]);
    }
    fprintf(report_file, "=====================================\n");
    fclose(report_file);
}

// Funkcja zwalniająca miejsce w semaforze
void release_semaphore(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = 1; // Zwiększenie wartości semafora
    sb.sem_flg = 0;
    if (semop(semid, &sb, 1) == -1) {
        perror(BLUE "semop release" RESET);
    }
}

// Funkcja inicjalizująca semafor
int init_semaphore(int initial_value) {
    key_t sem_key = ftok(".", 'S');
    if (sem_key < 0) {
        perror(BLUE "ftok [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    int semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid < 0) {
        perror(BLUE "semget [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    if (semctl(semid, 0, SETVAL, initial_value) == -1) {
        perror(BLUE "semctl SETVAL [kasjer]" RESET);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    return semid;
}

// Funkcja obsługująca komunikaty
void* handle_messages(void* arg) {
    struct kasjer_info* kasjer = (struct kasjer_info*)arg;

    // Klucz kolejki komunikatów
    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror(BLUE "ERROR ftok [kasjer]" RESET);
        pthread_exit(NULL);
    }

    // Uzyskanie dostępu do kolejki komunikatów
    int msgq1_id = msgget(msgq1_key, 0600);
    if (msgq1_id < 0) {
        perror(BLUE "ERROR msgget [kasjer]" RESET);
        pthread_exit(NULL);
    }

    // Klucz semafora
    key_t sem_key = ftok(".", 'S');
    if (sem_key < 0) {
        perror(BLUE "ERROR ftok [kasjer]" RESET);
        pthread_exit(NULL);
    }

    // Uzyskanie dostępu do semafora
    int semid = semget(sem_key, 1, 0);
    if (semid < 0) {
        perror(BLUE "ERROR semget [kasjer]" RESET);
        pthread_exit(NULL);
    }

    while (!kasjer->stop) {
        struct msgbuf buffer;
        // Odbieranie dowolnego komunikatu z flagą IPC_NOWAIT
        if (msgrcv(msgq1_id, &buffer, sizeof(buffer.mtext), 0, IPC_NOWAIT) < 0) {
            if (errno == EAGAIN || errno == ENOMSG) {
                // Brak wiadomości, kontynuuj
                usleep(100000); // 100 ms
                continue;
            } else if (errno == EINTR) {
                continue; // Przerwanie przez sygnał
            } else {
                perror(BLUE "msgrcv" RESET);
                continue;
            }
        }

        // Analiza typu komunikatu
        if (strncmp(buffer.mtext, "REQUEST:", 8) == 0) {
            // Obsługa żądania stolika
            int group_size;
            pid_t client_pid;
            sscanf(buffer.mtext, "REQUEST:%d:%d", &group_size, &client_pid);

            printf(BLUE "Otrzymano żądanie stolika dla grupy %d osób od PID %d.\n" RESET, group_size, client_pid);

            char reply[256];
            if (group_size < 1 || group_size > 4) {
                snprintf(reply, sizeof(reply), "REJECTED: Invalid group size %d.", group_size);
            } else {
                pthread_mutex_lock(&kasjer->lock);
                if (kasjer->accept_new_orders && kasjer->tables[group_size - 1] > 0) {
                    kasjer->tables[group_size - 1]--;
                    kasjer->tables_assigned[group_size - 1]++;
                    kasjer->total_orders++;
                    snprintf(reply, sizeof(reply), "CONFIRMED: Assigned to %d-person table.", group_size);
                } else {
                    snprintf(reply, sizeof(reply), "REJECTED: No available table for %d persons or accepting new orders is disabled.", group_size);
                }
                pthread_mutex_unlock(&kasjer->lock);
            }

            // Wysłanie odpowiedzi do klienta
            send_message(msgq1_id, client_pid, reply);
            printf(BLUE "Wysłano odpowiedź do PID %d: %s\n" RESET, client_pid, reply);

        } else if (strncmp(buffer.mtext, "ORDER:", 6) == 0) {
            // Obsługa zamówienia
            int group_size;
            int client_pid;
            char items_str[256];
            sscanf(buffer.mtext, "ORDER:%d:%d:%255[^\n]", &group_size, &client_pid, items_str);

            printf(BLUE "Otrzymano zamówienie od PID %d dla grupy %d: %s\n" RESET, client_pid, group_size, items_str);

            // Parsowanie zamówienia
            char* token;
            double total = 0.0;
            token = strtok(items_str, ",");
            while (token != NULL) {
                // Znajdowanie ceny dla każdego przedmiotu
                for (int i = 0; i < menu_size; i++) {
                    if (strcmp(token, menu_items[i].name) == 0) {
                        total += menu_items[i].price;
                        break;
                    }
                }
                token = strtok(NULL, ",");
            }

            // Dodanie do utargu
            pthread_mutex_lock(&kasjer->lock);
            kasjer->revenue += total;
            pthread_mutex_unlock(&kasjer->lock);

            // Symulacja przygotowania pizzy
            sleep(2); // Czas na przygotowanie zamówienia

            // Wysłanie potwierdzenia do klienta
            char order_reply[256];
            snprintf(order_reply, sizeof(order_reply), "ORDER CONFIRMED: Your order totaling %.2f PLN is ready.", total);
            send_message(msgq1_id, client_pid, order_reply);
            printf(BLUE "Wysłano potwierdzenie zamówienia do PID %d: %s\n" RESET, client_pid, order_reply);

            // **Nie zwalniamy stolika tutaj. Zostanie zwolniony po komunikacie FINISH.**

        } else if (strncmp(buffer.mtext, "FINISH:", 7) == 0) {
            // Obsługa komunikatu o zakończeniu jedzenia
            int group_size;
            pid_t client_pid;
            sscanf(buffer.mtext, "FINISH:%d:%d", &group_size, &client_pid);

            printf(BLUE "Otrzymano informację o zakończeniu jedzenia od PID %d dla grupy %d.\n" RESET, client_pid, group_size);

            if (group_size >=1 && group_size <=4) {
                pthread_mutex_lock(&kasjer->lock);
                kasjer->tables[group_size - 1]++;
                kasjer->tables_assigned[group_size - 1]--;
                pthread_mutex_unlock(&kasjer->lock);
                printf(BLUE "Zwolniono stolik dla grupy %d osób.\n" RESET, group_size);
            } else {
                printf(BLUE "Nieprawidłowy rozmiar grupy: %d.\n" RESET, group_size);
            }

            // Zwalnianie semafora
            release_semaphore(semid);
        } else {
            printf(BLUE "Otrzymano nieznany komunikat: %s\n" RESET, buffer.mtext);
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[])
{
    if (argc != 6) {  // Teraz oczekujemy 5 argumentów: 4 stoliki + czas_dzialania_pizzerii
        fprintf(stderr, BLUE "Usage: %s <tables_1> <tables_2> <tables_3> <tables_4> <czas_dzialania_pizzerii>\n" RESET, argv[0]);
        exit(EXIT_FAILURE);
    }

    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);
    int czas_dzialania_pizzerii = atoi(argv[5]);

    if(a1 < 0 || a2 < 0 || a3 < 0 || a4 < 0 || czas_dzialania_pizzerii <= 0){
        fprintf(stderr, BLUE "Niepoprawne argumenty! Liczba stolików musi być >=0, a czas_dzialania_pizzerii >0.\n" RESET);
        exit(EXIT_FAILURE);
    }

    printf(BLUE "Kasjer zaczyna pracę! (kasjer)\n" RESET);

    struct kasjer_info kasjer;
    init_kasjer(&kasjer, a1, a2, a3, a4);

    printf(BLUE "Witam, mam %d stolików 1-osobowych, %d stolików 2-osobowych, %d stolików 3-osobowych, %d stolików 4-osobowych.\n" RESET,
           kasjer.tables[0], kasjer.tables[1], kasjer.tables[2], kasjer.tables[3]);

    // Klucz kolejki komunikatów
    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror(BLUE "ERROR ftok [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    // Tworzenie kolejki komunikatów
    int msgq1_id = msgget(msgq1_key, 0600 | IPC_CREAT | IPC_EXCL);
    if (msgq1_id < 0) {
        perror(BLUE "ERROR msgget [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    // Wysłanie wiadomości powitalnej
    char welcome_msg[] = "Zapraszam do zamawiania!";
    send_message(msgq1_id, 1, welcome_msg);
    printf(BLUE "Wysłano wiadomość powitalną.\n" RESET);

    // Ustawienie obsługi sygnału SIGINT
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror(BLUE "sigaction SIGINT" RESET);
        // Usunięcie kolejki przed zakończeniem
        msgctl(msgq1_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    // Tworzenie wątku do obsługi komunikatów
    pthread_t handler_thread;
    if (pthread_create(&handler_thread, NULL, handle_messages, &kasjer) != 0) {
        perror(BLUE "pthread_create" RESET);
        // Usunięcie kolejki przed zakończeniem
        msgctl(msgq1_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    // Obliczenie liczby miejsc w pizzerii
    int total_seats = a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4;

    // Inicjalizacja semafora z liczbą miejsc
    int semid = init_semaphore(total_seats);
    printf(BLUE "Zainicjalizowano semafor z wartością: %d\n" RESET, total_seats);

    // Ustalony czas działania kasjera (otrzymany jako argument)
    int total_duration = czas_dzialania_pizzerii; // w sekundach
    int time_before_stop_accepting = 5; // w sekundach

    // Timer: total_duration - time_before_stop_accepting przed zatrzymaniem akceptacji
    sleep(total_duration - time_before_stop_accepting);

    // Zatrzymanie akceptacji nowych zamówień
    pthread_mutex_lock(&kasjer.lock);
    kasjer.accept_new_orders = 0;
    pthread_mutex_unlock(&kasjer.lock);
    printf(BLUE "\n5 sekund przed zakończeniem pracy pizzerii. Kasjer przestaje akceptować nowe zamówienia.\n" RESET);

    // Oczekiwanie na ostatnie 5 sekund
    sleep(time_before_stop_accepting);

    // Zakończenie pracy kasjera
    pthread_mutex_lock(&kasjer.lock);
    kasjer.stop = 1;
    pthread_mutex_unlock(&kasjer.lock);

    printf(BLUE "\nZakonczono czas dzialania pizzerii. Kasjer przestaje pracowac.\n" RESET);

    // Czyszczenie
    pthread_join(handler_thread, NULL);
    pthread_mutex_destroy(&kasjer.lock);

    // Usunięcie kolejki komunikatów
    if (msgctl(msgq1_id, IPC_RMID, 0) < 0) {
        perror(BLUE "msgctl" RESET);
        exit(EXIT_FAILURE);
    }

    // Usunięcie semafora
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror(BLUE "semctl IPC_RMID [kasjer]" RESET);
    }

    // Generowanie raportu
    generate_report(&kasjer);

    printf(BLUE "Kasjer zakończył działanie.\n" RESET);
    return 0;
}
