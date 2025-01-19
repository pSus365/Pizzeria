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
#include <time.h>

// Definicje kolorów ANSI
#define RESET   "\033[0m"
#define GREEN   "\033[32m"

// Struktura komunikatu
struct msgbuf {
    long type;
    char mtext[256];
};

// Menu pizzerii
const char* menu_items[] = {"Margherita", "Pepperoni", "Hawaiian", "Veggie", "BBQ Chicken", "Four Cheese"};
const int menu_size = sizeof(menu_items) / sizeof(menu_items[0]);

// Struktura grupy klienta
struct client_group {
    int group_size;
    int mqid;
    pid_t pid;
    char order[1024];
    pthread_mutex_t lock;
};

// Funkcja wysyłająca komunikat
void send_message(int mqid, long type, const char* message) {
    struct msgbuf buffer;
    buffer.type = type;
    strncpy(buffer.mtext, message, sizeof(buffer.mtext) - 1);
    buffer.mtext[sizeof(buffer.mtext) - 1] = '\0';

    if (msgsnd(mqid, &buffer, strlen(buffer.mtext) + 1, 0) < 0) {
        perror(GREEN "msgsnd" RESET);
        pthread_exit(NULL);
    }
}

// Funkcja odbierająca komunikat
void receive_message(int mqid, long type, char* buffer_text, size_t buffer_size) {
    struct msgbuf buffer;

    if (msgrcv(mqid, &buffer, sizeof(buffer.mtext), type, 0) < 0) {
        perror(GREEN "msgrcv" RESET);
        pthread_exit(NULL);
    } else {
        strncpy(buffer_text, buffer.mtext, buffer_size - 1);
        buffer_text[buffer_size - 1] = '\0';
    }
}

// Funkcja wątku reprezentującego osobę w grupie
void* person_thread(void* arg) {
    struct client_group* group = (struct client_group*)arg;

    // Wybór losowej pozycji z menu
    srand(time(NULL) ^ pthread_self());
    int choice = rand() % menu_size;
    const char* selected_item = menu_items[choice];

    // Dodanie zamówienia do grupowego zamówienia
    pthread_mutex_lock(&group->lock);
    if (strlen(group->order) > 0) {
        strcat(group->order, ",");
    }
    strcat(group->order, selected_item);
    pthread_mutex_unlock(&group->lock);

    pthread_exit(NULL);
}

// Funkcja rezerwująca miejsce w semaforze
void reserve_semaphore(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op = -1; // Próba zmniejszenia semafora
    sb.sem_flg = 0;
    if (semop(semid, &sb, 1) == -1) {
        perror(GREEN "semop reserve" RESET);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, GREEN "Usage: %s <group_size>\n" RESET, argv[0]);
        exit(EXIT_FAILURE);
    }

    int group_size = atoi(argv[1]);
    if (group_size < 1 || group_size > 4) {
        fprintf(stderr, GREEN "Group size must be between 1 and 4.\n" RESET);
        exit(EXIT_FAILURE);
    }

    printf(GREEN "Jestem procesem generującym wątki klientów! Liczba osób: %d\n" RESET, group_size);

    // Inicjalizacja struktury grupy klienta
    struct client_group group;
    group.group_size = group_size;
    group.pid = getpid();
    group.order[0] = '\0';
    if (pthread_mutex_init(&group.lock, NULL) != 0) {
        perror(GREEN "pthread_mutex_init" RESET);
        exit(EXIT_FAILURE);
    }

    // Klucz kolejki komunikatów
    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror(GREEN "ERROR ftok [klient]" RESET);
        exit(EXIT_FAILURE);
    }

    // Uzyskanie dostępu do kolejki komunikatów
    int msgq1_id = msgget(msgq1_key, 0600);
    if (msgq1_id < 0) {
        perror(GREEN "ERROR msgget [klient]: No such file or directory" RESET);
        printf(GREEN "Upewnij się, że kasjer jest uruchomiony przed klientem.\n" RESET);
        exit(EXIT_FAILURE);
    }

    // Klucz semafora
    key_t sem_key = ftok(".", 'S');
    if (sem_key < 0) {
        perror(GREEN "ERROR ftok [klient]" RESET);
        exit(EXIT_FAILURE);
    }

    // Uzyskanie dostępu do semafora
    int semid = semget(sem_key, 1, 0);
    if (semid < 0) {
        perror(GREEN "ERROR semget [klient]: No such semaphore" RESET);
        printf(GREEN "Pizzeria może być zamknięta lub semafor nie został zainicjalizowany.\n" RESET);
        exit(EXIT_FAILURE);
    }

    // Rezerwacja miejsca w semaforze przed wejściem do pizzerii
    reserve_semaphore(semid);
    printf(GREEN "Zarezerwowano miejsce w pizzerii.\n" RESET);

    // Wysłanie żądania stolika
    char request_msg[64];
    snprintf(request_msg, sizeof(request_msg), "REQUEST:%d:%d", group_size, group.pid);
    send_message(msgq1_id, 1, request_msg);
    printf(GREEN "Wysłano żądanie stolika dla grupy %d osób.\n" RESET, group_size);

    // Oczekiwanie na odpowiedź
    char response[256];
    receive_message(msgq1_id, group.pid, response, sizeof(response));
    printf(GREEN "Odebrano odpowiedź: %s\n" RESET, response);

    if (strncmp(response, "CONFIRMED", 9) == 0) {
        printf(GREEN "Stolik przydzielony. Generowanie wątków klientów.\n" RESET);

        // Tworzenie wątków
        pthread_t threads[group_size];
        for (int i = 0; i < group_size; i++) {
            if (pthread_create(&threads[i], NULL, person_thread, &group) != 0) {
                perror(GREEN "pthread_create" RESET);
                exit(EXIT_FAILURE);
            }
        }

        // Czekanie na zakończenie wątków
        for (int i = 0; i < group_size; i++) {
            pthread_join(threads[i], NULL);
        }

        printf(GREEN "Łączne zamówienie: %s\n" RESET, group.order);

        // Wysłanie zamówienia wraz z rozmiarem grupy
        char order_msg[1024];
        snprintf(order_msg, sizeof(order_msg), "ORDER:%d:%d:%s", group_size, group.pid, group.order);
        send_message(msgq1_id, 2, order_msg);
        printf(GREEN "Wysłano zamówienie do kasjera.\n" RESET);

        // Oczekiwanie na potwierdzenie zamówienia
        char order_response[256];
        receive_message(msgq1_id, group.pid, order_response, sizeof(order_response));
        printf(GREEN "Odebrano potwierdzenie zamówienia: %s\n" RESET, order_response);

        // **Nowa funkcjonalność: Symulacja jedzenia pizzy**
        // Czas jedzenia pizzy (symulacja) - losowy czas między 5 a 15 sekundami
        int eating_time = rand() % 11 + 5;
        printf(GREEN "Grupa %d osób zaczyna jeść pizzę. Czas jedzenia: %d sekund.\n" RESET, group_size, eating_time);
        sleep(eating_time);
        printf(GREEN "Grupa %d osób zakończyła jedzenie pizzy.\n" RESET, group_size);

        // Wysłanie komunikatu o zakończeniu jedzenia
        char finish_msg[64];
        snprintf(finish_msg, sizeof(finish_msg), "FINISH:%d:%d", group_size, group.pid);
        send_message(msgq1_id, 1, finish_msg);
        printf(GREEN "Wysłano informację o zakończeniu jedzenia do kasjera.\n" RESET);

    } else {
        printf(GREEN "Nie ma dostępnych stolików dla grupy %d osób.\n" RESET, group_size);
    }

    // Czyszczenie
    pthread_mutex_destroy(&group.lock);

    return 0;
}
