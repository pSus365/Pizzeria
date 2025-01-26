#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#define RESET   "\033[0m"
#define GREEN   "\033[32m"

// Komunikat
struct msgbuf {
    long type;
    char mtext[256];
};

const char* menu_items[] = {
    "Margherita",
    "Pepperoni",
    "Hawaiian",
    "Veggie",
    "BBQ Chicken",
    "Four Cheese"
};
const int menu_size=6;

// Wysyłanie (z natychmiastowym sprawdzeniem EIDRM/EINVAL)
int send_message(int mqid, long type, const char* text){
    struct msgbuf buffer;
    buffer.type=type;
    strncpy(buffer.mtext, text, sizeof(buffer.mtext)-1);
    buffer.mtext[sizeof(buffer.mtext)-1]='\0';

    if(msgsnd(mqid,&buffer,strlen(buffer.mtext)+1,0)==-1){
        if(errno==EIDRM || errno==EINVAL){
            printf(GREEN "[KLIENT] Kolejka zamknięta - kończę.\n" RESET);
            return -1;
        }
        perror(GREEN "msgsnd [klient]" RESET);
        return -1;
    }
    return 0;
}

// Odbieranie nieblokujące z ograniczoną liczbą prób
int receive_message_nb(int mqid, long type, char* out, size_t max_len, int max_tries){
    int tries=0;
    while(tries<max_tries){
        struct msgbuf buf;
        ssize_t r = msgrcv(mqid,&buf,sizeof(buf.mtext),type,IPC_NOWAIT);
        if(r==-1){
            if(errno==EAGAIN || errno==ENOMSG){
                // Brak wiadomości
                usleep(2000000); // 2 s
                tries++;
                continue;
            }
            if(errno==EIDRM || errno==EINVAL){
                printf(GREEN "[KLIENT] Kolejka zamknięta - kończę.\n" RESET);
                return -1;
            }
            if(errno==EINTR){
                continue;
            }
            perror(GREEN "msgrcv [klient]" RESET);
            return -1;
        }
        // Sukces
        strncpy(out, buf.mtext, max_len);
        out[max_len-1]='\0';
        return 0;
    }
    // Po wyczerpaniu prób
    printf(GREEN "[KLIENT] Nie doczekano się odpowiedzi. Kończę.\n" RESET);
    return -1;
}

int main(int argc, char* argv[]){
    setbuf(stdout, NULL);

    if(argc!=2){
        fprintf(stderr, GREEN "Użycie: %s <liczba_osob>\n" RESET, argv[0]);
        exit(EXIT_FAILURE);
    }
    int group_size=atoi(argv[1]);
    if(group_size<1||group_size>4){
        fprintf(stderr, GREEN "Grupa musi mieć 1..4 osób.\n" RESET);
        exit(EXIT_FAILURE);
    }

    printf(GREEN "[KLIENT %d] Liczba osób w grupie: %d\n" RESET, getpid(), group_size);

    key_t mqkey = ftok(".", 'M');
    if(mqkey<0){
        perror(GREEN "ftok [klient]" RESET);
        exit(EXIT_FAILURE);
    }
    int mqid = msgget(mqkey, 0600);
    if(mqid<0){
        perror(GREEN "msgget [klient]" RESET);
        exit(EXIT_FAILURE);
    }

    // REQUEST stolika
    char request[256];
    snprintf(request,sizeof(request),"REQUEST:%d:%d",group_size,getpid());
    if(send_message(mqid,1,request)==-1){
        // Błąd lub kolejka zamknięta
        exit(EXIT_FAILURE);
    }
    printf(GREEN "[KLIENT %d] Wysłano żądanie stolika.\n" RESET, getpid());

    // Odbieramy odpowiedź nieblokująco (max_tries = 20 => 2 sek)
    char response[256];
    if(receive_message_nb(mqid, getpid(), response, sizeof(response), 20)==-1){
        exit(EXIT_FAILURE);
    }
    printf(GREEN "[KLIENT %d] Odpowiedź: %s\n" RESET, getpid(), response);

    if(strncmp(response,"REJECTED",8)==0){
        printf(GREEN "[KLIENT %d] Stolik odrzucony, kończę.\n" RESET, getpid());
        exit(EXIT_SUCCESS);
    }

    // Złożenie zamówienia
    char pizzas[128]="";
    for(int i=0;i<group_size;i++){
        if(i>0) strcat(pizzas,",");
        int idx = rand()%menu_size;
        strcat(pizzas, menu_items[idx]);
    }
    char order[256];
    snprintf(order,sizeof(order),"ORDER:%d:%d:%s",group_size,getpid(),pizzas);

    if(send_message(mqid,1,order)==-1){
        exit(EXIT_FAILURE);
    }
    printf(GREEN "[KLIENT %d] Wysłano zamówienie: %s\n" RESET, getpid(), order);

    // Odbieramy odpowiedź do zamówienia
    if(receive_message_nb(mqid, getpid(), response, sizeof(response), 20)==-1){
        exit(EXIT_FAILURE);
    }

    if(strncmp(response,"ORDER CONFIRMED",15)==0){
        printf(GREEN "[KLIENT %d] Otrzymano potwierdzenie: %s\n" RESET, getpid(), response);
        // Jedzenie
        int eat_time = rand()%5 +3; 
        printf(GREEN "[KLIENT %d] Grupa %d osób je pizzę przez %d sekund.\n" RESET,
               getpid(), group_size, eat_time);
        sleep(eat_time);

        // FINISH
        char finish_msg[256];
        snprintf(finish_msg,sizeof(finish_msg),"FINISH:%d:%d",group_size,getpid());
        if(send_message(mqid,1,finish_msg)==-1){
            exit(EXIT_FAILURE);
        }
        printf(GREEN "[KLIENT %d] Kończymy jedzenie, wysłano FINISH.\n" RESET, getpid());
    }
    else if(strncmp(response,"ORDER REJECTED",14)==0){
        printf(GREEN "[KLIENT %d] Zamówienie odrzucone (closing).\n" RESET, getpid());
    }
    else {
        printf(GREEN "[KLIENT %d] Nieznana odpowiedź: %s\n" RESET, getpid(), response);
    }

    return 0;
}
