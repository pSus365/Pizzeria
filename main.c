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

#define MAX_ACTIVE_CLIENTS 100  // Definicja z wartością lub usuń jeśli nie jest potrzebna

// Funkcja do sprawdzania argumentów wywołania programu - liczba stolików
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
    printf("Poprawna ilosc stolikow! \n");

    return(liczba_stolikow); 
}

// Funkcja do obliczania liczby miejsc w restauracji
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
    //for(int x= 0; x < 1000; x++) {printf("x: %d ", rand());}
    
    int liczba_stolikow = arg_check(argc, argv);
    int max_klient = liczba_miejsc(argv);
    int main_id = getpid();
    
    // Tworzenie procesu kasjera
    pid_t kasjer_id = fork();
    switch(kasjer_id){
        case -1:
            perror("ERROR przy tworzeniu kasjera! (main)");
            exit(1);
        case 0:
            // Przekazujemy dodatkowy argument: czas_dzialania_pizzerii
            // Ten argument będzie dodany później
            // Dla teraz, przekazujemy 0 jako placeholder
            execl("./kasjer", "kasjer", argv[1], argv[2], argv[3], argv[4], NULL);
            perror("ERROR execl [kasjer]");
            exit(6);
    } 

    sleep(2);

    // Tworzenie procesu strażaka
    pid_t strazak_id = fork();  //TODO przyjmowanie pidu kasjera maina i liczba stolikow do opróżnienia 
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



    // Generowanie czasu działania pizzerii
    int czas_dzialania_pizzerii = rand() % 61 + 30; // czas w zakresie od 30 do 90 sekund
    time_t czas_aktualny = time(NULL);
    printf("Czas dzialania pizzerii: %d sekund\n", czas_dzialania_pizzerii);

    // Tworzenie nowego procesu kasjera z przekazanym czasem działania
    pid_t nowy_kasjer_id = fork();
    if(nowy_kasjer_id == -1){
        perror("ERROR przy tworzeniu nowego kasjera! (main)");
        exit(8);
    }
    if(nowy_kasjer_id == 0){
        // Przekazanie czasu działania pizzerii jako dodatkowego argumentu
        char str_czas_dzialania_pizzerii[12];
        sprintf(str_czas_dzialania_pizzerii, "%d", czas_dzialania_pizzerii);

        execl("./kasjer", "kasjer", argv[1], argv[2], argv[3], argv[4], str_czas_dzialania_pizzerii, NULL);
        perror("ERROR execl [nowy kasjer]");
        exit(6);
    }


    // Generowanie grup klientów w pętli
    while((time(NULL) - czas_aktualny) < czas_dzialania_pizzerii){
        
        char str_liczba_osob[12];
        int liczba_osob = rand() % 4 + 1; // generuje liczbe osob w grupie
        
        sprintf(str_liczba_osob, "%d", liczba_osob);
        //int test_pid = getpid();
        //printf("LICZBA OSOB: %d, PID: %d \n", liczba_osob, test_pid);
        
        //FIXME odebranie przez klienta grupy do utworzenia wątkow
        
        pid_t klient_pr_id = fork();
        //FIXME generuj tylko jezeli jest mniej klientow niz max mozliwych
        switch(klient_pr_id){
            case -1:
                perror("ERROR przy wywolaniu procesu klienta! (main prog -> execl) \n");
                exit(9);
            case 0:{
                execl("./klient", "klient", str_liczba_osob, (char *)NULL); //FIXME group size ma byc a nie liczba grup!!!!!!!
                perror("ERROR przy wywolaniu procesu klienta! [main prog]\n");
                exit(10);
            }
            default:
                break;
        }
         sleep(3);
    }

    printf("Koniec czasu pizzerii! [main prog]\n");

    // Czekanie na zakończenie wszystkich procesów potomnych
    while(wait(NULL) > 0);

    printf("Wszystkie procesy potomne zostaly zakonczone. [main prog]\n");

    return 0;
}
