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

// Struktura komunikatu
struct msgbuf {
    long type;
    char mtext[50];
};

/// Funkcja wysylajaca wiadomosc do kolejki komunikatow
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



int liczba_miejsc(int a1, int a2, int a3, int a4){ // HACK nie ma to sensu na tę chwilę
    
    int liczba_miejsc = a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4;

    return(liczba_miejsc);
}

int main(int argc, char * argv[] ){
    
    
    printf("Kasjer zaczyna prace! (kasjer) \n");

    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);

    printf("Witam, mam %d startowych miejsc w pizzerii! [Kasjer] \n", liczba_miejsc(a1, a2, a3, a4));

    int max_klient = liczba_miejsc(a1, a2, a3, a4);


    key_t msgq1_key = ftok(".", 'M');
    if (msgq1_key < 0) {
        perror("ERROR ftok [kasjer] \n");
        exit(1);
    }

    int msgq1_id = msgget(msgq1_key, 0600 | IPC_CREAT | IPC_EXCL);
    if (msgq1_id < 0) {
        perror("ERROR msgget [kasjer]! \n");
        exit(2);
    }


    send_message(msgq1_id, "Zapraszam do zamawiania! \n");


    printf("Czekam na klientów...\n");
    sleep(5); // Poczekam na klientów

    receive_message(msgq1_id);

    // Usunięcie kolejki
    if (msgctl(msgq1_id, IPC_RMID, 0) < 0) {
        perror("msgctl");
        exit(1);
    }

    return 0;
}