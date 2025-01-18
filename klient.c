// klient.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

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
        perror("msgsnd");
        pthread_exit(NULL);
    }
}

// Funkcja odbierająca komunikat
void receive_message(int mqid, long type, char* buffer_text, size_t buffer_size) {
    struct msgbuf buffer;

    if (msgrcv(mqid, &buffer, sizeof(buffer.mtext), type, 0) < 0) {
        perror("msgrcv");
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

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <group_size>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int group_size = atoi(argv[1]);
    if (group_size < 1 || group_size > 4) {
        fprintf(stderr, "Group size must be between 1 and 4.\n");
        exit(EXIT_FAILURE);
    }

    printf("Jestem procesem generującym wątki klientów! Liczba osób: %d\n", group_size);

    // Inicjalizacja struktury grupy klienta
    struct client_group group;
    group.group_size = group_size;
    group.pid = getpid();
    group.order[0] = '\0';
    if (pthread_mutex_init(&group.lock, NULL) != 0) {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    // Klucz kolejki komunikatów
    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror("ERROR ftok [klient]");
        exit(EXIT_FAILURE);
    }

    // Uzyskanie dostępu do kolejki komunikatów
    int msgq1_id = msgget(msgq1_key, 0600);
    if (msgq1_id < 0) {
        perror("ERROR msgget [klient]: No such file or directory");
        printf("Upewnij się, że kasjer jest uruchomiony przed klientem.\n");
        exit(EXIT_FAILURE);
    }

    // Wysłanie żądania stolika
    char request_msg[64];
    snprintf(request_msg, sizeof(request_msg), "REQUEST:%d:%d", group_size, group.pid);
    send_message(msgq1_id, 1, request_msg);
    printf("Wysłano żądanie stolika dla grupy %d osób.\n", group_size);

    // Oczekiwanie na odpowiedź
    char response[256];
    receive_message(msgq1_id, group.pid, response, sizeof(response));
    printf("Odebrano odpowiedź: %s\n", response);

    if (strncmp(response, "CONFIRMED", 9) == 0) {
        printf("Stolik przydzielony. Generowanie wątków klientów.\n");

        // Tworzenie wątków
        pthread_t threads[group_size];
        for (int i = 0; i < group_size; i++) {
            if (pthread_create(&threads[i], NULL, person_thread, &group) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
        }

        // Czekanie na zakończenie wątków
        for (int i = 0; i < group_size; i++) {
            pthread_join(threads[i], NULL);
        }

        printf("Łączne zamówienie: %s\n", group.order);

        // Wysłanie zamówienia wraz z rozmiarem grupy
        char order_msg[1024];
        snprintf(order_msg, sizeof(order_msg), "ORDER:%d:%d:%s", group_size, group.pid, group.order);
        send_message(msgq1_id, 2, order_msg);
        printf("Wysłano zamówienie do kasjera.\n");

        // Oczekiwanie na potwierdzenie zamówienia
        char order_response[256];
        receive_message(msgq1_id, group.pid, order_response, sizeof(order_response));
        printf("Odebrano potwierdzenie zamówienia: %s\n", order_response);
    } else {
        printf("Nie ma dostępnych stolików dla grupy %d osób.\n", group_size);
    }

    // Czyszczenie
    pthread_mutex_destroy(&group.lock);

    return 0;
}
