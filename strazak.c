#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

#define RESET "\033[0m"
#define RED   "\033[31m"

static pid_t main_pid;
static pid_t kasjer_pid;
static int stoliki;

void sigint_handler(int sig){
    if(sig==SIGINT){
        printf(RED "[STRAZAK] Odebrano SIGINT, kończę działanie strażaka.\n" RESET);
        exit(0);
    }
}

int main(int argc, char* argv[]){
    setbuf(stdout, NULL);

    if(argc!=4){
        fprintf(stderr, RED "Użycie: %s <pid_main> <pid_kasjer> <liczba_stolikow>\n" RESET, argv[0]);
        exit(1);
    }

    main_pid = atoi(argv[1]);
    kasjer_pid= atoi(argv[2]);
    stoliki = atoi(argv[3]);

    srand(time(NULL));

    printf(RED "[STRAZAK] Start. main=%d, kasjer=%d, stoliki=%d\n" RESET,
           main_pid, kasjer_pid, stoliki);

    signal(SIGINT, sigint_handler);

    int t = rand()%11+5; // 5..15 s
    printf(RED "[STRAZAK] Pożar nastąpi za %d sekund...\n" RESET, t);

    //sleep(t);

    printf(RED "[STRAZAK] WYSTĄPIŁ POŻAR! Zamykam pizzerię...\n" RESET);
    kill(kasjer_pid, SIGINT);
    kill(main_pid, SIGINT);

    printf(RED "[STRAZAK] Kończę działanie strażaka.\n" RESET);
    return 0;
}
