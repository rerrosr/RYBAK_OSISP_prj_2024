#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define h_addr h_addr_list[0]

#define SERVER_FTP_PORT 1231
#define DATA_CONNECTION_PORT SERVER_FTP_PORT +1
#define CONFIG "client_config/config.txt"

#define OK 0                                        //коды ошибок
#define ER_INVALID_HOST_NAME -1
#define ER_CREATE_SOCKET_FAILED -2
#define ER_BIND_FAILED -3
#define ER_CONNECT_FAILED -4
#define ER_SEND_FAILED -5
#define ER_RECEIVE_FAILED -6

int control_socket;			//управляющий сокет

int read_file(char *check_mes, int *offset, char *final_buf, FILE *file){
    char buf[256];

    memset(buf, '\0', sizeof(buf));
    fgets(buf, 256, file);
    if (!strlen(buf) || strncmp(buf, check_mes, strlen(check_mes)) != 0){
        fclose(file);
        return -1;
    }
    fseek(file, *offset + strlen(check_mes), SEEK_SET);
    memset(buf, '\0', sizeof(buf));
    fgets(buf, 256, file);
    *offset = ftell(file);
    buf[strlen(buf) - 1] = '\0';
    strcpy(final_buf, buf);
    return OK;
}

int send_message(int s, char *msg, int msg_size)
{
    if((send(s, msg, msg_size, 0)) < 0)             //пересылка данных через сокет
    {
        fprintf(stderr, "Не удалось отправить команду!\n");
        return(ER_SEND_FAILED);
    }

    return(OK);
}

void signal_handler(int sig)
{
    signal(sig, SIG_DFL);

    if (sig == SIGPIPE)
    {
        printf("Сервер разорвал подключение!");
        signal(SIGINT, SIG_DFL);
    }
    else
        send_message(control_socket, "quit", 4);
    close(control_socket);
    printf("\nFTP-Client завершил работу.\n");

    kill(getpid(), SIGINT);
}

int receive_message(int s, char *buffer, int buffer_size, int *msg_size)
{
    *msg_size = recv(s, buffer, buffer_size, 0);         //получение сообщения

    if(*msg_size <= 0)
    {
        fprintf(stderr, "Не удалось получить сообщение!\n");
        return(ER_RECEIVE_FAILED);
    }

    return (OK);
}

void init_signals()
{
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGPIPE, signal_handler);
}

int read_config(char *ip_buf, char *port_buf)
{
    int offset = 0;
    FILE *file;
    file = fopen(CONFIG, "rb");
    if (file == NULL)
        return -4;
    if(read_file("ip = ", &offset, ip_buf, file) != OK) return -2;
    if(read_file("port = ", &offset, port_buf, file) != OK) return -3;
    fclose(file);
    return OK;
}

