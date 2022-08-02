#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <pthread.h>

#define GRN   "\x1B[32m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

#define MSG_SIZE 100
#define KEY_FILE_PATH  "panel"
#define ID 'A'

int msg_id;
sem_t state_read;

int floor_level = -1;
int call_pressed = 0;
int lamp_state = 0; // 0: off, 1: arrival

typedef struct msg_buffer {
    long msg_type;
    char msg_text[MSG_SIZE];
} MsgBuffer;

void clearScreen() {
    printf("%c[2J%c[;H",(char) 27, (char) 27);
}

void draw_panel() {
    if (lamp_state == 0)
        printf("(" WHT "O" RESET ")\n"); // off - white
    else
        printf("(" GRN "O" RESET ")\n"); // arrival - green

    printf("---\n");

    if (call_pressed == 0)
        printf(WHT "call" RESET "\n");
    else
        printf(GRN "call" RESET "\n");
}

void* draw_ui() {
    while (1) {
        sem_wait(&state_read);

        clearScreen();
        draw_panel();
        printf("\nFloor %d - Press Enter to call", floor_level);
        fflush(stdout); // print immediately
    }
}

void* listen_thread() {
    MsgBuffer rcv_message;
    while (1) {
        memset(&rcv_message, 0, sizeof(rcv_message));
        msgrcv(msg_id, &rcv_message, MSG_SIZE, floor_level + 5, 0);

        if (strcmp(rcv_message.msg_text, "arrival 1") == 0) {
            lamp_state = 1;
            sem_post(&state_read);
        } else if (strcmp(rcv_message.msg_text, "arrival 0") == 0) {
            lamp_state = 0;
            sem_post(&state_read);
        } else if (strcmp(rcv_message.msg_text, "OK") == 0) {
            call_pressed = 0;
            sem_post(&state_read);
        }
    }
}

int main(int argc, char** argv) {
    if (argc <= 1) {
        printf("Floor argument needed\n");
        exit(0);
    } else if (sscanf(argv[1], "%d", &floor_level) != 1) {
        printf("Invalid Floor\n");
        exit(0);
    } else if (floor_level < 2 || floor_level > 5) {
        printf("Invalid Floor\n");
        exit(0);
    }

    MsgBuffer message;
    
    key_t key = ftok(KEY_FILE_PATH, ID);
    msg_id = msgget(key, 0666 | IPC_CREAT);
    message.msg_type = floor_level;
    
    sem_init(&state_read, 0, 1);

    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, &draw_ui, NULL);
    pthread_create(&tid2, NULL, &listen_thread, NULL);

    while (1) {
        getchar(); // press Enter

        if (call_pressed == 0) {
            call_pressed = 1;
            msgsnd(msg_id, &message, 0, 0);
        }
        sem_post(&state_read);
    }

    return 0;
}
