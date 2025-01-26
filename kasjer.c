#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#define RESET   "\033[0m"
#define BLUE    "\033[34m"

struct msgbuf {
    long type;
    char mtext[256];
};

// Menu pizzerii
struct menu_item {
    char name[50];
    double price;
} menu_items[] = {
    {"Margherita", 20.0},
    {"Pepperoni", 25.0},
    {"Hawaiian", 22.5},
    {"Veggie", 18.0},
    {"BBQ Chicken", 27.0},
    {"Four Cheese", 24.0}
};
const int menu_size = sizeof(menu_items)/sizeof(menu_items[0]);

// Kasjer
struct kasjer_info {
    int tables[4];         
    int initial_tables[4];
    double revenue;        
    int total_orders;      
    int tables_assigned[4];
    pthread_mutex_t lock;  
    int accept_new_orders; 
    int stop;              
} kasjer;

volatile sig_atomic_t global_stop = 0;

void handle_signal(int sig) {
    if (sig == SIGINT) {
        printf(BLUE "\n[KASJER] Odebrano sygnał SIGINT. Kończenie pracy kasjera...\n" RESET);
        global_stop = 1;
    }
}

void init_kasjer(struct kasjer_info* k, int a1, int a2, int a3, int a4){
    k->tables[0] = a1;  
    k->tables[1] = a2;
    k->tables[2] = a3;
    k->tables[3] = a4;

    k->initial_tables[0] = a1;
    k->initial_tables[1] = a2;
    k->initial_tables[2] = a3;
    k->initial_tables[3] = a4;

    k->revenue = 0.0;
    k->total_orders = 0;

    for(int i=0; i<4; i++){
        k->tables_assigned[i] = 0;
    }
    k->accept_new_orders=1;
    k->stop=0;

    if(pthread_mutex_init(&k->lock, NULL)!=0){
        perror("pthread_mutex_init [kasjer]");
        exit(EXIT_FAILURE);
    }
}

void send_message(int mqid, long type, const char* msg){
    struct msgbuf buf;
    buf.type = type;
    strncpy(buf.mtext, msg, sizeof(buf.mtext)-1);
    buf.mtext[sizeof(buf.mtext)-1] = '\0';

    if(msgsnd(mqid, &buf, strlen(buf.mtext)+1, 0)==-1){
        perror(BLUE "msgsnd [kasjer]" RESET);
    }
}

void generate_report(struct kasjer_info* k){
    printf(BLUE "\n===== Raport Działania Pizzerii =====\n" RESET);
    printf(BLUE "Całkowity utarg: %.2f PLN\n" RESET, k->revenue);
    printf(BLUE "Łączna liczba zamówień: %d\n" RESET, k->total_orders);
    printf(BLUE "Przydzielone stoliki (aktualnie zajęte):\n" RESET);
    for(int i=0; i<4; i++){
        printf(BLUE "  %d-osobowe: %d\n" RESET, i+1, k->tables_assigned[i]);
    }
    printf(BLUE "=====================================\n" RESET);

    FILE* f = fopen("report.txt","a");
    if(!f){
        perror(BLUE "fopen [kasjer]" RESET);
        return;
    }
    fprintf(f, "\n===== Raport Działania Pizzerii =====\n");
    fprintf(f, "Całkowity utarg: %.2f PLN\n", k->revenue);
    fprintf(f, "Łączna liczba zamówień: %d\n", k->total_orders);
    fprintf(f, "Przydzielone stoliki (aktualnie zajęte):\n");
    for(int i=0; i<4; i++){
        fprintf(f, "  %d-osobowe: %d\n", i+1, k->tables_assigned[i]);
    }
    fprintf(f, "=====================================\n");
    fclose(f);
}

void release_semaphore(int semid){
    struct sembuf sb;
    sb.sem_num=0;
    sb.sem_op=1; 
    sb.sem_flg=0;
    if(semop(semid, &sb, 1)==-1){
        perror(BLUE "semop release [kasjer]" RESET);
    }
}

int init_semaphore(int val){
    key_t sem_key = ftok(".", 'S');
    if(sem_key<0){
        perror(BLUE "ftok [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }
    int semid = semget(sem_key,1,IPC_CREAT|IPC_EXCL|0666);
    if(semid<0){
        perror(BLUE "semget [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }
    if(semctl(semid,0,SETVAL, val)==-1){
        perror(BLUE "semctl SETVAL [kasjer]" RESET);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }
    return semid;
}

// Odrzuca wszystkie REQUEST/ORDER zalegające w kolejce, FINISH pozostawia
void reject_all_waiting_requests_and_orders(int msgq1_id){
    struct msgbuf buffer;
    while(1){
        if(msgrcv(msgq1_id,&buffer,sizeof(buffer.mtext),0,IPC_NOWAIT)<0){
            if(errno==EAGAIN || errno==ENOMSG){
                break;
            }
            if(errno==EINTR){
                continue;
            }
            break;
        }
        if(strncmp(buffer.mtext, "REQUEST:", 8)==0 ||
           strncmp(buffer.mtext, "ORDER:", 6)==0) {
            // Odrzucamy
            char rej[256]="REJECTED: No longer accepting requests or orders.";
            // Znajdź pid w polu
            char tmp[256];
            strcpy(tmp, buffer.mtext);
            char* t = strtok(tmp, ":"); 
            t = strtok(NULL, ":"); // group_size
            t = strtok(NULL, ":"); // pid
            pid_t client_pid=1; 
            if(t) client_pid=atoi(t);

            send_message(msgq1_id, client_pid, rej);
        }
        else if(strncmp(buffer.mtext,"FINISH:",7)==0){
            // FINISH wrzucamy z powrotem, bo chcemy normalnie zwolnić stolik
            msgsnd(msgq1_id,&buffer,strlen(buffer.mtext)+1, IPC_NOWAIT);
        }
        else {
            // Nieznany typ - ignorujemy
        }
    }
}

// Wątek do obsługi komunikatów
void* handle_messages(void* arg){
    struct kasjer_info* k=(struct kasjer_info*)arg;

    key_t msgq1_key = ftok(".", 'M');
    if(msgq1_key<0){
        perror(BLUE "ftok [kasjer]" RESET);
        pthread_exit(NULL);
    }
    int msgq1_id = msgget(msgq1_key, 0600);
    if(msgq1_id<0){
        perror(BLUE "msgget [kasjer]" RESET);
        pthread_exit(NULL);
    }

    key_t sem_key = ftok(".", 'S');
    if(sem_key<0){
        perror(BLUE "ftok [kasjer]" RESET);
        pthread_exit(NULL);
    }
    int semid = semget(sem_key,1,0);
    if(semid<0){
        perror(BLUE "semget [kasjer]" RESET);
        pthread_exit(NULL);
    }

    while(!k->stop && !global_stop){
        struct msgbuf buf;
        // Odczyt nieblokujący
        if(msgrcv(msgq1_id, &buf, sizeof(buf.mtext), 0, IPC_NOWAIT)<0){
            if(errno==EAGAIN || errno==ENOMSG){
                usleep(100000);
                continue;
            }
            if(errno==EINTR){
                continue;
            }
            perror(BLUE "msgrcv [kasjer]" RESET);
            continue;
        }

        // Analiza
        if(strncmp(buf.mtext, "REQUEST:",8)==0){
            int gsize; pid_t cpid;
            sscanf(buf.mtext, "REQUEST:%d:%d",&gsize,&cpid);

            printf(BLUE "[KASJER] Żądanie stolika dla %d (PID %d)\n" RESET, gsize, cpid);

            char reply[256];
            pthread_mutex_lock(&k->lock);
            if(!k->accept_new_orders){
                snprintf(reply,sizeof(reply),"REJECTED: Pizzeria is closing or no longer accepting orders.");
            }
            else if(gsize<1||gsize>4){
                snprintf(reply,sizeof(reply),"REJECTED: Invalid group size %d.",gsize);
            }
            else {
                if(k->tables[gsize-1]>0){
                    k->tables[gsize-1]--;
                    k->tables_assigned[gsize-1]++;
                    snprintf(reply,sizeof(reply),"CONFIRMED: Assigned to %d-person table.", gsize);
                } else {
                    snprintf(reply,sizeof(reply),"REJECTED: No available table for %d persons.", gsize);
                }
            }
            pthread_mutex_unlock(&k->lock);
            send_message(msgq1_id, cpid, reply);
            printf(BLUE "[KASJER] Odpowiedź do PID %d: %s\n" RESET, cpid, reply);

        } else if(strncmp(buf.mtext, "ORDER:",6)==0){
            int gsize; pid_t cpid;
            char items_str[256];
            sscanf(buf.mtext,"ORDER:%d:%d:%255[^\n]",&gsize,&cpid,items_str);

            printf(BLUE "[KASJER] Zamówienie od PID %d (grupa %d): %s\n" RESET,
                   cpid, gsize, items_str);

            pthread_mutex_lock(&k->lock);
            if(!k->accept_new_orders){
                pthread_mutex_unlock(&k->lock);
                char ord_reply[256];
                snprintf(ord_reply,sizeof(ord_reply),
                         "ORDER REJECTED: Pizzeria is closing or no longer accepting orders.");
                send_message(msgq1_id, cpid, ord_reply);
                printf(BLUE "[KASJER] Odrzucono zamówienie PID %d (closing)\n" RESET, cpid);
                continue;
            }
            pthread_mutex_unlock(&k->lock);

            // Parsowanie zamówienia
            double total=0.0;
            char* token = strtok(items_str,",");
            while(token){
                for(int i=0;i<menu_size;i++){
                    if(strcmp(token, menu_items[i].name)==0){
                        total+=menu_items[i].price;
                        break;
                    }
                }
                token=strtok(NULL,",");
            }

            pthread_mutex_lock(&k->lock);
            k->revenue+=total;
            k->total_orders++;
            pthread_mutex_unlock(&k->lock);

            //sleep(2); // przygotowanie

            char ord_reply[256];
            snprintf(ord_reply,sizeof(ord_reply),
                     "ORDER CONFIRMED: Your order totaling %.2f PLN is ready.",total);
            send_message(msgq1_id, cpid, ord_reply);
            printf(BLUE "[KASJER] Potwierdzono zamówienie PID %d: %.2f PLN\n" RESET,
                   cpid, total);

        } else if(strncmp(buf.mtext,"FINISH:",7)==0){
            int gsize; pid_t cpid;
            sscanf(buf.mtext,"FINISH:%d:%d",&gsize,&cpid);

            printf(BLUE "[KASJER] FINISH od PID %d (grupa %d)\n" RESET, cpid, gsize);
            if(gsize>=1 && gsize<=4){
                pthread_mutex_lock(&k->lock);
                k->tables[gsize-1]++;
                k->tables_assigned[gsize-1]--;
                pthread_mutex_unlock(&k->lock);
                printf(BLUE "[KASJER] Zwolniono stolik %d-os.\n" RESET, gsize);
            }
            release_semaphore(semid);
        } else {
            printf(BLUE "[KASJER] Otrzymano nieznany komunikat: %s\n" RESET, buf.mtext);
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]){
    setbuf(stdout, NULL);

    if(argc!=6){
        fprintf(stderr, BLUE "Użycie: %s <t1> <t2> <t3> <t4> <czas>\n" RESET, argv[0]);
        exit(EXIT_FAILURE);
    }

    int a1=atoi(argv[1]), a2=atoi(argv[2]), a3=atoi(argv[3]), a4=atoi(argv[4]);
    int czas=atoi(argv[5]);

    if(a1<0||a2<0||a3<0||a4<0||czas<=0){
        fprintf(stderr, BLUE "Niepoprawne argumenty!\n" RESET);
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags=0;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT,&sa,NULL)==-1){
        perror(BLUE "sigaction [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    printf(BLUE "[KASJER] Start pracy. 1-os(%d),2-os(%d),3-os(%d),4-os(%d), czas=%d\n" RESET,
           a1,a2,a3,a4,czas);

    init_kasjer(&kasjer,a1,a2,a3,a4);

    key_t msgq1_key = ftok(".", 'M');
    if(msgq1_key<0){
        perror(BLUE "ftok [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }
    int msgq1_id = msgget(msgq1_key, 0600|IPC_CREAT|IPC_EXCL);
    if(msgq1_id<0){
        perror(BLUE "msgget [kasjer]" RESET);
        exit(EXIT_FAILURE);
    }

    // Wiadomość powitalna
    send_message(msgq1_id, 1, "Zapraszam do zamawiania!");
    printf(BLUE "[KASJER] Kolejka utworzona, msg powitalna wysłana.\n" RESET);

    // Semafor
    int total_seats = a1*1 + a2*2 + a3*3 + a4*4;
    int semid = init_semaphore(total_seats);
    printf(BLUE "[KASJER] Semafor z wartością %d.\n" RESET, total_seats);

    // Wątek obsługi
    pthread_t th;
    if(pthread_create(&th,NULL,handle_messages,&kasjer)!=0){
        perror(BLUE "pthread_create [kasjer]" RESET);
        msgctl(msgq1_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    // Kasjer pracuje: w ostatnich 5 sek nie przyjmuje
    int time_to_close=5;
    if(czas<time_to_close) time_to_close=czas;
    int normal_phase= czas - time_to_close;
    if(normal_phase<0) normal_phase=0;

    // Faza normalna
    for(int i=0; i<normal_phase; i++){
        if(global_stop) break;
        //sleep(1);
    }

    // Wyłączamy przyjmowanie nowych zamówień
    pthread_mutex_lock(&kasjer.lock);
    kasjer.accept_new_orders=0;
    pthread_mutex_unlock(&kasjer.lock);

    printf(BLUE "[KASJER] Koniec przyjmowania nowych zamówień (ostatnie 5 s).\n" RESET);

    // Odrzucamy wszystkie REQUEST/ORDER w kolejce
    reject_all_waiting_requests_and_orders(msgq1_id);

    for(int i=0; i<time_to_close; i++){
        if(global_stop) break;
        //sleep(1);
    }

    // Kończymy kasjera
    pthread_mutex_lock(&kasjer.lock);
    kasjer.stop=1;
    pthread_mutex_unlock(&kasjer.lock);

    printf(BLUE "[KASJER] Koniec czasu pizzerii.\n" RESET);

    // Czekamy 2 sekundy, by komunikaty zdążyły się wypisać
    //sleep(2);

    pthread_join(th,NULL);
    pthread_mutex_destroy(&kasjer.lock);

    // Usuwanie kolejki
    if(msgctl(msgq1_id, IPC_RMID,0)==-1){
        perror(BLUE "msgctl [kasjer]" RESET);
    }

    // Usuwanie semafora
    if(semctl(semid,0,IPC_RMID)==-1){
        perror(BLUE "semctl [kasjer]" RESET);
    }

    // Raport
    generate_report(&kasjer);

    printf(BLUE "[KASJER] Zakończono działanie.\n" RESET);
    return 0;
}
