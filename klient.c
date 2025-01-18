#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

// Struktura komunikatu
struct msgbuf {
    long type;
    char mtext[50];
};

// Funkcja wysylajaca wiadomosc do kolejki komunikatow
void send_message(int mqid, char message[]) {
    struct msgbuf buffer;
    buffer.type = 1; // Typ komunikatu
    strncpy(buffer.mtext, message, sizeof(buffer.mtext) - 1); // Kopiowanie wiadomosci

    if (msgsnd(mqid, &buffer, sizeof(buffer.mtext), 0) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

// Funkcja odbierajaca wiadomosc z kolejki komunikatow
void receive_message(int mqid) {
    struct msgbuf buffer;

    if (msgrcv(mqid, &buffer, sizeof(buffer.mtext), 1, 0) < 0) {
        perror("msgrcv");
        exit(1);
    } else {
        printf("Odebrany komunikat: %s\n", buffer.mtext);
    }
}




int main(int argc, char * argv[]){
    // generate liczba grup klientow      group size ma byc!!!!!
    int liczba_osob = atoi(argv[1]);
    printf("Jestem procesem generujacym watki klientow! Liczba osob: %d \n", liczba_osob); // TODO generowanie wątków

    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror("ERROR ftok [klient]");
        exit(1);
    }

    sleep(1); // Poczekaj, aż kasjer utworzy kolejkę

    int msgq1_id = msgget(msgq1_key, 0600);
    if (msgq1_id < 0) {
        perror("ERROR msgget [klient]");
        exit(1);
    }

    receive_message(msgq1_id);

    //send_message(msgq1_id, "Super! No to będzie numerek 1,3,4! \n");

    return 0;
}