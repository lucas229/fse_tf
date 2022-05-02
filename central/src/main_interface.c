#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ncurses.h>

#include "posix_sockets.h"
#include "cJSON.h"
#include "main_interface.h"

Device devices[MAX_DEVICES];
int devices_size = 0;

char rooms[MAX_ROOMS][50];
int rooms_size = 0;
char devices_queue[MAX_DEVICES][20];
int queue_size = 0;

struct mqtt_client client;
int sockfd = -1;
pthread_t menu_tid, client_daemon, freq_tid;

void init_server() {
    char addr[] = "test.mosquitto.org";
    char port[] = "1883";
    char topic[] = "fse2021/180113861/dispositivos/+";
    sockfd = open_nb_socket(addr, port);
    init_client(topic);

    pthread_create(&freq_tid, NULL, check_frequence, NULL);
    pthread_create(&menu_tid, NULL, init_menu, NULL);

    pthread_join(freq_tid, NULL);
    pthread_join(menu_tid, NULL);
}

void exit_server() {
    if(sockfd != -1) {
        close(sockfd);
    }
    pthread_cancel(client_daemon);
    pthread_cancel(menu_tid);
    pthread_cancel(freq_tid);
    endwin();
    exit(0);
}


void init_client(const char *topic) {
    if(sockfd == -1) {
        perror("Failed to open socket: ");
        exit_server();
    }

    static uint8_t sendbuf[2048];
    static uint8_t recvbuf[1024];
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    const char* client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    if(client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_server();
    }

    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_server();
    }
    mqtt_subscribe(&client, topic, 0);
}

void *init_menu(void *args) {
    initscr();
    curs_set(0);
    noecho();
    timeout(1000);

    while(1) {
        erase();
        printw("[0] Gerenciar dispostivos\n");
        printw("[1] Cadastrar dispositivo\n");
        printw("[2] Cadastrar cômodo\n");

        int command = getch();
        if(command != ERR) {
            if(command == '0') {
                show_status_menu();
            }
            else if(command == '1') {
                menu_register();
            } else if(command == '2') {
                room_menu();
            } else if(command == 'q') {
                break;
            }
        }
        refresh();
    }
    return NULL;
}

int room_menu() {
    int choice = -1;
    while(1) {
        erase();
        for(int i = 0; i < rooms_size; i++) {
            printw("[%d] %s\n", i, rooms[i]);
        }
        printw("[n] Novo cômodo");
        refresh();
        choice = getch();
        if(choice != ERR) {
            if(choice == 'n') {
                new_room_menu();
                return rooms_size - 1;
            } else if(choice == 'q') {
                return -1;
            } else if(choice - '0' < rooms_size){
                break;
            }
        }
    }
    return choice - '0';
}

void new_room_menu() {
    timeout(-1);
    erase();
    echo();
    curs_set(1);
    printw("Nome do cômodo: ");
    char room_name[25];
    getstr(room_name);
    if(find_room_by_name(room_name) == -1){
        strcpy(rooms[rooms_size++], room_name);
    }
    noecho();
    curs_set(0);
    refresh();
    timeout(1000);
}

void show_status_menu() {
    while(1) {
        erase();
        for(int i = 0; i < devices_size; i++) {
            printw("[%d] %s\n", i + 1, devices[i].id);
            // printw("Temperatura: %.1f ºC\n", devices[i].temperature);
            // printw("Umidade: %.1f %%\n", devices[i].humidity);
            // printw("Estado: %d\n", devices[i].status);
            // printw("Ativo: %d\n\n", devices[i].is_active);
        }
        refresh();
    }
}

void menu_register() {   
    int choice; 

    while(1) {
        erase();
        int count = 0;
        for(int i = 0; i < queue_size; i++) {
            if(find_device_by_mac(devices_queue[i]) == -1) {
                count++;
                printw("[%d] %s\n", i, devices_queue[i]);
            }
        }
        if(count == 0){
            printw("Não há dispositivos aguardando cadastro");
        }
        choice = getch();
        if(choice == 'q') {
            return;
        } else if(choice >= '0' && choice <= '9' && choice - '0' < queue_size) {
            if(find_device_by_mac(devices_queue[choice - '0']) == -1) {
                break;
            }
        }
        refresh();
    }
    int room_id = room_menu();    
    if(room_id == -1){
        return; 
    }
    timeout(-1);
    erase();
    refresh();
    echo();
    curs_set(1);
    printw("Nome do dispositivo: ");
    char device_name[25];
    getstr(device_name);

    noecho();
    curs_set(0);
    erase();
    int esp_mode;
    while(1) {
        printw("[%d] Energia\n", ENERGY_ID);
        printw("[%d] Bateria\n", BATTERY_ID);
        refresh();
        esp_mode = getch() - '0';
        if(esp_mode == ENERGY_ID || esp_mode == BATTERY_ID) {
            break;
        }
    }

    register_device(rooms[room_id], devices_queue[choice - '0'], esp_mode);

    erase();
    refresh();
    timeout(1000);
}

void register_device(char *room_name, char* device_id, int esp_mode) {
    char *msg;
    char msg_topic[50];
    char new_topic[100];

    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(item, "room", cJSON_CreateString(room_name));
    cJSON_AddItemToObject(item, "mode", cJSON_CreateNumber(esp_mode));

    sprintf(msg_topic, "fse2021/180113861/dispositivos/%s", device_id);
    msg = cJSON_Print(item);

    mqtt_publish(&client, msg_topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);
    free(msg);
    
    sprintf(new_topic, "fse2021/180113861/%s/temperatura", room_name);
    mqtt_subscribe(&client, new_topic, 0);
    sprintf(new_topic, "fse2021/180113861/%s/umidade", room_name);
    mqtt_subscribe(&client, new_topic, 0);
    sprintf(new_topic, "fse2021/180113861/%s/estado", room_name);
    mqtt_subscribe(&client, new_topic, 0);

    strcpy(devices[devices_size].id, device_id);
    devices[devices_size].status = 0;
    devices[devices_size].is_active = 1;
    devices_size++;
}


void change_status_menu(){
    for(int i=0; i<devices_size; i++){
        printf("[%d] Dispositivo %s\n", i+1, devices[i].id);
    } 

    printf("\nDigite o número do dispositivo que deseja alterar o estado\n");
    int device_number;
    scanf("%d", &device_number);
    
    char topic[100];
    sprintf(topic, "fse2021/180113861/dispositivos/%s", devices[device_number-1].id);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "type", cJSON_CreateString("status"));
    cJSON_AddItemToObject(root, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(root, "status", cJSON_CreateNumber(!devices[device_number - 1].status));
    devices[device_number - 1].status = !devices[device_number - 1].status;
    char *text = cJSON_Print(root);

    mqtt_publish(&client, topic, text, strlen(text), MQTT_PUBLISH_QOS_0);

    free(text);
    cJSON_Delete(root);
}



int find_device_by_mac(char *mac_addr){
    for(int i=0; i<devices_size; i++){
        if(strcmp(mac_addr, devices[i].id) == 0) {
            return i;
        }
    }
    return -1; 
}

int find_room_by_name(char *room_name){
    for(int i=0; i<rooms_size; i++){
        if(strcmp(room_name, rooms[i]) == 0) {
            return i;
        }
    }
    return -1; 
}

void publish_callback(void **unused, struct mqtt_response_publish *published) {
    cJSON *root = cJSON_Parse(published->application_message);
    char *type = cJSON_GetObjectItem(root, "type")->valuestring;
    char *sender = cJSON_GetObjectItem(root, "sender")->valuestring;

    if(strcmp(sender,"distribuido") == 0){
        if(strcmp(type, "cadastro") == 0) {
            handle_register_request(published);
        } else if(strcmp(type, "frequencia") != 0){
            handle_device_data(published, type);
        } else {
            devices[find_device_by_mac(cJSON_GetObjectItem(root,"id")->valuestring)].is_active = 1;
        }
    }

    cJSON_Delete(root);
}

void handle_device_data(struct mqtt_response_publish *published, char *type) {
    char message[500];
    strcpy(message, published->application_message);
    message[published->application_message_size] = '\0';
    cJSON *json = cJSON_Parse(message);

    float data = cJSON_GetObjectItem(json, type)->valueint;
    char *mac = cJSON_GetObjectItem(json, "id")->valuestring;
    int index = find_device_by_mac(mac);
    if(index == -1) {
        cJSON_Delete(json);
        return;
    }
    if(strcmp(type, "temperature") == 0) {
        devices[index].temperature = data;
    } else {
        devices[index].humidity = data;
    }

    cJSON_Delete(json);
}


void handle_register_request(struct mqtt_response_publish *published){
    cJSON *root = cJSON_Parse(published->application_message);
    char* device_mac = cJSON_GetObjectItem(root, "id")->valuestring;

    strcpy(devices_queue[queue_size++], device_mac);
    cJSON_Delete(root);
}   

void *check_frequence(void *args) {
    char topic[200];
    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("frequencia"));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("central"));

    char *message = cJSON_Print(item);
    
    while(1) {
        for(int i = 0; i < devices_size; i++) {
            sprintf(topic, "fse2021/180113861/dispositivos/%s", devices[i].id);
            devices[i].is_active = 0; 
            mqtt_publish(&client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);
        }
        sleep(5);
    }

    free(message);
    cJSON_Delete(item);
}

void* client_refresher(void *client) {
    while(1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}
