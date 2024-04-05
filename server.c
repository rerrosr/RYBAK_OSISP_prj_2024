#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_CLIENTS 10

//статус коды
#define OK 0
#define ER_INVALID_HOST_NAME (-1)
#define ER_CREATE_SOCKET_FAILED (-2)
#define ER_BIND_FAILED (-3)
#define ER_CONNECT_FAILED (-4)
#define ER_SEND_FAILED (-5)
#define ER_RECEIVE_FAILED (-6)

#define CONFIG_FILE "config"

struct client_socket{
    int client_sock;
    char root_dir[4096];
    char client_ip[1024];
    int server_port;
};

int server_sock;   //сокет для коннекта с клиентом

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

int data_connect (char *server_name, int *s, int server_port){ //создание принимающего сокета
    int sock;

    struct sockaddr_in client_address;  //локальный IP клиента
    struct sockaddr_in server_address;	//IP сервера
    struct hostent *server_IP_structure; //IP сервера в бинарном виде

    if((server_IP_structure = gethostbyname(server_name)) == NULL){ //получаем IP сервера
        fprintf(stdout, "%s неизвестный адрес\n", server_name);
        return (ER_INVALID_HOST_NAME);  /* error return */
    }
    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Невозможно создать сокет\n");
        return (ER_CREATE_SOCKET_FAILED);
    }
    memset((char *) &client_address, 0, sizeof(client_address));
    client_address.sin_family = AF_INET;
    client_address.sin_addr.s_addr = htonl(INADDR_ANY);
    client_address.sin_port = 0;

    int flag = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    if(bind(sock, (struct sockaddr *)&client_address, sizeof(client_address)) < 0){
        perror("Невозможно связать сокет с клиентом\n");
        close(sock);
        return(ER_BIND_FAILED);
    }
    memset((char *) &server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    memcpy((char *) &server_address.sin_addr, server_IP_structure->h_addr_list[0],server_IP_structure->h_length);
    server_address.sin_port = htons(server_port + 1);
    if (connect(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0){
        fprintf(stdout, "\n %s\n", strerror(errno));
        close (sock);
        return(ER_CONNECT_FAILED);
    }
    *s = sock;
    return(OK);
}

int send_mes(int s, char *msg, int msg_size){
    int i;
    for(i=0; i < msg_size; i++)
        fprintf(stdout,"%c",msg[i]);
    fprintf(stdout,"\n");

    if((send(s, msg, msg_size, 0)) < 0){
        perror("Ошибка отправки сообщения клиенту\n");
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
        send_mes(server_sock, "quit", 4);
    close(server_sock);
    printf("\nFTP-Client завершил работу.\n");

    kill(getpid(), SIGINT);
}

void process_cmd(int msg_size, const char *user_cmd, char *argument, char *cmd){
    int i = 0, j = 0;
    memset(cmd, 0, 1024);
    memset(argument, 0, 1024);
    while(user_cmd[i] == ' ')
        i++;
    while(i < msg_size && user_cmd[i] != ' ' && user_cmd[i] != '\n'){
        cmd[j] = user_cmd[i];
        i++;
        j++;
    }
    while(user_cmd[i] == ' ')
        i++;
    j = 0;
    if(user_cmd[i] == '"') {
        i++;
        while(i < msg_size && user_cmd[i] != '"'){
            argument[j] = user_cmd[i];
            i++;
            j++;
        }
    }
    else{
        while(i < msg_size && user_cmd[i] != ' ' && user_cmd[i] != '\n'){
            argument[j] = user_cmd[i];
            i++;
            j++;
        }
    }
}

int read_config(char *dir_buf, char *port_buf)
{
    int offset = 0;
    FILE *file;
    file = fopen(CONFIG_FILE, "rb");
    if (file == NULL)
        return -4;
    if(read_file("root directory = ", &offset, dir_buf, file) != OK) return -1;
    if(read_file("port number = ", &offset, port_buf, file) != OK) return -3;
    fclose(file);
    return OK;
}

void init_signals()
{
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGPIPE, signal_handler);
}

int receive_message (int s, char *buf, int buffer_size, int *msg_size ){
    int i;
    *msg_size = recv(s, buf, buffer_size, 0);

    if(*msg_size < 0){
        perror("Ошибка получения сообщения от клиента\n");
        return(ER_RECEIVE_FAILED);
    }
    for(i = 0; i < *msg_size; i++)
        fprintf(stdout, "%c", buf[i]);
    fprintf(stdout,"\n");

    return (OK);
}

void valid_int(int *num, int min, int max)
{
    int c;
    do
    {
        if(!scanf("%d", num))
        {
            printf("Введите число!\n");
            do
            {
                c = fgetc(stdin);
            }while (c != '\n' && c != EOF);
            continue;
        }
        if(*num < min || *num > max)
        {
            printf("Число должно быть в промежутке от %d до %d!\n", min, max);
            continue;
        }
        getchar();

        break;
    }while(true);
}

void *client_handler(void *args){
    struct client_socket data = *(struct client_socket *) args;

    int cd_counter = 0;
    char user_cmd[1024];	//введенная пользователем строка
    char cmd[1024];		//команда без аргументов
    char argument[1024]; //аргумент команды
    char reply_msg[4115];
    char buffer[4096];
    int msg_size;        //размер принятого сообщения в байтах
    fprintf(stdout, "cmd 202 Клиент с индексом %lu начал работу\n\n", pthread_self());
    if(send_mes(data.client_sock, reply_msg, strlen(reply_msg) + 1) < 0){
        fprintf(stderr, "Ошибка соединения\n");
        close (data.client_sock);  //завершение работы;
        pthread_exit(NULL);
    }
    do{
        memset(reply_msg, '\0', sizeof(reply_msg));
        memset(user_cmd, '\0', sizeof(user_cmd));
        memset(buffer, '\0', sizeof(buffer));
        memset(cmd, '\0', sizeof(cmd));
        memset(argument, '\0', sizeof(argument));
        if(receive_message(data.client_sock, user_cmd, sizeof(user_cmd), &msg_size) < 0){
            fprintf(stdout,"Ошибка получения команды. Закрытие портов \n");
            fprintf(stderr,"FTP-сервера аварийно завершен\n");
            break;
        }
        process_cmd(msg_size, user_cmd, argument, cmd);
        fprintf(stdout,"Команда пользователя : %s\n", cmd);
        fprintf(stdout,"с аргументом : %s\n", argument);

        if(strcmp(cmd, "quit") == 0) {
            memset(reply_msg, '\0', sizeof(reply_msg));
            sprintf(reply_msg, "cmd 202 Клиент завершил работу\n");
            fprintf(stdout, "cmd 202 Клиент с индексом %lu завершил работу\n", pthread_self());
        } else
            sprintf(reply_msg, "cmd 202 Такой команды не предусмотрено\n");

        if(send_mes(data.client_sock, reply_msg, strlen(reply_msg) + 1) < 0)
            break;
    }while(strcmp(cmd, "quit") != 0);

    fprintf(stdout,"Закрытие сокета клиента\n");
    close (data.client_sock);  //завершение работы;
    pthread_exit(NULL);
}