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
#include <errno.h>

#define RESET   "\033[0m"
#define CYAN    "\033[33m"

// Funkcja sprawdzająca argumenty (liczba stolików)
int arg_check(int argc, char * argv[]){
    if(argc != 5){
        fprintf(stderr, CYAN "Podano nieodpowiednia liczbe argumentow! [argc != 5]\n" RESET);
        exit(3);
    }

    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);

    if(a1 < 0 || a2 < 0 || a3 < 0 || a4 < 0){
        fprintf(stderr, CYAN "Nie mozna podawac ujemnych liczb stolikow!\n" RESET);
        exit(4);
    }

    int liczba_stolikow = a1 + a2 + a3 + a4;
    if(liczba_stolikow < 1){
        fprintf(stderr, CYAN "W restauracji musi byc co najmniej jeden stolik!\n" RESET);
        exit(5);
    }
    printf(CYAN "Poprawna ilosc stolikow!\n" RESET);
    return liczba_stolikow; 
}

// Funkcja do obliczania liczby miejsc
int liczba_miejsc(char * argv[]){
    int a1 = atoi(argv[1]);
    int a2 = atoi(argv[2]);
    int a3 = atoi(argv[3]);
    int a4 = atoi(argv[4]);
    return (a1*1 + a2*2 + a3*3 + a4*4);
}

int main(int argc, char* argv[])
{
    // Wyłączenie buforowania
    setbuf(stdout, NULL);

    srand(time(NULL));

    // Sprawdzenie argumentów i obliczenie stolików
    int liczba_stolikow = arg_check(argc, argv);
    int max_klient = liczba_miejsc(argv);
    int main_id = getpid();

    // Losowy czas pizzerii 30..90 s
    int czas_dzialania_pizzerii = rand() % 61 + 30;
    time_t czas_aktualny = time(NULL);

    printf(CYAN "Czas dzialania pizzerii: %d sekund\n" RESET, czas_dzialania_pizzerii);

    // Uruchamiamy kasjera
    pid_t kasjer_id = fork();
    if(kasjer_id == -1){
        perror("ERROR przy tworzeniu kasjera (main)");
        exit(8);
    }
    if(kasjer_id == 0){
        char str_czas[12];
        sprintf(str_czas, "%d", czas_dzialania_pizzerii);
        execl("./kasjer", "kasjer", argv[1], argv[2], argv[3], argv[4],
              str_czas, NULL);
        perror("ERROR execl [kasjer]");
        exit(6);
    }

    // Krótka przerwa, by kasjer się uruchomił
    sleep(2);

    // Uruchamiamy strażaka
    pid_t strazak_id = fork();
    if(strazak_id == -1){
        perror("ERROR przy tworzeniu strazaka!");
        exit(2);
    }
    if(strazak_id == 0){
        // dziecko -> strażak
        char str_main[20], str_kasjer[20], str_stolikow[20];
        sprintf(str_main, "%d", main_id);
        sprintf(str_kasjer, "%d", kasjer_id);
        sprintf(str_stolikow, "%d", liczba_stolikow);

        execl("./strazak", "strazak", str_main, str_kasjer, str_stolikow, NULL);
        perror("ERROR execl [strazak]");
        exit(7);
    }

    printf(CYAN "PID programu glownego [main]: %d\n" RESET, main_id);
    printf(CYAN "PID kasjera [main]: %d\n" RESET, kasjer_id);
    printf(CYAN "PID strazaka [main]: %d\n" RESET, strazak_id);

    // Generowanie klientów do upływu czasu pizzerii
    while((time(NULL) - czas_aktualny) < czas_dzialania_pizzerii){
        char str_liczba_osob[12];
        int liczba_osob = rand() % 4 + 1; // 1..4
        sprintf(str_liczba_osob, "%d", liczba_osob);

        pid_t klient_pid = fork();
        if(klient_pid == -1){
            perror("ERROR przy fork [klient]");
            exit(9);
        }
        if(klient_pid == 0){
            execl("./klient", "klient", str_liczba_osob, NULL);
            perror("ERROR execl [klient]");
            exit(10);
        }
        //sleep(3);
    }

    printf(CYAN "Koniec czasu pizzerii! [main]\n" RESET);

    // Zabijamy kasjera i strażaka, jeśli żyją
    kill(kasjer_id, SIGINT);
    kill(strazak_id, SIGINT);

    // Czekamy na wszystkie procesy potomne (kasjera, strażaka i klientów)
    while(wait(NULL) > 0);

    printf(CYAN "Wszystkie procesy potomne zostaly zakonczone. [main]\n" RESET);
    return 0;
}
