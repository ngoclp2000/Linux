#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>

#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

#define MSG_SIZE 100
#define KEY_FILE_PATH "panel"
#define ID 'A'
#define SNAME "/mysem"

int msg_id;
sem_t state_read; //, state_write;

int floor_level = 0;
int delivery_pressed[5]; // ignore index 0
int lamp_state = 1;      // 0: off, 1: arrival, 2: error
int alpha = 0;
typedef struct msg_buffer
{
    long msg_type;
    char msg_text[MSG_SIZE];
} MsgBuffer;

typedef struct msg_floor
{
    int current_floor;
    int destination_floor;
} MsgFloor;

void clearScreen()
{
    printf("%c[2J%c[;H", (char)27, (char)27);
}

void draw_panel()
{
    if (lamp_state == 0)
        printf("(" WHT "O" RESET ")\n"); // off - white
    else if (lamp_state == 1)
        printf("(" GRN "O" RESET ")\n"); // arrival - green
    else
        printf("(" RED "O" RESET ")\n"); // error - red

    printf("---\n");

    for (int i = 5; i >= 1; i--)
    {
        if (delivery_pressed[i - 1] == 0)
            printf("(" WHT "%d" RESET ")\n", i);
        else
            printf("(" GRN "%d" RESET ")\n", i);
    }
}

MsgFloor getDeliveryFloorInput()
{
    int current_floor, destination_floor;
    char buffer[16];
    while (1)
    {
        fgets(buffer, 16, stdin);
        if (sscanf(buffer, "%d %d", &current_floor, &destination_floor) == 2)
        {
            MsgFloor msg_floor;
            memset(&msg_floor, 0, sizeof(msg_floor));
            if (1 <= current_floor && current_floor <= 5 && 1 <= destination_floor && destination_floor <= 5)
            {
                msg_floor.current_floor = current_floor;
                msg_floor.destination_floor = destination_floor;
                return msg_floor;
            }
        }

        printf("Invalid Floor, try again: ");
    }
}

void *draw_ui()
{
    while (1)
    {
        sem_wait(&state_read);

        clearScreen();
        draw_panel();
        printf("\nFloor 1 - Input delivery floor: ");
        fflush(stdout); // print immediately
    }
}

void *listen_thread()
{
    MsgBuffer rcv_message;
    while (1)
    {
        if (floor_level != 0)
        {
            memset(&rcv_message, 0, sizeof(rcv_message));
            printf("Waiting for message with type %d\n", floor_level + alpha + 5);
            msgrcv(msg_id, &rcv_message, MSG_SIZE, floor_level + alpha + 5, 1);
            
            if (strcmp(rcv_message.msg_text, "arrival 1") == 0)
            {
                lamp_state = 1;
                sem_post(&state_read);
            }
            else if (strcmp(rcv_message.msg_text, "arrival 0") == 0)
            {
                lamp_state = 0;
                sem_post(&state_read);
            }
            else if (strncmp(rcv_message.msg_text, "OK ", 3) == 0)
            {
                int i;
                sscanf(rcv_message.msg_text, "%*s%d", &i);
                delivery_pressed[i - 1] = 0;
                floor_level = 0;
                sem_post(&state_read);
                // printf("Here : %ld %d\n", rcv_message.msg_type, i);
            }
            else if (strcmp(rcv_message.msg_text, "error 1") == 0)
            {
                lamp_state = 2;
                sem_post(&state_read);
            }
            else if (strcmp(rcv_message.msg_text, "error 0") == 0)
            {
                lamp_state = 0;
                sem_post(&state_read);
            }
            else
            {
                sem_post(&state_read);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    MsgBuffer message;

    key_t key = ftok(KEY_FILE_PATH, ID);
    msg_id = msgget(key, 0666 | IPC_CREAT);
    sem_init(&state_read, 1, 1);
    if (argc > 1)
    {
        alpha = atoi(argv[1]);
    }
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, &draw_ui, NULL);
    pthread_create(&tid2, NULL, &listen_thread, NULL);

    while (1)
    {
        MsgFloor msg_floor = getDeliveryFloorInput();
        if (delivery_pressed[msg_floor.destination_floor - 1] == 0)
        {
            floor_level = msg_floor.destination_floor;
            message.msg_type = floor_level;
            delivery_pressed[msg_floor.destination_floor - 1] = 1;
            sprintf(message.msg_text, "%d %d %d", msg_floor.current_floor, msg_floor.destination_floor, alpha);
            msgsnd(msg_id, &message, strlen(message.msg_text), 0);
        }
        sem_post(&state_read);
    }

    return 0;
}
