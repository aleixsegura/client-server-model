#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>


/* Given variables to control the client-server execution. */
#define T 1 
#define P 2 
#define Q 3 
#define CONSTANT_TIME Q * T  
#define U 2 
#define N 6 
#define O 2 
#define R 2 
#define CONSECUTIVE_ALIVES 3      
#define W 3 
#define DEFAULT_STR_SIZE 100

/* Boolean variables to control some executions. */
bool first_reg = true;
bool debug_option = false;
bool send_alives_opt = true;
bool sent_file = false;

/* Structure that represents the current client. */
struct Client{
    char id [7];
    char mac [13];
    char sv_addr [20];
    int UDP_port;
    char client_state [30];
    int reg_attempts;
};

struct Client client;

/* Structure that will represent a UDP Package. */
struct UDP_Package{
    unsigned char type;
    char id [7];
    char mac [13];
    char random [7];
    char data [50];
};

struct UDP_Package register_package;
struct UDP_Package alive_package;
struct UDP_Package server_package_reg;

/* Structure that will represent a TCP Package.*/
struct TCP_Package{
    unsigned char type;
    char id [7];
    char mac [13];
    char random [7];
    char data [150];
};

struct TCP_Package request_package;
struct TCP_Package data_package;
struct TCP_Package end_package;
struct TCP_Package request_gf_package;

/* Rest of global variables used. */
char software_file [20] = "client.cfg";
char ntwk_file [20] = "boot.cfg";
int TCP_port;
int pending_socket;
pthread_t alives_thread;

/* Function declarations. */
void handle_parameters(int argc, char *argv[]);
void wait_input_commands();
char* get_input_command();
char* get_time();
void read_client();
void change_show_client_state(char *state);
void build_register_package();
void build_alive_package();
void register_phase();
void* send_alives();
struct UDP_Package get_server_package(int socket, struct sockaddr_in *sv_addr, int wait_time);
struct TCP_Package get_server_package_TCP(int socket, struct sockaddr_in *sv_addr);
bool is_valid_PDU(struct UDP_Package server_package);
bool is_valid_PDU_TCP(struct TCP_Package package);
char* get_PDU_type(int value);
void send_cfg_file();
void build_send_file_package(FILE *ntwk_file);
void build_end_package();
long int get_file_size(FILE *ntwk_file);
void build_dataline_package(char *line);
void get_cfg_file();
void build_get_file_package();
void end_client();
bool is_valid_sf(char software_file []);
bool is_valid_ntwk_file(char ntwk_file []);

/* Main program. */
int main(int argc, char *argv[]){
    handle_parameters(argc, argv);
    read_client();
    register_phase();
    pthread_create(&alives_thread, NULL, send_alives, NULL);
    wait_input_commands();
    pthread_join(alives_thread, NULL);
    return 0;
}

/* Function that implements the register phase which tries to register the current client to the server. */
void register_phase(){
    if(debug_option)
        printf("%s MSG => Starting register phase. Client will send [REGISTER_REQ] to contact the server.\n", get_time()); 
    
    build_register_package();
    first_reg = false;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0 ){
        printf("%s MSG => Unable to create the socket.\n", get_time());
        exit(-1);
    }
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(0);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int binding = bind(sock, (struct sockaddr *) &client_addr, sizeof(struct sockaddr_in));
    if (binding < 0){
        printf("%s MSG => Unable to bind the socket.\n", get_time());
        exit(-1);
    }

    struct sockaddr_in sv_addr;
    memset(&sv_addr, 0, sizeof(struct sockaddr_in));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = htons(2023);
    sv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    
    int wait_pckg_time = T;
    while (client.reg_attempts < O){
        for (int packs_sent = 0; packs_sent < N; packs_sent++){
            int package_bytes = sendto(sock, &register_package, sizeof(register_package), 0, (struct sockaddr *) &sv_addr, sizeof(sv_addr));
            change_show_client_state("WAIT_REG_RESPONSE");
            if (package_bytes < 0){
                printf("%s MSG => Unable to send the register package petition to the server.\n", get_time());
                exit(-1);
            }
            server_package_reg = get_server_package(sock, &sv_addr, wait_pckg_time);
            if(strcmp(get_PDU_type(server_package_reg.type), "ERROR") == 0)
                printf("%s MSG => No response from server.\n", get_time());
            
            if (strcmp(get_PDU_type(server_package_reg.type), "REGISTER_REJ")  == 0){
                printf("%s MSG => Client register denied, received ERROR: %s.\n", get_time(), server_package_reg.data);
                change_show_client_state("DISCONNECTED");
                exit(-1);
            }
            else if (strcmp(get_PDU_type(server_package_reg.type), "REGISTER_NACK") == 0){
                if (debug_option)
                    printf("%s MSG => Client register not accepted, received ERROR: %s.\n", get_time(), server_package_reg.data);
                break;
            }
            else if (strcmp(get_PDU_type(server_package_reg.type), "REGISTER_ACK") == 0){
                change_show_client_state("REGISTERED");
                client.reg_attempts = 0;
                if (debug_option)
                    printf("%s MSG => Achieved REGISTERED status after sending %i [REGISTER_REQ] packages.\n", get_time(), packs_sent + 1);
                strcpy(alive_package.random, server_package_reg.random);
                TCP_port = atoi(server_package_reg.data);
                close(sock); 
                return;
            }
            if (packs_sent >= P && wait_pckg_time < CONSTANT_TIME)
                wait_pckg_time += T;
            sleep(wait_pckg_time);
        }
        sleep(U);
        wait_pckg_time = T;
        client.reg_attempts++;
    }
    if (client.reg_attempts == O){
        printf("%s MSG => Unable to reach the server after %i attemps.\n", get_time(), O);
        exit(-1);
    }
}

/* Function that is reponsible of periodically sending ALIVES each R seconds to the server. It's executed by a thread. */
void* send_alives(){
    build_alive_package();
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    pending_socket = sock;
    if (sock < 0){
        printf("%s MSG => Unable to create the socket.\n", get_time());
        exit(-1);
    }
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(0);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int binding = bind(sock, (struct sockaddr *) &client_addr, sizeof(struct sockaddr_in));
    if (binding < 0){
        printf("%s MSG => Unable to bind the socket.\n", get_time());
        exit(-1);
    }
    
    struct sockaddr_in sv_addr;
    memset(&sv_addr, 0, sizeof(struct sockaddr_in));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = htons(2023);
    sv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    bool first_alive = true;
    
    while(1){
        if (!send_alives_opt){
            if (debug_option)
                printf("%s MSG => Thread for sending ALIVES finalizing...\n", get_time());
            pthread_exit(NULL);
            break;
        }
        
        int no_recept_confirm = 0;
        while (no_recept_confirm < CONSECUTIVE_ALIVES){
            int package_bytes = sendto(sock, &alive_package, sizeof(alive_package), 0, (struct sockaddr *) &sv_addr, sizeof(sv_addr));
            if (package_bytes < 0)
                exit(-1);
            struct UDP_Package server_package = get_server_package(sock, &sv_addr, R);
            if (strcmp(get_PDU_type(server_package.type), "ALIVE_ACK") == 0 && is_valid_PDU(server_package)){
                no_recept_confirm = 0;
                if (first_alive){
                    change_show_client_state("ALIVE");
                    first_alive = false;
                }
            }
            else if (strcmp(get_PDU_type(server_package.type), "ALIVE_ACK") == 0 && !is_valid_PDU(server_package))
              printf("%s MSG => Received [ALIVE_ACK] but fields are not valid. Ignoring the package.\n", get_time());
            
            else if (strcmp(get_PDU_type(server_package.type), "ALIVE_NACK") == 0)
                no_recept_confirm++;
            
            else if (strcmp(get_PDU_type(server_package.type), "ALIVE_REJ") == 0 && strcmp(client.client_state, "ALIVE") == 0){
                if (debug_option)
                    printf("%s MSG => Identity fraud detected!.\n", get_time());
                change_show_client_state("DISCONNECTED");
                register_phase();
            }
            else if (strcmp(get_PDU_type(server_package.type), "ALIVE_REJ") == 0 && strcmp(client.client_state, "ALIVE") != 0){
                if (debug_option)
                    printf(" %s MSG => Warning! Trying to force client response to insert information.\n", get_time());
                change_show_client_state("DISCONNECTED");
                register_phase();
            }
            else if (strcmp(get_PDU_type(server_package.type), "ERROR") == 0)
              no_recept_confirm++;
              
            sleep(R);
        }
        if (no_recept_confirm == CONSECUTIVE_ALIVES){
            if (debug_option)
                printf("%s MSG => Bad communications with the server.\n", get_time());
            change_show_client_state("DISCONNECTED");
            register_phase();
        }
    }
}

/* Function that checks if a UDP package received from the server is valid or not. For UDP communication. */
bool is_valid_PDU(struct UDP_Package server_package){
    return strcmp(server_package.id, server_package_reg.id) == 0 && strcmp(server_package.mac, server_package_reg.mac) == 0 && 
            strcmp(server_package.random, server_package_reg.random) == 0;
}

/* Function that receives the server package. For UDP communication. */
struct UDP_Package get_server_package(int socket, struct sockaddr_in *sv_addr, int wait_time){
    struct UDP_Package server_package;
    fd_set reads_fd_set;
    FD_ZERO(&reads_fd_set);
    FD_SET(socket, &reads_fd_set);
    struct timeval timeout;
    timeout.tv_sec = wait_time;
    timeout.tv_usec = 0;
    int sel = select(socket + 1, &reads_fd_set, NULL, NULL, &timeout);
    if (sel < 0){
        printf("%s MSG => select() error: %s\n.", get_time(), strerror(errno));
        exit(-1);
    }
    if (sel == 0){
      server_package.type = 0x0F;
      strcpy(server_package.id, "0");
      strcpy(server_package.mac, "0");
      strcpy(server_package.random, "0");
      strcpy(server_package.data, "0");
      return server_package;
    }
    if (FD_ISSET(socket, &reads_fd_set)){
        socklen_t server_addr_l = sizeof(struct sockaddr_in);
        if (recvfrom(socket, &server_package, sizeof(server_package), 0, (struct sockaddr *) sv_addr, &server_addr_l) < 0){ 
            printf("%s MSG => Unable to receive the server package.\n", get_time());
        }
    }
    return server_package;
}

/* Function to update and show the client state. */
void change_show_client_state(char *state){
    strcpy(client.client_state, state);
    printf("%s MSG => Current client state: %s.\n", get_time(), client.client_state);
}

/* Function to check if the user has introduced debug mode(-d), a different software configuration file(-c) or network configuration file(-f). */
void handle_parameters(int argc, char *argv[]){
    for (int i = 0; i < argc; i++){
        if (strcmp(argv[i], "-d") == 0){
            printf("-----ENTERED DEBUG MODE-----\n");
            debug_option = true;
        }
        if (strcmp(argv[i], "-c") == 0 && is_valid_sf(argv[i + 1]))
            strcpy(software_file, argv[i + 1]);
        if (strcmp(argv[i], "-f") == 0 && is_valid_ntwk_file(argv[i + 1]))
            strcpy(ntwk_file, argv[i + 1]);       
    }
}

/* Function that returns the current time. */
char* get_time(){
    time_t c_time;
    struct tm *tm;
    static char current_time[50];
    
    c_time = time(NULL);
    tm = localtime(&c_time);
    strftime(current_time, sizeof(current_time), "%H:%M:%S:", tm);
   
    return current_time;
}

/* Function to check if the introduced software file with (-c) is valid or not. If it's not valid, default config will be used. */
bool is_valid_sf(char software_file []){
   if (strcmp(software_file, "client1.cfg") == 0 || strcmp(software_file, "client2.cfg") == 0 
    || strcmp(software_file, "client3.cfg") == 0 || strcmp(software_file, "client4.cfg") == 0 
    || strcmp(software_file, "client5.cfg") == 0 || strcmp(software_file, "client9.cfg") == 0)
        return true;
    printf("%s MSG => Invalid Software file. Finalizing current client.\n", get_time());
    exit(-1);
}

/* Function to check if the introduced network file with (-f) is valid or not. If it's not valid, default config will be used. */
bool is_valid_ntwk_file(char ntwk_file []){
    if(strcmp(ntwk_file, "boot1.cfg") == 0 || strcmp(ntwk_file, "boot2.cfg") == 0 
            || strcmp(ntwk_file, "boot3.cfg") == 0)
        return true;
    printf("%s MSG => Invalid Network file. Finalizing current client.\n", get_time()); 
    exit(-1);
}

/* Function that reads the client data which is inside the software file. */
void read_client(){
    client.reg_attempts = 0;
    FILE *client_data;
    client_data = fopen(software_file, "r");
    if (client_data == NULL){
        printf("%s MSG => Software file: %s could not be read.\n", get_time(), software_file);
        exit(-1);
    }
    char line [50];

    while(fgets(line, 50, client_data) != NULL){
        line[strcspn(line, "\n")] = '\0';
        char *separator = strchr(line, ' ');
        char key [20];
        char value [20];
        memcpy(key, line, separator - line);
        key[separator - line] = '\0';
        strcpy(value, separator + 1);
        value[strcspn(value, "\n")] = '\0';

        if (strcmp(key, "Id") == 0){
            strcpy(client.id, value);
        }
        else if (strcmp(key, "MAC") == 0){
            strcpy(client.mac, value);
        }
        else if (strcmp(key, "NMS-Id") == 0){
            strcpy(client.sv_addr, value);
        }
        else if (strcmp(key, "NMS-UDP-port") == 0){
            client.UDP_port = atoi(value);
        }
    }
    change_show_client_state("DISCONNECTED");
    fclose(client_data);
}

/* Function that builds the next register package to send to the server. Random is 000000 for first package 
    or the received random if already sent one register package.*/
void build_register_package(){
    register_package.type = 0x00;
    strcpy(register_package.id, client.id);
    strcpy(register_package.mac, client.mac);
    if (first_reg){
        strcpy(register_package.random, "000000");
    }else{
        strcpy(register_package.random, server_package_reg.random);
    }
    strcpy(register_package.data, "");
}

/* Function that builds the alive package to send to the server. */
void build_alive_package(){
    alive_package.type = 0x10;
    strcpy(alive_package.id, client.id);
    strcpy(alive_package.mac, client.mac);
    strcpy(alive_package.data, "");
}

/* Function that controls and awaits for CLI commands introduced by the user. */
void wait_input_commands(){
    if (debug_option){
        printf("\n");
        printf("CLI waiting for input commands.\n");
        printf("Options are: \n");
        printf("1) send-cfg to send the boot configuration file.\n");
        printf("2) get-cfg to get the boot configuration file.\n");
        printf("3) quit to end the client.\n");
    }
    while (1){
        char command [DEFAULT_STR_SIZE];
        strcpy(command, get_input_command());
        if (strcmp(command, "send-cfg") == 0)
            send_cfg_file();
        else if (strcmp(command, "get-cfg") == 0)
            get_cfg_file();
        else if (strcmp(command, "quit") == 0)
            end_client();
        else
            printf("%s MSG => %s is and invalid command!.\n", get_time(), command);
    }
}

/* Function that returns the command introduced in the CLI. */
char* get_input_command(){
    char* command = (char*) malloc(DEFAULT_STR_SIZE * sizeof(char));
    fgets(command, DEFAULT_STR_SIZE, stdin);
    command[strcspn(command, "\n")] = '\0';
    return command;
}

/* Function that sends the network config file to the server. */
void send_cfg_file(){
    printf("%s MSG => Request to send the configuration file to the server (%s).\n", get_time(), ntwk_file);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        printf("%s MSG => Unable to create the socket.\n", get_time());
        exit(-1);
    }
    struct sockaddr_in sv_addr;
    memset(&sv_addr, 0, sizeof(struct sockaddr_in));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = htons(TCP_port);
    sv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int connection = connect(sock, (struct sockaddr *) &sv_addr, sizeof(struct sockaddr_in));
    if (connection < 0){
        printf("%s MSG => Unable to set a connection to the server.\n", get_time());
        close(sock);
        exit(- 1);
    }
    FILE *boot_config;
    boot_config = fopen(ntwk_file, "r");
    if (boot_config == NULL){
        printf("%s MSG => Network configuration file: %s could not be read.\n", get_time(), ntwk_file);
        close(sock);
        exit(-1);
    }

    build_send_file_package(boot_config);
    int package_bytes = send(sock, &request_package, sizeof(request_package), 0);
    if (package_bytes < 0){
        printf("%s MSG => Unable to send the config file (%s) petition package to the server.\n", get_time(), ntwk_file);
        exit(-1);
    }
    struct TCP_Package server_package = get_server_package_TCP(sock, &sv_addr);
    if (strcmp(get_PDU_type(server_package.type), "SEND_ACK") == 0 && is_valid_PDU_TCP(server_package)){
        char line [150];
        // Each line is a SEND_DATA package.
        while(fgets(line, 150, boot_config) != NULL){
            build_dataline_package(line);
            int package_bytes = send(sock, &data_package, sizeof(data_package), 0);
            if (package_bytes < 0){
                printf("%s MSG => Unable to send the current SEND_DATA package type.\n", get_time());
                fclose(boot_config);
                close(sock);
                exit(-1);
            }
        }
        fclose(boot_config); 
        
        build_end_package();
        int end_bytes = send(sock, &end_package, sizeof(end_package), 0);
        if (end_bytes < 0){
            printf("%s MSG => Unable to send the SEND_END package type.\n", get_time());
            exit(-1);
        }
        printf("%s MSG => Configuration file (%s) sending finalized.\n", get_time(), ntwk_file);
        sent_file = true;
        close(sock);
    }
    else if (strcmp(get_PDU_type(server_package.type), "SEND_ACK") == 0 && !is_valid_PDU_TCP(server_package)){
        if (debug_option)
            printf("%s MSG => Received package SEND_ACK but PDU is invalid. Finalizing send-cfg process.\n", get_time());
        close(sock);
        fclose(boot_config);
        return;
    }
    else if (strcmp(get_PDU_type(server_package.type) , "ERROR") == 0){
        close(sock);
        fclose(boot_config);
    }
    else{
        printf("%s MSG => Received package type 0x%x(%s) from the server. Client closing socket.\n", get_time(), server_package.type, get_PDU_type(server_package.type));
        close(sock);
        fclose(boot_config);
    }
}

/* Function that builds the SEND_END package to send to the server */
void build_end_package(){
    end_package.type = 0x2A;
    strcpy(end_package.id, client.id);
    strcpy(end_package.mac, client.mac);
    strcpy(end_package.random, server_package_reg.random);
    strcpy(end_package.data, "");
}

/* Function that recieves the server package. For TCP communication. */
struct TCP_Package get_server_package_TCP(int socket, struct sockaddr_in *sv_addr){
    struct TCP_Package server_package;
    fd_set reads_fd_set;
    FD_ZERO(&reads_fd_set);
    FD_SET(socket, &reads_fd_set);
    struct timeval timeout;
    timeout.tv_sec = W;
    timeout.tv_usec = 0;

    int sel = select(socket + 1, &reads_fd_set, NULL, NULL, &timeout);
    if (sel < 0){
        printf("%s MSG => select() error: %s\n.", get_time(), strerror(errno));
        exit(-1);
    }
    if (sel == 0){
      if (debug_option)
        printf("%s MSG => Bad communications. Closing TCP channel.\n", get_time());
      server_package.type = 0x0F;
      strcpy(server_package.id, "0");
      strcpy(server_package.mac, "0");
      strcpy(server_package.random, "0");
      strcpy(server_package.data, "0");
      return server_package;
    }
    if (FD_ISSET(socket, &reads_fd_set)){
        int rec = recv(socket, &server_package, sizeof(server_package), 0);
        if (rec < 0){ 
            printf("%s MSG => Unable to receive the server package.\n", get_time());
            exit(-1);
        }
    }
    return server_package;
}

/* Function that builds the SEND_FILE package. */
void build_send_file_package(FILE *config_file){
    request_package.type = 0x20;
    strcpy(request_package.id, client.id);
    strcpy(request_package.mac, client.mac);
    strcpy(request_package.random, server_package_reg.random);
    long int ntwk_file_size = get_file_size(config_file);
    char buffer [50];
    snprintf(buffer, sizeof(buffer), "%s,%ld", ntwk_file, ntwk_file_size);
    strcpy(request_package.data, buffer);
}

/* Function that returns the size in bytes of the network config file. */
long int get_file_size(FILE *config_file){
   fseek(config_file, 0, SEEK_END);
   long int size = ftell(config_file);
   fseek(config_file, 0, SEEK_SET);
   return size;
}

/* Function that builds the current SEND_DATA package to be sent to the server. */
void build_dataline_package(char *line){
    data_package.type = 0x22;
    strcpy(data_package.id, client.id);
    strcpy(data_package.mac, client.mac);
    strcpy(data_package.random, server_package_reg.random);
    strcpy(data_package.data, line);
}

/* Function that gets back the configuration file. */
void get_cfg_file(){
    printf("%s MSG => Request to get the configuration file from the server (%s).\n", get_time(), client.id);
    if (!sent_file){
        if (debug_option)
            printf("%s MSG => Can't do get-cfg without previous send-cfg.\n", get_time());
        return;
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        printf("%s MSG => Unable to create the socket.\n", get_time());
        exit(-1);
    }
    struct sockaddr_in sv_addr;
    memset(&sv_addr, 0, sizeof(struct sockaddr_in));
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = htons(TCP_port);
    sv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int connection = connect(sock, (struct sockaddr *) &sv_addr, sizeof(struct sockaddr_in));
    if (connection < 0){
        printf("%s MSG => Unable to set a connection to the server.\n", get_time());
        exit(- 1);
    }
    build_get_file_package();
    int package_bytes = send(sock, &request_gf_package, sizeof(request_gf_package), 0);
    if (package_bytes < 0){
        printf("%s MSG => Unable to send the configuration file (%s) petition package to the server.\n", get_time(), ntwk_file);
        exit(-1);
    }

    struct TCP_Package server_package = get_server_package_TCP(sock, &sv_addr);
    if (strcmp(get_PDU_type(server_package.type), "GET_ACK") == 0){
        FILE *boot_config;
        boot_config = fopen(ntwk_file, "w");
        if (boot_config == NULL){
            printf("%s MSG => Network configuration file: %s could not be read.\n", get_time(), ntwk_file);
            exit(-1);
        }
        while (strcmp(get_PDU_type(server_package.type), "GET_END") != 0){
            server_package = get_server_package_TCP(sock, &sv_addr);
            if (strcmp(get_PDU_type(server_package.type), "GET_DATA")  == 0)
                fputs(server_package.data, boot_config);  
            if (strcmp(get_PDU_type(server_package.type), "ERROR") == 0){
                if (debug_option)
                  printf("%s MSG => Didn't receive final GET_END or current GET_DATA package.\n", get_time());
                break;
            }
        }
        fclose(boot_config);
        close(sock);
        printf("%s MSG => Configuration file (%s) received.\n", get_time(), client.id);
    }

    
    else{
        printf("%s MSG => Received package type 0x%x(%s) from the server. Client closing socket.\n", get_time(), server_package.type, get_PDU_type(server_package.type));
        close(sock);
    }
}

/* Function that checks if the received package is valid or not. For TCP communication. */
bool is_valid_PDU_TCP(struct TCP_Package package){
    return strcmp(package.id, server_package_reg.id) == 0 && strcmp(package.mac, server_package_reg.mac) == 0 && strcmp(package.random, server_package_reg.random) == 0;
}

/* Function to build a GET_FILE package. */
void build_get_file_package(){
    request_gf_package.type = 0x30;
    strcpy(request_gf_package.id, client.id);
    strcpy(request_gf_package.mac, client.mac);
    strcpy(request_gf_package.random, server_package_reg.random);
    strcpy(request_gf_package.data, "");
}

/* Function that simply ends the client if quit command has been introduced. */
void end_client(){
    if (debug_option)
        printf("%s MSG => Finalizing client...\n", get_time());
    send_alives_opt = false;
    close(pending_socket);
    sleep(1);
    exit(0);
}

/* Function that returns the string type assigned to hex type value. */
char* get_PDU_type(int value){
    if (value == 0x02)
        return "REGISTER_ACK";
    else if (value == 0x04)
        return "REGISTER_NACK";
    else if (value == 0x06)
        return "REGISTER_REJ";
    else if (value == 0x0F)
        return "ERROR";
    else if (value == 0x12)
        return "ALIVE_ACK";
    else if (value == 0x14)
        return "ALIVE_NACK";
    else if (value == 0x16)
        return "ALIVE_REJ";
    else if (value == 0x24)
        return "SEND_ACK";
    else if (value == 0x26)
        return "SEND_NACK";
    else if (value == 0x28)
        return "SEND_REJ";
    else if (value == 0x32)
        return "GET_DATA";
    else if (value == 0x34)
        return "GET_ACK";
    else if (value == 0x36)
        return "GET_NACK";
    else if (value == 0x38)
        return "GET_REJ";
    else if (value == 0x3A)
        return "GET_END";
    else
        return NULL;
}
