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

int exec_command();
void signal_handler(int);
void init_signals();
void init_strings();
void init_dirs();
int read_config(char *, char*);
int read_file(char *, int *, char *, FILE *);
void change_dir(char *);
void file_info(char *, char *, int);
void valid_int(int *, int, int);

int make_connection(char *, char *, int *);
int get_data_socket(int *);

void process_path(char *, char *, char *);
int put_file(char *, char *);
int recv_file(char *);

int send_message(int, char *, int);
int receive_message(int, char *, int, int *);
void print_message(char *, int);

char user_input[1024];	    //ввод пользователя
char server_reply[4096];    //ответ сервера
char current_dir[1024];

char ip_buffer[1024];
char port_buffer[1024];

int control_socket;			//управляющий сокет

int main()
{
	int msg_size;	        //размер сообщения
	int status = OK;
	int option;

	init_signals();
	init_strings();
	init_dirs();
	printf("\nСчитать данные для подключения:\n"
		   "1 — Из файла конфигурации\n"
		   "2 — С клавиатуры\n\n"
		   "> ");
	valid_int(&option, 1, 2);

	if(option == 1)
	{
		if((status = read_config(ip_buffer, port_buffer)) != OK)
		{
			fprintf(stderr, "Неправильный формат файла конфигурации\n");
			FILE *file = fopen(CONFIG, "wb");
			fprintf(file, "ip = localhost\n");
			fprintf(file, "port = 1231\n");
			fclose(file);
			return EXIT_FAILURE;
		}
		if ((status = make_connection(ip_buffer, port_buffer, &control_socket)) != OK)
			return EXIT_FAILURE;
	}

	if(option == 2)
	{
		do
		{
			printf("\nВведите IP-адрес сервера (quit для выхода): ");						//192.168.1.105
			scanf("%s", ip_buffer);
			getc(stdin);

			if (strcmp(ip_buffer,"quit") == 0) 
				return EXIT_SUCCESS;

			printf("\nВведите порт (quit для выхода): ");						//192.168.1.105
			scanf("%s", port_buffer);
			getc(stdin);

			if (strcmp(port_buffer,"quit") == 0) 
				return EXIT_SUCCESS;

			printf("Подключение к серверу...\n\n");	
			status = make_connection(ip_buffer, port_buffer, &control_socket);  							//переделать с таймером
		} while (!control_socket || status != OK);
	}

	FILE *file = fopen(CONFIG, "wb");
	fprintf(file, "ip = %s\n", ip_buffer);
	fprintf(file, "port = %s\n", port_buffer);
	fclose(file);

	printf("FTP-Client начал работу.\n");

	status = receive_message(control_socket, server_reply, 
							 sizeof(server_reply), &msg_size);
	if(status == OK)
		print_message(server_reply, msg_size);

	while (true)
	{
		init_strings();
		printf("%s> ", current_dir);
		fgets(user_input, 1024, stdin); 

		if (strlen(user_input) <= 1)
			continue;
		
		if (user_input[0] != '@')
            status = exec_command();
        else
        {
            FILE *script_file;
            user_input[strcspn(user_input, "\n")] = '\0'; 
            if((script_file = fopen(user_input + 1, "r")) == NULL)
            {
                fprintf(stderr, "Такого файла не существует\n\n");
                continue;
            }

            while (!feof(script_file))
            {
                fgets(user_input, 1024, script_file);
                printf("%s> %s", current_dir, user_input);

				status = exec_command();
				if (!strcmp(user_input, "quit\n"))
					break;
            }
            fclose(script_file);
        }

		if (!strcmp(user_input, "quit\n"))
			break;
    }

	close(control_socket);
	printf("FTP-Client завершил работу.\n");
	
	return EXIT_SUCCESS;
} 

int exec_command()
{
	char command[1024];		    //команда, содержащаяся во вводе пользователя
	char argument[1024];	    //аргументы для command
	char path[256];
	int status = OK;
	int msg_size;

	memset(path, '\0', 256);
	if(strncmp("put ", user_input, 4) == 0) 
	{
		process_path(user_input + 4, path, argument);
		strcpy(user_input, "put ");
		strcat(user_input, argument);
	}

	memset(command, '\0', 1024);
	memset(argument, '\0', 1024);

	status = send_message(control_socket, user_input, strlen(user_input) + 1);
	if(status != OK) 
	{
		fprintf(stderr, "Ошибка при посылке команды!\n");
		return ER_SEND_FAILED;
	}

	if(strstr(user_input, " ") != NULL) 
	{
		strcpy(command, strtok(user_input, " "));
		strcpy(argument, strtok(NULL, "\n"));

		if(strcmp("put", command) == 0) 
		{
			status = put_file(path, argument);
			if (status != OK)
				fprintf(stderr, "Ошибка при загрузке файла!\n");
		}
		
		if(strcmp("recv", command) == 0) 
		{
			status = recv_file(argument);
			if (status != OK)
				fprintf(stderr, "Ошибка при скачивании файла!\n");
		}
	}

	status = receive_message(control_socket, server_reply, 
								sizeof(server_reply), &msg_size);

	if(status != OK)
	{
		fprintf(stderr, "Ошибка при получении ответа от сервера!\n");
		return ER_RECEIVE_FAILED;
	}
	else
		print_message(server_reply, msg_size);

	if (!strcmp(command, "cd") && 
			strcmp(server_reply, "Такого каталога не существует\n"))
		change_dir(argument);

	return status;
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

void init_signals()
{
	signal(SIGINT, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGPIPE, signal_handler);
}

void init_strings()
{
	memset(user_input, '\0', 1024);
	memset(server_reply, '\0', 4096);
}

void init_dirs()
{
	if(access("client_config", F_OK) != 0)
		mkdir("client_config", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if(access("client_config/config.txt", F_OK) != 0)
	{
		FILE *file = fopen(CONFIG, "wb");
		fprintf(file, "ip =\n");
		fprintf(file, "port =\n");
		fclose(file);
	}
	if(access("downloads", F_OK) != 0)
		mkdir("downloads", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
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

void change_dir(char *path)
{
    char arg[1024];
    char temp[1024];
    char *substr;
    int i = 0;

    memset(arg, '\0', 1024);
    memset(temp, '\0', 1024);

    strcpy(arg, path);
    arg[strcspn(arg, "\n") + 1] = '\0'; 
    strcpy(temp, current_dir);
    i = strlen(temp);

    if (!strlen(arg))
        return;
    

    substr = strtok(arg, "/");
    if (strcmp(substr, "..") == 0)
    {
        temp[i - 1] = '\0';
        i--;
        for (;temp[i] != '/' && i > 0; i--)
            temp[i] = '\0';
        temp[i] = '\0';
    } else if (strcmp(substr, ".") != 0)
    {
        if (strlen(temp))
            strcat(temp, "/");
        strcat(temp, substr);
    }

    while ((substr = strtok(NULL, "/")) != NULL)
    {
        i = strlen(temp);
        if (strcmp(substr, ".") == 0)
            continue;

        if (strcmp(substr, "..") == 0)
        {
            temp[i - 1] = '\0';
            i--;
            for (;temp[i] != '/' && i > 0; i--)
                temp[i] = '\0';
            temp[i] = '\0';
            continue;
        } else 
        {
            if (strlen(temp))
                strcat(temp, "/");
            strcat(temp, substr);
        }
    }
    strcpy(current_dir, temp);
}

void file_info(char *file_path, char *file_name, int mode)
{
	struct stat file_stat;
	char buf[1024];
	long size = 0;
	char size_str[20];

	lstat(file_path, &file_stat);
	size = file_stat.st_size;
	sprintf(size_str, "%lu ", size);

	if (mode)
		strcpy(buf, "Скачан файл: ");
	else
		strcpy(buf, "Загружен файл: ");

	strcat(buf, file_name);
	strcat(buf, "\nРазмер файла: ");
	strcat(buf, size_str);
	
	if (size % 10 > 1 && size % 10 < 5)
		strcat(buf, "байта\n");	
	else
		strcat(buf, "байтов\n");	

	printf("%s\n", buf);
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

int make_connection (char *server_name, char* port, int *s)
{
	int flag;
	int sock;	                            		//номер сокета
	struct sockaddr_in client_address;  			//IP клиента
	struct sockaddr_in server_address;	    		//IP сервера
	struct hostent	   *server_IP_structure;
	struct timeval timeout;

	if((server_IP_structure = gethostbyname(server_name)) == NULL)     //получение данных о сервера
	{
		printf("Сервер \"%s\" не найден.\n", server_name);
		return (ER_INVALID_HOST_NAME);
	}

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)                //создание сокета
	{
		fprintf(stderr, "Не удалось создать сокет!\n");
		return (ER_CREATE_SOCKET_FAILED);
	}
	flag = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

	memset((char *) &client_address, 0, sizeof(client_address));    //инициализация памяти

	client_address.sin_family = AF_INET;	
	client_address.sin_addr.s_addr = htonl(INADDR_ANY);     //INADDR_ANY = 0 означает, что IP адрес клиента будет определён системой
	client_address.sin_port = 0;                            //0 значит, что порт будет определён системой

	if(bind(sock, (struct sockaddr *)&client_address, sizeof(client_address)) < 0)  //связать сокет с IP и портом клиента
	{
		fprintf(stderr, "Не удалось связать сокет с IP и портом клиента!\n");
		close(sock);                                                   //закрытие сокета
		return(ER_BIND_FAILED);
	}

	memset((char *) &server_address, 0, sizeof(server_address)); 	//инициализация памяти

	server_address.sin_family = AF_INET;
	memcpy((char *) &server_address.sin_addr, server_IP_structure->h_addr,
		server_IP_structure->h_length);
	server_address.sin_port = htons(atoi(port));

	timeout.tv_sec  = 5;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	if (connect(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) //подключение к серверу
	{
		fprintf(stderr, "Время ожидания истекло!\n");
		close(sock); 	                        //закрытие сокета
		return(ER_CONNECT_FAILED);
	}
	timeout.tv_sec = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	*s = sock; //сохранение номера сокета
    
	return(OK); 
}

int get_data_socket (int *s)
{
	int flag;
    int sock;
	struct sockaddr_in server_address;

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Не удалось создать сокет!\n");
		return(ER_CREATE_SOCKET_FAILED);
	}
	flag = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

	memset((char *)&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(atoi(port_buffer) + 1);

	if(bind(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)   //связать сокет с IP и портом сервера
	{
		fprintf(stderr, "Не удалось связать сокет с IP и портом сервера!\n");
		close(sock);                                                //закрытие сокета
		return ER_BIND_FAILED;	
	}

	listen(sock, 1);  	
	*s = sock;              //сохранение номера сокета

	return OK; 
}

void process_path(char *user_input, char *file_path, char *file_name)
{
	char *last_slash = NULL;
	char *old_path = get_current_dir_name();
	int i = 0;

	for (i = 0; i < (int)strlen(user_input); i++)
		if (user_input[i] == '/')
			last_slash = user_input + i;

	if (last_slash == NULL)
	{
		strcpy(file_path, user_input);
		strcpy(file_name, user_input);
		file_path[strcspn(file_path, "\n")] = '\0'; 
		return;
	}

	*last_slash = '\0';
	strcpy(file_name, last_slash + 1);
	
	if (user_input[0] == '~')
	{
		strcpy(file_path, getenv("HOME"));
		strcat(file_path, user_input + 1);
	}
	else
		strcpy(file_path, user_input);

	chdir(file_path);
	strcpy(file_path, get_current_dir_name());
	strcat(file_path, "/");
	strcat(file_path, file_name);
	file_path[strcspn(file_path, "\n")] = '\0'; 
	chdir(old_path);
}

void archive(char *file_path, char *file_name)
{
	char buf[4096];
	char full_path[1024];
	strcpy(full_path, file_path);
	strcat(full_path, file_name);
	*(full_path + strlen(full_path) - 4) = '\0';
	sprintf(buf, "tar -zcf \"%s\" \"%s\"",file_name, full_path);
	system(buf);
}

void del_archive(char *file_path, char *file_name)
{
	char full_path[1024];
	strcpy(full_path, file_path);
	strcat(full_path, file_name);
	remove(full_path);
}

int put_file(char *file_path, char *file_name)
{
	FILE *file;
	char buff[256];	
	char full_path[1024];
	int data_socket;
	int temp_socket;
	int status = OK;	
	int char_read = 0;
	int msg_size = -1;

	strcpy(full_path, file_path);
	strcpy(full_path, file_name);
	struct stat file_stat;
	struct stat check;
	memset(&file_stat, 0, sizeof(struct stat));
	memset(&check, 0, sizeof(struct stat));
	if (strcmp(file_name + strlen(file_name) - 4, ".tar") == 0)
	{
		int res = lstat(full_path, &file_stat);
		if(res == -1)
		{
			fprintf(stderr, "Ошибка архивации!\n");
			if (errno == ENOENT)
				fprintf(stderr, "Каталога не существует!\n");

			return -1;
		}
		if (S_ISDIR(file_stat.st_mode))                                                         //если найдена папка
			archive(file_path, file_name);
		else
		{
			fprintf(stderr, "Это не каталог!\n");
			return -1;
		}
	}

	if((status = get_data_socket(&temp_socket)) != OK)
		return status;
	data_socket = accept(temp_socket, NULL, NULL);
	close(temp_socket);

	file = fopen(file_path, "rb");
	if(file != NULL) 
	{
		while((char_read = fread(buff, sizeof(char), sizeof(buff), file)))
		{
            msg_size = send(data_socket, buff, char_read, 0);
            if (msg_size == -1 || msg_size == 0) 
                break;
			memset(buff, '\0', sizeof(buff));
		}
		fclose(file);
	} 
	else 
		fprintf(stderr, "Такого файла не существует!\n");

	close(data_socket);


	if (memcmp(&file_stat, &check, sizeof(struct stat)) != 0)
		del_archive(file_path, file_name);

	if(msg_size != -1)
		file_info(file_path, file_name, 0);

	return status;
}

int recv_file(char* file_name)
{
	FILE *file;
	char buff[256];
	char file_path[256];
	int data_socket;
	int temp_socket;
	int status = OK;
	int msg_size;	        //размер сообщения
	
	if((status = get_data_socket(&temp_socket)) != OK)
		return status;
	data_socket = accept(temp_socket, NULL, NULL);
	close(temp_socket);

	strcpy(file_path, "downloads/");
	strcat(file_path, file_name);
	file = fopen(file_path, "wb");	
	if(file != NULL) 
	{ 
		while((msg_size = recv(data_socket, buff, sizeof(buff), 0))) 
		{
			if(msg_size == -1 || msg_size == 0) 
				break;

			fwrite(buff, sizeof(char), msg_size, file);
			fflush(file);
			memset(buff, '\0', sizeof(buff));
		}
		fclose(file);
	} 	
	close(data_socket);

	if(msg_size == -1)
		remove(file_path);
	else
		file_info(file_path, file_name, 1);

	return status;
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

void print_message(char *message, int msg_size)
{
	int i = 0;
	char buffer[4];

	strncpy(buffer, message, 3);

	if (strcmp(buffer, "cmd") == 0)
		printf("LOG: ");

	for(i = 0; i < msg_size; i++)                      //вывод сообщения
		printf("%c", message[i]);
	printf("\n");
}
