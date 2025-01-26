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

#define RESET   "\033[0m"
#define BLUE    "\033[34m"
#define CYAN    "\033[33m"
#define RED     "\033[41m"


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


    int time_fire = rand() % 600;
    sleep(time_fire);

    printf(RED "Strazak: POZAR!!!\n");

    if(kill(kasjer_pid, SIGINT) == -1){
        perror("ERROR SIGINT kasjer");
    }

    if(kill(main_pid, SIGINT) == -1){
        perror("ERROR SIGINT main");
    }







    return 0;
}