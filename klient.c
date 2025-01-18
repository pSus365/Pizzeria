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

int main(int lo){
    int g_lo = lo; // generate liczba grup klientow      group size ma byc!!!!!

    printf("Jestem procesem generujacym watki klientow! Mam wygenerowac %d osob! \n", g_lo); // TODO generowanie wątków
}