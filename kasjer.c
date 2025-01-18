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

    key_t request_key; 
    if((request_key = ftok(".", "REQ_KEY")) == -1){
        perror("ERROR przy generowaniu klucza REQUEST_KEY [kasjer] \n");
        exit(8);
    }

    key_t response_key;
    if((request_key = ftok(".", "RES_KEY")) == -1){
        perror("ERROR przy generowaniu klucza RESPONSE_KEY [kasjer] \n");
        exit(9);
    }

    int requestQ = msgget((key_t)request_key, IPC_CREAT | 0666);  // kolejka do odbierania zamowienia od klientów
    if(requestQ == -1) {
        perror("Blad tworzenia requestQ");
        return 1;
    }

    int responseQ = msgget((key_t)response_key, IPC_CREAT | 0666); // kolejka do podliczenia ceny dla klientow
    if(responseQ == -1) {
        perror("Blad tworzenia responseQ");
        
        msgctl(requestQ, IPC_RMID, NULL); // czyszczenie requestQ
        return 1;
    }



    //Usuwam kolejki
    if(msgctl(requestQ, IPC_RMID, NULL) == -1) {
        perror("[main] Blad usuwania requestQ");
    }
    if(msgctl(responseQ, IPC_RMID, NULL) == -1) {
        perror("[main] Blad usuwania responseQ");
    }

    printf("Koniec pracy! [Kasjer] \n");
    return 0;


    return 0;
}