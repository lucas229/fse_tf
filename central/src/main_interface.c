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
    endwin();
    if(sockfd != -1) {
        close(sockfd);
    }
    pthread_cancel(client_daemon);
    pthread_cancel(menu_tid);
    pthread_cancel(freq_tid);
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
        printw("Menu inicial\n\n");
        printw("[1] Cadastrar dispositivo\n");
        printw("[2] Gerenciar cômodos\n");
        printw("\n[q] Finalizar\n");

        int command = getch();
        if(command != ERR) {
            if(command == '1') {
                menu_register();
            } else if(command == '2') {
                room_menu();
            } else if(command == 'q') {
                exit_server();
            }
        }
        refresh();
    }
    return NULL;
}

void room_menu(){
    while(1){
        int room;
        do {
            room = room_selection_menu();
            if(room == -1) {
                return;
            }    
        } while(room == -2);
        
        int count = 0;
        Device *list[MAX_DEVICES];
        for(int i = 0; i < devices_size; i++) {
            if(devices[i].room == room) {
                list[count++] = &devices[i];
            }
        }
        while(1) {
            erase();
            printw("Cômodo: %s\n\n", rooms[room]);
            float temperature = find_data(room, 't'), humidity = find_data(room, 'h');
            if(temperature > 0) {
                printw("Temperatura: %.1f ºC\n", temperature);
                printw("Umidade: %.1f %%\n\n", humidity);
            }
            if(count == 0) {
                printw("Não há dispositivos nesse cômodo\n\n");
            }
            for(int i = 0; i < count; i++) {
                printw("[%d] %s\n", i, list[i]->name);
            }

            printw("\n[q] Voltar\n");

            int command = getch();
            if(command != ERR) {
                if(command - '0' >= 0 && command  - '0' < count) {
                    device_menu(list[command - '0']);
                } else if(command == 'q') {
                    break; 
                }
            }
            refresh();
        }
        erase();
    }
}

void device_menu(Device *dev) {
    
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_RED, -1);

    while(1) {
        erase();
        if(dev->status) {
            attron(COLOR_PAIR(1));
            printw("Estado atual: Ligado\n");
        } else {
            attron(COLOR_PAIR(2));
            printw("Estado atual: Desligado\n");
        }
        attroff(COLOR_PAIR(1));
        attroff(COLOR_PAIR(2));
        print_device(dev->id);
        printw("\n[a] Ligar/Desligar dipositivo\n");
        printw("[r] Remover dispositivo\n");
        printw("[q] Voltar\n");
        int command = getch();
        if(command != ERR) {
            if(command == 'a') {
                change_status(dev);
            } else if(command == 'q') {
                break; 
            }
        }
        refresh();
    }
    erase();
}

int room_selection_menu() {
    int choice = -1;
    while(1) {
        erase();
        printw("Selecione um cômodo:\n\n");
        for(int i = 0; i < rooms_size; i++) {
            printw("[%d] %s\n", i, rooms[i]);
        }
        printw("\n[n] Novo cômodo\n");
        printw("[q] Voltar\n");
        refresh();
        choice = getch();
        if(choice != ERR) {
            if(choice == 'n') {
                new_room_menu();
                return -2;
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
    printw("Cadastrando novo cômodo...\n\n");
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

void menu_register() {   
    int choice; 

    while(1) {
        erase();
        int count = 0;
        printw("Selecione um dispositivo para cadastrar:\n\n");
        for(int i = 0; i < queue_size; i++) {
            if(find_device_by_mac(devices_queue[i]) == -1) {
                count++;
                printw("[%d] MAC: %s\n", i, devices_queue[i]);
            }
        }
        if(count == 0){
            printw("Não há dispositivos aguardando cadastro.\n");
        }
        printw("\n[q] Voltar\n");
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
    int room_id = room_selection_menu();    
    if(room_id == -1){
        return; 
    } else if(room_id == -2){
        room_id = rooms_size - 1;
    }
    timeout(-1);
    erase();
    refresh();
    echo();
    curs_set(1);
    printw("Cadastrando dispositivo...\n\n");
    printw("Nome do dispositivo: ");
    char device_name[25];
    getstr(device_name);

    noecho();
    curs_set(0);
    erase();
    int esp_mode;
    while(1) {
        erase();
        printw("Selecione o modo de operação do dispositivo:\n\n");
        printw("[%d] Energia\n", ENERGY_ID);
        printw("[%d] Bateria\n", BATTERY_ID);
        refresh();
        esp_mode = getch() - '0';
        if(esp_mode == ENERGY_ID || esp_mode == BATTERY_ID) {
            break;
        } 
    }

    register_device(rooms[room_id], devices_queue[choice - '0'], device_name, esp_mode);
    
    erase();
    printw("Dispositivo cadastrado com sucesso\n\n");
    print_device(devices[devices_size - 1].id);
    printw("\nAperte qualquer tecla para continuar...\n");
    getch();

    refresh();
    timeout(1000);
}

void register_device(char *room_name, char* device_id, char *device_name, int esp_mode) {
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
    devices[devices_size].room = find_room_by_name(room_name);
    strcpy(devices[devices_size].name, device_name);
    devices[devices_size].mode = esp_mode;
    devices[devices_size].is_active = 1;
    devices_size++;
}


void change_status(Device *dev){
    char topic[100];

    sprintf(topic, "fse2021/180113861/dispositivos/%s", dev->id);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "type", cJSON_CreateString("status"));
    cJSON_AddItemToObject(root, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(root, "status", cJSON_CreateNumber(!dev->status));
    dev->status = !dev->status;
    char *text = cJSON_Print(root);

    mqtt_publish(&client, topic, text, strlen(text), MQTT_PUBLISH_QOS_0);

    free(text);
    cJSON_Delete(root);
}

void print_device(char* mac_addr){
    int i = find_device_by_mac(mac_addr);
    printw("MAC: %s\n", devices[i].id);
    printw("Dispositivo: %s\n", devices[i].name);
    if(devices[i].mode == ENERGY_ID){
        printw("Tipo: Energia\n");
    } else if(devices[i].mode == BATTERY_ID){
        printw("Tipo: Bateria\n");
    }
    printw("Cômodo: %s\n", rooms[devices[i].room]);
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

float find_data(int room, char type) {
    float data = 0;
    int count = 0;
    for(int i = 0; i < devices_size; i++) {
        if(devices[i].room == room && devices[i].mode == ENERGY_ID) {
            if(devices[i].temperature == 0) {
                continue;
            }
            if(type == 't') {
                data += devices[i].temperature;
            } else if(type == 'h') {
                data += devices[i].humidity;
            }
            count++;
        }
    }
    if(count == 0){
        return 0;
    }
    return data/count; 
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
