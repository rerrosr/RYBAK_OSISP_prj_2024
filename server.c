#define _GNU_SOURCE
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



int svc_init_server(int *s, int server_port);
int send_mes (int s, char *msg, int  msg_size);
int receive_message(int s, char *buf, int  buffer_size, int *msg_size);
int data_connect(char *server_name, int *s, int server_port);
void *client_handler(void *args);

void process_cmd(int msg_size, const char *user_cmd, char *argument, char *cmd);
void process_cwd(char *proc_cwd);
int read_config(char *, char *);
void valid_int(int *, int, int);
int read_file(char *check_mes, int *offset, char *final_buf, FILE *file);
void remove_substr(char *, char *);

void opt_pwd(char *buffer, char *reply_msg, char *cwd);
void opt_help(char *reply_msg);
void opt_ls(char *reply_msg, char *root_dir);
void opt_mkdir(char *argument, char *reply_msg);
void opt_rmdir(char *argument, char *reply_msg);
void opt_del(char *argument, char *reply_msg);
void opt_cd(char *argument, char *reply_msg, int *cd_counter, char *root_dir);
void opt_info(char *buffer, char *reply_msg);
void opt_recv(char *argument, int server_port, char *reply_msg, char *ip_addr);
void opt_put(char *argument, int msg_size, int server_port, char *ip_addr);

FILE *my_file;
pthread_mutex_t mutex;

struct client_socket{
    int client_sock;
    char root_dir[4096];
    char client_ip[1024];
    int server_port;
};

int server_sock;   //сокет для коннекта с клиентом

int main(){
    int client_sock;  //сокет взаимодействия с клиентом
    int status;
    int option;
    char dir_buf[4096];
    char ip_buf[4096];
    char port_buf[4096];
    fprintf(stdout, "\nСчитать данные для подключения:\n"
           "1 — Из файла конфигурации\n"
           "2 — С клавиатуры\n\n"
           "> ");
    valid_int(&option, 1, 2);

    if(option == 1){
        if((status = read_config(dir_buf, port_buf)) != OK){
            fprintf(stderr, "Неправильный формат файла конфигурации\n");
            FILE *file = fopen(CONFIG_FILE, "wb");
            fprintf(file, "root directory = /\n"
                                    "port number = 1231\n");
            fclose(file);
            return EXIT_FAILURE;
        }
        fprintf(stdout,"Корневой каталог :%s\nПорт сервера : %s\n", dir_buf, port_buf);
        if(strcmp(dir_buf, "./") != 0)
            if (chdir(dir_buf) < 0) {
                fprintf(stderr, "Переданный каталог не найден\n");
                exit(1);
            }

        fprintf(stdout, "Начало запуска FTP-сервера\n");
        fprintf(stdout, "Инициализация FTP-сервера\n");
        status = svc_init_server(&server_sock, atoi(port_buf));
        if (status != 0) {
            fprintf(stdout, "Закрытие FTP-сервера из-за ошибки\n");
            exit(status);
        }
    }
    else if(option == 2){
        do{
            memset(dir_buf, 0, sizeof(dir_buf));
            fprintf(stdout, "Введите корнейвой каталог сервера (quit для выхода): \n");
            fgets(dir_buf, sizeof(dir_buf), stdin);
            dir_buf[strlen(dir_buf) - 1] = '\0';
            if (strcmp(dir_buf, "quit") == 0)
                return EXIT_SUCCESS;

            if(strcmp(dir_buf, "./") != 0) {
                if ((status = chdir(dir_buf)) < 0)
                    fprintf(stderr, "Переданный каталог не найден\n");
            } else status = 1;
        } while (status < 0);

        do{
            memset(port_buf, 0, sizeof(port_buf));
            fprintf(stdout, "Введите порт для инициализации сервера (quit для выхода): \n");
            fgets(port_buf, sizeof(port_buf), stdin);
            port_buf[strlen(port_buf) - 1] = '\0';
            if (strcmp(port_buf, "quit") == 0)
                return EXIT_SUCCESS;

            fprintf(stdout, "Начало запуска FTP-сервера\n");
            fprintf(stdout, "Инициализация FTP-сервера\n");
            status = svc_init_server(&server_sock, atoi(port_buf));
            if (status != 0)
                fprintf(stdout, "Закрытие FTP-сервера из-за ошибки\n");
        }while(status < 0);
    }
    fprintf(stdout, "FTP-сервер ждет подключения клиента\n");

    pthread_mutex_init(&mutex, NULL);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("Невозможно настроить соединение : ");
            fprintf(stderr, "FTP-сервер аварийно завершает работу\n");
            close(server_sock);
            continue;
        }
        fprintf(stdout, "Получен доступ к новому клиенту\n");
        pthread_t thread;
        struct client_socket client;
        client.client_sock = client_sock;
        strcpy(client.root_dir, dir_buf);
        strcpy(client.client_ip, inet_ntoa(client_addr.sin_addr));
        client.server_port = atoi(port_buf);
        if ((pthread_create(&thread, NULL, client_handler, &client)) != 0) {
            fprintf(stderr, "Failed to accept client connection\n");
            break;
        }
        pthread_detach(thread);
    }
    pthread_mutex_destroy(&mutex);
    fprintf(stdout,"Закрытие сокета сервера\n");
    close(server_sock);
    return 0;
}

void *client_handler(void *args){
    struct client_socket data = *(struct client_socket*) args;
    int cd_counter = 0;
    char user_cmd[1024];	//введенная пользователем строка
    char cmd[1024];		//команда без аргументов
    char argument[1024]; //аргумент команды
    char reply_msg[4115];
    char buffer[4096];
    int msg_size;        //размер принятого сообщения в байтах
    fprintf(stdout, "cmd 202 Клиент с индексом %lu начал работу\n\n", pthread_self());
    opt_info(buffer, reply_msg);
    process_cwd(data.root_dir);
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
        if(strcmp(cmd, "pwd") == 0)
            opt_pwd(buffer, reply_msg, data.root_dir);
        else if(strcmp(cmd, "ls") == 0)
            opt_ls(reply_msg, data.root_dir);
        else if(strcmp(cmd, "mkdir") == 0)
            opt_mkdir(argument, reply_msg);
        else if(strcmp(cmd, "rmdir") == 0)
            opt_rmdir(argument, reply_msg);
        else if(strcmp(cmd, "del") == 0)
            opt_del(argument, reply_msg);
        else if(strcmp(cmd, "cd") == 0)
            opt_cd(argument, reply_msg, &cd_counter, data.root_dir);
        else if(strcmp(cmd, "info") == 0)
            opt_info(buffer, reply_msg);
        else if(strcmp(cmd, "help") == 0)
            opt_help(reply_msg);
        else if(strcmp(cmd, "recv") == 0)
            opt_recv(argument, data.server_port, reply_msg, data.client_ip);
        else if(strcmp(cmd, "put") == 0)
            opt_put(argument, msg_size, data.server_port, data.client_ip);
        else if(strcmp(cmd, "echo") == 0){
            memset(reply_msg, '\0', sizeof(reply_msg));
            strcpy(reply_msg, argument);
        }
        else if(strcmp(cmd, "quit") == 0) {
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

int svc_init_server (int *s, int server_port){
    int sock;
    struct sockaddr_in svc_addr;

    if((sock = socket(AF_INET, SOCK_STREAM,0)) < 0){
        perror("Ошибка создания сокета\n");
        return(ER_CREATE_SOCKET_FAILED);
    }
    memset((char *)&svc_addr, 0, sizeof(svc_addr));
    svc_addr.sin_family = AF_INET;
    svc_addr.sin_addr.s_addr = htonl(INADDR_ANY);  /* IP сервера */
    svc_addr.sin_port = htons(server_port);    /* порт считывания сервера */

    int flag = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    if(bind(sock, (struct sockaddr *)&svc_addr, sizeof(svc_addr)) < 0){
        perror("Невозможно связать сокет с клиентом\n");
        close(sock);
        return(ER_BIND_FAILED);
    }
    listen(sock, MAX_CLIENTS);
    *s = sock;

    return(OK);
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

void init_signals()
{
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGPIPE, signal_handler);
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

void process_cwd(char *proc_cwd){
    size_t slash_pos = 0;
    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    for(size_t i = 0; i < strlen(cwd); i++)
        if(cwd[i] == '/') slash_pos = i + 1;
    strcpy(proc_cwd, cwd + slash_pos) ;
}

void opt_pwd(char *buffer, char *reply_msg, char *cwd){
    char temp[4096];
    pthread_mutex_lock(&mutex);
    buffer = get_current_dir_name();
    strcpy(buffer,  strcpy(temp, strstr(buffer, cwd)) + strlen(cwd) );
    sprintf(reply_msg, "cmd 250 ok\nserver%s/\n", buffer);
    pthread_mutex_unlock(&mutex);
}

void opt_help(char *reply_msg){
    strcpy(reply_msg, "Команды\t\t Функционал \t\t Синтаксис\n"
                      "pwd  \t\t рабочий каталог   \t pwd\n"
                      "echo  \t\t проверка сообщения с клиентом\t echo mes\n"
                      "info  \t\t информация о сервере\t info\n"
                      "cd   \t\t смена каталога  \t cd dir\n"
                      "del \t\t удалить файл     \t del file\n"
                      "mkdir\t\t создать каталог  \t mkdir dir\n"
                      "rmdir\t\t удалить каталог  \t rmdir dir\n"
                      "ls   \t\t вывести файлы каталога\t ls\n"
                      "recv\t\t скачать файл  \t recv file_name\n"
                      "put\t\t отправить файл  \t put file_name\n"
    );
}


void remove_substr(char *str1, char *str2)
{
    char* temp;
    temp = strstr(str1, str2);
    strcpy(str1, temp + strlen(str2));
}

void opt_ls(char *reply_msg, char *root_dir)
{
    char *directory = get_current_dir_name();
    DIR *dir = opendir(directory);                                                              //открытие каталога

    struct dirent *dir_entry;                                                                   //структура, содержащая имя файла
    struct stat file_stat;                                                                      //структура, содержащая тип файла
    while ((dir_entry = readdir(dir)) != NULL)                                                  //чтение каталога
    {
        if (!(strcmp(dir_entry->d_name, ".") && strcmp(dir_entry->d_name, "..")))
            continue;

        char temp_name[255];
        strcpy(temp_name, directory);
        strcat(temp_name, "/");
        strcat(temp_name, dir_entry->d_name);
        lstat(temp_name, &file_stat);

        if (S_ISDIR(file_stat.st_mode))                                                         //если найдена папка
        {
            strcat(reply_msg, dir_entry->d_name);
            strcat(reply_msg, "/\n");
        }

        if (S_ISREG(file_stat.st_mode))                                                         //если найден файл
        {
            strcat(reply_msg, dir_entry->d_name);
            strcat(reply_msg, "\n");
        }

        if (S_ISLNK(file_stat.st_mode))                                                         //если найдена ссылка
        {
            char link_target[1024];
            memset(link_target, 0, 1024);

            ssize_t len = readlink(dir_entry->d_name, link_target, 1024 - 1);
            if (len != -1)
            {
                link_target[len] = '\0';
                char arrow_type[6];
                struct stat st;

                memset(arrow_type, 0, 6);

                if (lstat(link_target, &st) == 0 && S_ISREG(st.st_mode))
                    strcpy(arrow_type, "-->");
                else
                    strcpy(arrow_type, "-->>");

                remove_substr(link_target, root_dir);

                strcat(reply_msg, dir_entry->d_name);
                strcat(reply_msg, " ");
                strcat(reply_msg, arrow_type);
                strcat(reply_msg, " ");
                strcat(reply_msg, link_target);
                strcat(reply_msg, "\n");
            }
        }
    }

    closedir(dir);                                                                              //закрытие каталога
}


void opt_mkdir(char *argument, char *reply_msg){
    pthread_mutex_lock(&mutex);
    if(strlen(argument) == 0) {
        sprintf(reply_msg,"Повторите запрос. В команде отсутствуют аргументы\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    char sub_command[1031];
    sprintf(sub_command, "mkdir %s\n", argument);
    system(sub_command);
    if(system(sub_command) < 0) {
        sprintf(reply_msg, "Ошибка. Попробуйте еще раз.\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    sprintf(reply_msg, "cmd 212 Каталог %s успешно создан\n", argument);
    pthread_mutex_unlock(&mutex);
}

void opt_rmdir(char *argument, char *reply_msg){
    pthread_mutex_lock(&mutex);
    char sub_command[1030];
    if(strlen(argument) == 0) {
        sprintf(reply_msg, "Повторите запрос. В команде отсутствуют аргументы\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    sprintf(sub_command, "rmdir %s", argument);
    if(system(sub_command) < 0) {
        sprintf(reply_msg, "Ошибка. Попробуйте еще раз.\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    sprintf(reply_msg, "cmd 212 Каталог %s успешно удален\n", argument);
    pthread_mutex_unlock(&mutex);
}

void opt_del(char *argument, char *reply_msg){
    pthread_mutex_lock(&mutex);
    char sub_command[1027];
    if(strlen(argument) == 0) {
        sprintf(reply_msg, "Отсутствует аргумент\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    sprintf(sub_command, "rm %s", argument);
    if(system(sub_command) < 0 ) {
        sprintf(reply_msg, "Ошибка.\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    sprintf(reply_msg, "cmd 211 Файл %s успешно удален\n", argument);
    pthread_mutex_unlock(&mutex);
}

void opt_cd(char *argument, char *reply_msg, int *cd_counter, char *root_dir){
    char *substr;
    char dir[1024];
    char old_dir[1024];
    int counter_start = *cd_counter;

    memset(dir, '\0', 1024);
    strcpy(old_dir,get_current_dir_name());

    pthread_mutex_lock(&mutex);

    if(strlen(argument) != 0){
        substr = strtok(argument, "/\n");
        while (*substr == ' ') substr++;
        if ((strcmp(substr, "..") == 0 && *cd_counter <= 0))
            strcpy(dir, root_dir);
        else if (strcmp(substr, "..") == 0) {
            (*cd_counter)--;
            strcat(dir, substr);
        } else if (strcmp(substr, ".") != 0) {
            (*cd_counter)++;
            strcat(dir, substr);
        }

        while ((substr = strtok(NULL, "/\n")) != NULL) {
            if ((strcmp(substr, "..") == 0 && *cd_counter <= 0))
                continue;

            if (strcmp(substr, ".") == 0)
                continue;

            strcat(dir, "/");
            if (strcmp(substr, "..") == 0) {
                (*cd_counter)--;
                strcat(dir, substr);
            } else {
                (*cd_counter)++;
                strcat(dir, substr);
            }
        }
        fprintf(stdout, "%s\n", dir);

        int result = chdir(dir);
        if (result == 0)
            sprintf(reply_msg, "cmd 250 ok\n");
        else if (errno == ENOENT) {
            sprintf(reply_msg, "Такого каталога не существует\n");
            chdir(old_dir);
            *cd_counter = counter_start;
        }
    }else  sprintf(reply_msg, "Такого каталога не существует\n");
    pthread_mutex_unlock(&mutex);
}

void opt_info(char *buffer, char *reply_msg){
    pthread_mutex_lock(&mutex);
    system("echo FTP-сервер запущен и готов к работе! >> /tmp/meeting.txt");
    my_file = fopen("/tmp/meeting.txt", "r");
    fread(buffer, 4096, sizeof(char), my_file);
    sprintf(reply_msg, "cmd 250 ok \n%s", buffer);
    fclose(my_file);
    system("rm /tmp/meeting.txt");
    pthread_mutex_unlock(&mutex);
}

void opt_recv(char *argument, int server_port, char *reply_msg, char *ip_addr) {
    char sub_command[256];
    char *temp;
    struct stat file_stat;
    if(lstat(argument, &file_stat) == -1){
        if((temp = strstr(argument, ".")) != NULL && (strcmp(strcpy(sub_command, argument + strlen(argument) - 4), ".tar") == 0
                                                            || strcmp(strcpy(sub_command, argument + strlen(argument) - 5), ".tar\"") == 0)){
            *temp = '\0';
            sprintf(sub_command, "tar -zcf \"%s.tar\" \"%s\"", argument, argument);
            memset(argument, 0, strlen(argument));
            strcat(argument, ".tar");
            fprintf(stdout, "%s\n", sub_command);
            system(sub_command);
        }else if(errno == ENOENT) {
            sprintf(reply_msg, "Каталога не существует!\n");
            return;
        }
        else sprintf(reply_msg, "Для скачивания каталогов, нужно его архивировать.\n "
                                "Для скачивания архива введите имя_каталога.tar\n");
    }
    FILE *file;
    char buff[256];
    int s;
    int msg_size;
    int char_read;
    if(strlen(argument) != 0) {
        data_connect(ip_addr, &s, server_port);
        file = fopen(argument, "rb");
        if (file != NULL) {
            while ((char_read = fread(buff, sizeof(char), sizeof(buff), file))) {
                msg_size = send(s, buff, char_read, 0);
                if (msg_size <= 0)
                    break;
            }
            fclose(file);
        } else
            fprintf(stdout, "Такого файла не существует\n");
        close(s);
    }
}

void opt_put(char *argument, int msg_size, int server_port, char *ip_addr) {
    char buff[256];
    char file_name[1024];
    int s;

    if(strlen(argument) != 0){
        strcpy(file_name, strtok(argument, "\n"));
        data_connect(ip_addr, &s, server_port);
        FILE *new_file = fopen(file_name, "wb");
        if (new_file != NULL) {
            while ((msg_size = recv(s, buff, sizeof(buff), 0))) {
                if (msg_size <= 0)
                    break;

                fwrite(buff, sizeof(char), msg_size, new_file);
                fflush(new_file);
                memset(buff, '\0', sizeof(buff));
            }
            fclose(new_file);
        }
        close(s);
        if (msg_size == -1)
            remove(file_name);
    }
}

