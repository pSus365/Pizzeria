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

#define MAX_ACTIVE_CLIENTS

//argumenty wywołania programu - liczba stolików
int arg_check(int argc, char * argv[]){
    if(argc != 5){
        fprintf(stderr, "Podano nieodpowiednia liczbe argumentow! [argc != 5] \n");
        exit(3);
    }

    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);

    int liczba_stolikow = 0;

    if(a1 < 0 || a2 < 0 || a3 < 0 || a4 < 0)
    {
        fprintf(stderr, "Nie mozna podawac ujemnych liczb stolikow!\n");
        exit(4);
    }

    liczba_stolikow = a1 + a2 + a3 + a4;

    if(liczba_stolikow < 1){
        fprintf(stderr, "W restauracji musi byc co najmniej jeden stolik! \n");
        exit(5);
    }
    printf("Poprwana ilosc stolikow! \n");

    return(liczba_stolikow); 
}

int liczba_miejsc(char * argv[]){ 

    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);
    
    int liczba_miejsc = a1 * 1 + a2 * 2 + a3 * 3 + a4 * 4;

    return(liczba_miejsc);
}


int main(int argc, char* argv[])
{
    srand(time(NULL));
    int liczba_stolikow = arg_check(argc, argv);
    int max_klient = liczba_miejsc(argv);
    int main_id = getpid();
    
    // tworzę kasjera
    pid_t kasjer_id = fork();
    switch(kasjer_id){
        case -1:
            perror("ERROR przy tworzeniu kasjera! (main)");
            exit(1);
        case 0:
            execl("./kasjer", "kasjer",argv[1], argv[2], argv[3], argv[4], NULL);
    } 

    sleep(2);

    // tworze stazaka
    pid_t strazak_id = fork();  //TODO przyjmowanie pidu kasjera maina i liczba stolików do oproznienia 
    switch(strazak_id){
        case -1:
            perror("ERROR przy tworzeniu strazaka!");
            exit(2);
        case 0:{

            char str_main_id[20];
            char str_kasjer_id[20];
            char str_liczba_stolikow[20];

            sprintf(str_main_id, "%d", main_id);
            sprintf(str_kasjer_id, "%d", kasjer_id);
            sprintf(str_liczba_stolikow, "%d", liczba_stolikow);
            
            execl("./strazak", "strazak", str_main_id, str_kasjer_id, str_liczba_stolikow, (char *)NULL);

            perror("ERROR przy wywolaniu strazaka! (main prog -> execl) \n");
            exit(7);
        }

        default: 
            break;
    }

    printf("PID programu glownego [main prog]: %d \n", main_id);
    printf("PID kasjera [main prog]: %d \n", kasjer_id);
    printf("PID strazaka [main prog]: %d \n", strazak_id);



    //TODO generowanie grup klientow 
    //TODO działanie w pętli generowania grup klientow przez okreslony czas działania programu (interwałowo chce generowac grupy)


    // TODO generowanie czasu pracy pizzerii
    // TODO maksymalna liczba klientow
    // TODO co ile generuje grupe klientow


    //czas pracy pizzerii
    int czas_dzialania_pizzerii = rand() % 61 + 30; // czas w zakresie od 30 do 90 sekund
    time_t czas_aktualny = time(NULL);

    while((time(NULL) - czas_aktualny) < czas_dzialania_pizzerii){
        
        char str_grupa_klientow[20];
        int lo = rand() % 3 + 1; // generuje liczbe osob w grupie
        sprintf(str_grupa_klientow, "%d", lo);
        
        //FIXME odebranie przez klienta grupy do utworzenia wątkow
    

        pid_t klient_pr_id = fork();
        //FIXME generuj tylko jezeli jest mniej klientow niz max mozliwych
        switch(klient_pr_id){
            case -1:
                perror("ERROR przy wywolaniu procesu klienta! (main prog -> execl) \n");
                exit(9);
            case 0:{
                execl("./klient", "klient", str_grupa_klientow, (char *)NULL); //FIXME group size ma byc a nie liczba grup!!!!!!!
                perror("ERROR przy wywolaniu procesu klienta! [main prog]\n");
                exit(10);
            }
            default:
                break;
        }
         sleep(3);
    }

    printf("Koniec czasu pizzerii! [main prog]\n");


    //TODO Wysylamy komunikat mtype=99 do kasjera, by się zamknął
    //TODO Czekamy na zakończenie procesu kasjera (wait)



     for(int i=0; i<2; i++){ // FIXME dwa procesy poki co
        wait(NULL); 
    }
   





    
}