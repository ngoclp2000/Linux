#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <pthread.h>

#define FIFO_FILE_SENSOR "/tmp/fifo_sensor"
#define FIFO_FILE_MNG_TO_CTRL "/tmp/fifo_mng_to_ctrl"
#define FIFO_FILE_CTRL_TO_MNG "/tmp/fifo_ctrl_to_mng"
#define BUFF_SIZE 32

int lift_body_pid, lift_sensor_pid;
int sensor_fifo_fd;
int mng_ctrl_fifo_fd[2]; // 0: read, 1: write

// key for shared memory b/w liftCtrl, liftBody & liftSensor
key_t key;

int my_round(float f)
{
    f += 0.5;
    return (int)f;
}

// -------------------- liftBody -------------------- //

void liftBody()
{
    // shared memory: height & state
    int shmid = shmget(key, 8, 0660 | IPC_CREAT);

    // height from center of lift body to ground (start at floor 1)
    float *height = (float *)shmat(shmid, (void *)0, 0);
    *height = 1.5;

    // state (0: stop, 1: go up, -1: go down)
    int *state = (int *)((void *)height + 4);
    *state = 0;

    // speed = d / t (0.5m / s ~ 0.25m / 0.5s)
    float d = 0.25, t = 0.5;

    while (1)
    {
        *height += *state * d;

        if (*height == 1.5)
        {
            *state = 0;
        }

        //printf("%c[2J%c[;H", (char)27, (char)27);
        printf("Floor\tHeight\n");
        printf("%d\t%.2f m\n", my_round((*height + 1.5) / 3), *height);
        fflush(stdout);

        usleep(t * 1000000);
    }
}

// ------------------- liftSensor ------------------- //

void *floorSensor(void *arg)
{
    int floor = *((int *)arg);
    float floor_height = 3 * floor - 1.5; // tang 5: 13.5      13 <= height <= 14
    int sensor = 0, sensor_tmp;

    // shared memory: height
    int shmid = shmget(key, 8, 0660 | IPC_CREAT);
    float *height = (float *)shmat(shmid, (void *)0, 0);
    int *state = (int *)((void *)height + 4);
    char buf[BUFF_SIZE];
    while (1)
    {
        if (floor_height - 0 <= *height && *height <= floor_height + 0)
        {
            sensor_tmp = 1;
            if (abs(*state) == 2)
            {
                *state = 0;
            }
        }
        else
        {

            sensor_tmp = 0;
        }

        if (sensor != sensor_tmp)
        {
            sensor = sensor_tmp;
            // send sensor info to liftMng
            sprintf(buf, "%d %d", floor, sensor);
            write(sensor_fifo_fd, buf, strlen(buf) + 1);
        }

        usleep(0.25 * 1000000);
    }
}

void liftSensor()
{
    // shared memory: height
    int shmid = shmget(key, 8, 0660 | IPC_CREAT);
    float *height = (float *)shmat(shmid, (void *)0, 0);

    // named pipe (FIFO): sending sensor notify - write only
    sensor_fifo_fd = open(FIFO_FILE_SENSOR, O_WRONLY);

    // Floor Position Sensor
    for (int i = 1; i <= 5; i++)
    {
        pthread_t tid;
        int *arg = (int *)calloc(1, sizeof(int));
        *arg = i;
        pthread_create(&tid, NULL, &floorSensor, arg);
    }

    // Error Sensor
    int error_sensor = 0, error_sensor_tmp;
    char buf[BUFF_SIZE];
    while (1)
    {
        if (*height >= 3 * 5 - 1)
        {
            error_sensor_tmp = 1;
        }
        else
        {
            error_sensor_tmp = 0;
        }

        if (error_sensor != error_sensor_tmp)
        {
            error_sensor = error_sensor_tmp;
            // send error sensor info to liftCtrl
            sprintf(buf, "%d %d", 6, error_sensor);
            write(sensor_fifo_fd, buf, strlen(buf) + 1);
        }

        usleep(0.5 * 1000000);
    }
}

// -------------------- liftCtrl -------------------- //

void *listen_lift_sensor()
{
    // named pipe (FIFO): receiving sensor notify - read only
    sensor_fifo_fd = open(FIFO_FILE_SENSOR, O_RDONLY);

    // named pipe (FIFO): sending lift arrival & movement notify - write only
    mkfifo(FIFO_FILE_CTRL_TO_MNG, 0666);
    mng_ctrl_fifo_fd[1] = open(FIFO_FILE_CTRL_TO_MNG, O_WRONLY);

    // shared memory: state (to stop when error sensor ON)
    int shmid = shmget(key, 8, 0660 | IPC_CREAT);
    int *state = (int *)(shmat(shmid, (void *)0, 0) + 4);

    char buf[BUFF_SIZE];
    int sensor, on;
    while (1)
    {
        // receive and forward sensor notify from liftSensor to liftMng
        read(sensor_fifo_fd, buf, BUFF_SIZE);
        write(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);

        // force stop on rise-error
        sscanf(buf, "%d%d", &sensor, &on);
        if (sensor == 6 && on == 1)
        {
            *state = 0;
        }
    }
}

void liftCtrl()
{
    // shared memory: state
    int shmid = shmget(key, 8, 0660 | IPC_CREAT);
    int *state = (int *)(shmat(shmid, (void *)0, 0) + 4);

    // listen sensor notify from liftSensor
    pthread_t tid;
    pthread_create(&tid, NULL, &listen_lift_sensor, NULL);

    // named pipe (FIFO): receiving command from liftMng - read only
    mkfifo(FIFO_FILE_MNG_TO_CTRL, 0666);
    mng_ctrl_fifo_fd[0] = open(FIFO_FILE_MNG_TO_CTRL, O_RDONLY);

    // receive command from liftMng
    char buf[BUFF_SIZE];
    memset(buf, 0, BUFF_SIZE);
    while (1)
    {
        int size = read(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);

        if (strcmp(buf, "lift-up") == 0)
        {
            *state = 1;
        }
        else if (strcmp(buf, "lift-down") == 0)
        {
            *state = -1;
        }
        else if (strcmp(buf, "lift-stop") == 0)
        {
            *state = 0;
        }
        else if (strcmp(buf, "emergency") == 0)
        {
            if (*state < 0)
            {
                *state = -2;
            }
            else if (*state > 0)
            {
                *state = 2;
            }
        }
    }
}

// ---------------------- main ---------------------- //

void signal_handler(int sig)
{
    close(sensor_fifo_fd);
    close(mng_ctrl_fifo_fd[0]);
    close(mng_ctrl_fifo_fd[1]);

    unlink(FIFO_FILE_SENSOR);
    unlink(FIFO_FILE_MNG_TO_CTRL);
    unlink(FIFO_FILE_CTRL_TO_MNG);

    kill(lift_body_pid, SIGKILL);
    kill(lift_sensor_pid, SIGKILL);
    exit(0);
}

int main()
{
    signal(SIGINT, signal_handler);
    key = ftok("shm", 65);

    lift_body_pid = fork();
    if (lift_body_pid == 0)
    {
        // Child Process 1: liftBody
        liftBody();
        exit(0);
    }

    mkfifo(FIFO_FILE_SENSOR, 0666);

    lift_sensor_pid = fork();
    if (lift_sensor_pid == 0)
    {
        // Child Process 2: liftSensor
        liftSensor();
        exit(0);
    }

    // Parent Process: liftCtrl
    liftCtrl();
}
