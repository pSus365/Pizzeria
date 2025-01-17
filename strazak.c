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


int main(int argc, char * argv[]){

    printf("Strazak zaczyna prace! (strazak) \n");
    
    if(argc != 4){
        fprintf(stderr, "Strazak odebral nieprawidlowa liczbe argumentow! [argc != 4] \n");
        exit(6);
    }

    pid_t main_pid = atoi(argv[1]);
    pid_t kasjer_pid = atoi(argv[2]);

    int liczba_stolikow = atoi(argv[3]);

    printf("Nadzoruje system o ID: %d \n", main_pid);
    printf("Opiejkuje sie kasjerem o ID: %d \n", kasjer_pid);
    printf("Mam pod opieka %d stolikow! \n", liczba_stolikow);


    return 0;
}