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



    return 0;
}