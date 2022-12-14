#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <signal.h>
#include <pthread.h>

#define MSG_SIZE 100
#define KEY_FILE_PATH "panel"
#define ID 'A'

#define FIFO_FILE_MNG_TO_CTRL "/tmp/fifo_mng_to_ctrl"
#define FIFO_FILE_CTRL_TO_MNG "/tmp/fifo_ctrl_to_mng"
#define BUFF_SIZE 32

int msg_id;
int mng_ctrl_fifo_fd[2]; // 0: write, 1: read
int current_lift_floor = 1;
int isBusy = 0;
int current_alpha = 0;
int isEmergency = 0;
typedef struct msg_buffer
{
    long int msg_type;
    char msg_text[MSG_SIZE];
} MsgBuffer;

typedef struct Request
{
    int floor;
    int deliveryFloor; // only for floor == 1
    int alpha;
} Request;

Request *requestQueue = NULL;
int requestQueueSize = 0;
void enqueue(Request request);
Request dequeue();

void signal_handler(int sig)
{
    msgctl(msg_id, IPC_RMID, NULL);
    exit(0);
}

Request getRequest(MsgBuffer message)
{
    Request request;
    sscanf(message.msg_text, "%d %d %d", &request.floor, &request.deliveryFloor, &request.alpha);
    // printf("Received request: %d %d %d\n", request.floor, request.deliveryFloor, request.alpha);
    return request;
}

void send_message_to_panel(int floor, char message[], int alpha)
{
    MsgBuffer message_to_panel;
    message_to_panel.msg_type = floor + alpha + 5;
    strcpy(message_to_panel.msg_text, message);
    // printf("Sending message: floor %d alpha: %d, %s\n", floor, alpha, message_to_panel.msg_text);
    msgsnd(msg_id, &message_to_panel, strlen(message_to_panel.msg_text), 0);
}

void performRequest(Request request)
{
    isBusy = 1;
    char buf[BUFF_SIZE];
    int sensor, on, flag = 1;
    int closest_floor = 0;
    current_alpha = request.alpha;
    send_message_to_panel(request.deliveryFloor, "trigger", request.alpha);
    // lift-up
    send_message_to_panel(request.deliveryFloor, "arrival 0", request.alpha);

    // So sanh tang hien tai cua thang may va tang nguoi su dung dang o
    if (current_lift_floor < request.floor)
    {
        strcpy(buf, "lift-up");
    }
    else if (current_lift_floor > request.floor)
    {
        strcpy(buf, "lift-down");
    }
    else
    {
        flag = 0;
    }
    int cur_floor = current_lift_floor;
    if (flag)
    {
        write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
        while (cur_floor != request.floor)
        {
            int size = read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
            // printf("BUFF: %s\n", buf);
            if (isEmergency == 1)
            {
                if (strncmp(buf, "EMERGENCY ", 10) == 0)
                {
                    sscanf(buf, "%*s %d", &closest_floor);
                    printf("Closest floor: %d\n", closest_floor);
                
                    char res[BUFF_SIZE];
                    sprintf(res, "%s", "EMERGENCY");

                    MsgBuffer message_to_panel;
                    message_to_panel.msg_type = 100;
                    strcpy(message_to_panel.msg_text, res);
                    msgsnd(msg_id, &message_to_panel, strlen(message_to_panel.msg_text), 0);

                    current_lift_floor = closest_floor;
                    isEmergency = 0;
                    isBusy = 0;
                    return;
                }
            }
            sscanf(buf, "%d%d", &sensor, &on);
            if (sensor == 6)
            {
                if (on)
                    send_message_to_panel(request.deliveryFloor, "error 1", request.alpha);
                else
                    send_message_to_panel(request.deliveryFloor, "error 0", request.alpha);
            }
            else if (on)
            {
                cur_floor = sensor;
            }
        }
    }

    strcpy(buf, "lift-stop");
    write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
    send_message_to_panel(request.deliveryFloor, "arrival 1", request.alpha);
    sleep(2);

    flag = 1;
    // So sanh tang hien tai voi tang nguoi dung muon den
    if (cur_floor < request.deliveryFloor)
    {
        strcpy(buf, "lift-up");
    }
    else if (cur_floor > request.deliveryFloor)
    {
        strcpy(buf, "lift-down");
    }
    else
    {
        flag = 0; // tang hien tai bang tang nguoi dung muon den
    }

    // lift-down
    if (flag)
    {
        write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
        memset(buf, 0, sizeof(buf));
        while (cur_floor != request.deliveryFloor)
        {
                
            int size = read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
            // printf("BUFF: %s\n", buf);
            if (isEmergency == 1)
            {
                if (strncmp(buf, "EMERGENCY ", 10) == 0)
                {
                    sscanf(buf, "%*s %d", &closest_floor);
                    printf("Closest floor: %d\n", closest_floor);
                
                    char res[BUFF_SIZE];
                    sprintf(res, "%s", "EMERGENCY");

                    MsgBuffer message_to_panel;
                    message_to_panel.msg_type = 100;
                    strcpy(message_to_panel.msg_text, res);
                    msgsnd(msg_id, &message_to_panel, strlen(message_to_panel.msg_text), 0);

                    current_lift_floor = closest_floor;
                    isEmergency = 0;
                    isBusy = 0;
                    return;
                }
            }
            sscanf(buf, "%d%d", &sensor, &on);
            if (sensor == 6)
            {
                if (on)
                    send_message_to_panel(request.deliveryFloor, "error 1", request.alpha);
                else
                    send_message_to_panel(request.deliveryFloor, "error 0", request.alpha);
            }
            else if (on)
            {
                cur_floor = sensor;
            }
        }
    }
    strcpy(buf, "lift-stop");
    write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
    char res[BUFF_SIZE];
    sprintf(res, "%s %d", "OK", request.deliveryFloor);
    printf("%d %s\n", request.deliveryFloor, res);
    send_message_to_panel(request.deliveryFloor, res, request.alpha);
    current_lift_floor = request.deliveryFloor;
    isBusy = 0;
}

void *liftCtrlCommunication()
{
    mkfifo(FIFO_FILE_MNG_TO_CTRL, 0666);
    mng_ctrl_fifo_fd[0] = open(FIFO_FILE_MNG_TO_CTRL, O_WRONLY);

    mkfifo(FIFO_FILE_CTRL_TO_MNG, 0666);
    mng_ctrl_fifo_fd[1] = open(FIFO_FILE_CTRL_TO_MNG, O_RDONLY);

    while (1)
    {
        // printf("Emergency now: %d!\n", isEmergency);
        // while (isEmergency == 1)
        // {
        //     printf("Lift is in emergency mode.\n");
        //     isBusy = 0;
        //     char buf[BUFF_SIZE];
        //     int closest_floor = 0;
        //     int size = read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
        //     while (size < 0)
        //     {
        //         size = read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
        //         printf("%s\n", buf);
        //     }

        //     if (strncmp(buf, "EMERGENCY ", 11) == 0)
        //     {
        //         sscanf(buf, "%*s %d", &closest_floor);
        //         printf("Closest floor: %d\n", closest_floor);
            
        //         char res[BUFF_SIZE];
        //         sprintf(res, "%s", "EMERGENCY");

        //         MsgBuffer message_to_panel;
        //         message_to_panel.msg_type = 100;
        //         strcpy(message_to_panel.msg_text, res);
        //         msgsnd(msg_id, &message_to_panel, strlen(message_to_panel.msg_text), 0);

        //         current_lift_floor = closest_floor;
        //         isEmergency = 0;
        //         isBusy = 0;
        //     }
        //     sleep(1);
        // }

        if (requestQueueSize == 0 || isBusy)
        {
            sleep(1);
            continue;
        }

        Request request = dequeue();
        performRequest(request);
    }
}

void *thread_emergency()
{
    char buf[BUFF_SIZE];
    MsgBuffer message;
    while (1)
    {
        memset(&message, 0, sizeof(message));
        msgrcv(msg_id, &message, MSG_SIZE, 100, 0);
        printf("%ld: %s\n", message.msg_type, message.msg_text);
        int alpha;
        sscanf(message.msg_text, "%*d %*d %d\n", &alpha);
        // printf("Alpha: %d %d\n", alpha, current_alpha);
        if (current_alpha == alpha)
        {
            // thuc hien dung thang may
            printf("Emergency! STOP!\n");
            strcpy(buf, "emergency");
            write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
            isEmergency = 1;
            // printf("Emergency in thread emergency: %d!\n", isEmergency);
        }
    }
}

int main()
{
    signal(SIGINT, signal_handler);

    MsgBuffer message;

    key_t key = ftok(KEY_FILE_PATH, ID);
    msg_id = msgget(key, 0666 | IPC_CREAT);

    pthread_t tid, tid1;
    pthread_create(&tid, NULL, &liftCtrlCommunication, NULL);
    pthread_create(&tid1, NULL, &thread_emergency, NULL);

    while (1)
    {
        memset(&message, 0, sizeof(message));
        msgrcv(msg_id, &message, MSG_SIZE, -5, 0);
        printf("%ld: %s\n", message.msg_type, message.msg_text);
        enqueue(getRequest(message));
    }

    return 0;
}

void enqueue(Request request)
{
    requestQueue = (Request *)realloc(requestQueue, (requestQueueSize + 1) * sizeof(Request));
    requestQueue[requestQueueSize] = request;
    requestQueueSize++;
}

Request dequeue()
{
    Request top = requestQueue[0];
    memmove(requestQueue, requestQueue + 1, (requestQueueSize - 1) * sizeof(Request));
    requestQueue = (Request *)realloc(requestQueue, (requestQueueSize - 1) * sizeof(Request));
    requestQueueSize--;
    return top;
}
