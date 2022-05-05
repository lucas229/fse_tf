#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ncurses.h>
#include <ctype.h>

#include "posix_sockets.h"
#include "cJSON.h"
#include "main_interface.h"
#include "logger.h"

Device devices[MAX_DEVICES];
int devices_size = 0;

char rooms[MAX_ROOMS][50];
int rooms_size = 0;
char devices_queue[MAX_DEVICES][20];
int queue_size = 0;

struct mqtt_client client;
int sockfd = -1;
int alarm_status = 0;
pthread_t menu_tid, client_daemon, freq_tid, alarm_tid;

void init_server() {

    init_file();
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

    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_RED, -1);
    init_pair(3, COLOR_YELLOW, -1);

    while(1) {
        erase();
        printw("Menu inicial\n\n");
        printw("[1] Cadastrar dispositivo\n");
        printw("[2] Gerenciar cômodos\n");
        if(alarm_status == 0) {
            attron(COLOR_PAIR(2));
        } else if(alarm_status == 1) {
            attron(COLOR_PAIR(3));
        } else {
            attron(COLOR_PAIR(1));
        }

        printw("[a] Ligar/Desligar alarme\n");

        attroff(COLOR_PAIR(1));
        attroff(COLOR_PAIR(2));
        attroff(COLOR_PAIR(3));

        printw("\n[q] Finalizar\n");

        int command = getch();
        if(command != ERR) {
            if(command == '1') {
                menu_register();
            } else if(command == '2') {
                room_menu();
            } else if(command == 'a') {
                handle_alarm_status();
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
        while(1) {
            erase();
            printw("Cômodo: %s\n\n", rooms[room]);
            float temperature = find_data(room, 't'), humidity = find_data(room, 'h');
            if(temperature > 0) {
                printw("Temperatura: %.1f ºC\n", temperature);
                printw("Umidade: %.1f %%\n\n", humidity);
            }
            
            count = 0;
            for(int i = 0; i < devices_size; i++) {
                if(devices[i].room == room) {
                    list[count++] = &devices[i];
                }
            }

            if(count == 0) {
                printw("Não há dispositivos nesse cômodo\n\n");
            } else {
                printw("Dispositivos:\n");
            }
            for(int i = 0; i < count; i++) {
                printw("[%d] %s", i, list[i]->input);
                if(list[i]->mode == ENERGY_ID) {
                    printw(" | %s", list[i]->output);
                }
                printw("\n");
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

int dimmable_menu(){
    timeout(-1);
    clear();
    echo();
    curs_set(1);

    printw("Intensidade do acionamento (0 a 255): ");
    refresh();
    char num[5];
    getstr(num);
    int number = atoi(num);
    if(number > 255){
        number = 255;
    }
    else if(number < 0){
        number = 0;
    }
    timeout(1000);
    clear();
    noecho();
    curs_set(0);
    return number; 
}

void device_menu(Device *dev) {
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
        if(dev->mode == ENERGY_ID){
            printw("\n[a] Ligar/Desligar dipositivo\n");
            printw("[r] Remover dispositivo\n");
        } else {
            printw("\n");
        }
        printw("[q] Voltar\n");
        int command = getch();
        if(command != ERR) {
            if((command == 'a' || command == 'r') && dev->mode == BATTERY_ID) {
                continue;
            }
            if(command == 'a') {
                if(dev->is_dimmable) {
                    dev->status = dimmable_menu();      
                } else {
                    dev->status = !dev->status;
                }
                change_status(dev);
                check_alarm(dev->id);                
            } else if(command == 'r'){
                handle_remove_device(dev->id);
                break;
            }
            else if(command == 'q') {
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
        if(rooms_size > 0) {
            printw("\n");
        }
        printw("[n] Novo cômodo\n");
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
        char new_topic[100];
        char command[100];
        sprintf(new_topic, "fse2021/180113861/%s/temperatura", room_name);
        mqtt_subscribe(&client, new_topic, 0);
        sprintf(new_topic, "fse2021/180113861/%s/umidade", room_name);
        mqtt_subscribe(&client, new_topic, 0);
        sprintf(new_topic, "fse2021/180113861/%s/estado", room_name);
        mqtt_subscribe(&client, new_topic, 0);

        sprintf(command, "Cadastro do cômodo %s", room_name);
        log_data("geral", command);
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
        printw("Selecione um dispositivo para cadastrar:\n\n");
        for(int i = 0; i < queue_size; i++) {            
            printw("[%d] MAC: %s\n", i, devices_queue[i]);
        }
        if(queue_size == 0){
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
    
    Device new_device;

    request_mode(devices_queue[choice - '0']);
    clear();
    printw("Iniciando o cadastro do dispositivo...\n");
    refresh();
    sleep(2);

    strcpy(new_device.id, devices_queue[choice - '0']);
    new_device.room = room_selection_menu();
    if(new_device.room == -1){
        return;
    } else if(new_device.room == -2){
        new_device.room = rooms_size - 1;
    }

    timeout(-1);
    echo();
    curs_set(1);

    clear();
    printw("Cadastrando dispositivo...\n\n");
    char input_device_name[25], output_device_name[25];
    printw("Nome do dispositivo de entrada: ");
    refresh();
    getstr(input_device_name);
    strcpy(new_device.input, input_device_name);

    if(devices[devices_size].mode == ENERGY_ID) {
        clear();
        printw("Cadastrando dispositivo...\n\n");
        printw("Nome do dispositivo de saída: ");
        refresh();
        getstr(output_device_name);
        strcpy(new_device.output, output_device_name);
    }

    noecho();
    curs_set(0);

    timeout(1000);


    new_device.trigger_alarm = get_answer_menu("A entrada aciona alarme?");

    if(devices[devices_size].mode == ENERGY_ID) {
        new_device.is_dimmable = get_answer_menu("A saída é dimerizável?");
    }
    else {
        new_device.is_dimmable = 0;
    }
    new_device.mode = devices[devices_size].mode;
    register_device(new_device);
    for(int i = choice - '0'; i < queue_size - 1; i++) {
        strcpy(devices_queue[i], devices_queue[i + 1]);
    }
    queue_size--;

    timeout(-1);
    clear();
    printw("Dispositivo cadastrado com sucesso\n\n");
    print_device(devices[devices_size - 1].id);
    printw("\nAperte qualquer tecla para continuar...\n");
    getch();

    refresh();
    timeout(1000);
}

int get_answer_menu(char *question) {
    clear();
    char answer; 
    while(1) {
        erase();
        printw("Cadastrando dispositivo...\n\n");
        printw("%s\n[1] Sim\n[0] Não\n", question);
        refresh();
        answer = getchar();
        if(answer == '1' || answer == '0'){
            break; 
        }  
    }

    return answer - '0';
}

void register_device(Device new_device) {
    char *msg;
    char msg_topic[100];

    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(item, "room", cJSON_CreateString(rooms[new_device.room]));
    cJSON_AddItemToObject(item, "input", cJSON_CreateString(new_device.input));
    cJSON_AddItemToObject(item, "output", cJSON_CreateString(new_device.output));
    cJSON_AddItemToObject(item, "trigger_alarm", cJSON_CreateNumber(new_device.trigger_alarm));
    cJSON_AddItemToObject(item, "is_dimmable", cJSON_CreateNumber(new_device.is_dimmable));

    sprintf(msg_topic, "fse2021/180113861/dispositivos/%s", new_device.id);
    msg = cJSON_Print(item);

    mqtt_publish(&client, msg_topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);
    free(msg);
    cJSON_Delete(item);

    new_device.status = 0;
    new_device.is_active = 1;
    devices[devices_size++] = new_device;

    log_data(new_device.id, "dispositivo adicionado");
}

void request_mode(char *mac) {
    char topic[100];
    sprintf(topic, "fse2021/180113861/dispositivos/%s", mac);

    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "type", cJSON_CreateString("mode"));
    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("central"));
    char *request = cJSON_Print(json);
    mqtt_publish(&client, topic, request, strlen(request), MQTT_PUBLISH_QOS_0);
    free(request);
    cJSON_Delete(json);
}

void change_status(Device *dev){
    char topic[100];

    sprintf(topic, "fse2021/180113861/%s/estado", rooms[dev->room]);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "type", cJSON_CreateString("status"));
    cJSON_AddItemToObject(root, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(root, "status", cJSON_CreateNumber(dev->status));
    
    char *text = cJSON_Print(root);

    mqtt_publish(&client, topic, text, strlen(text), MQTT_PUBLISH_QOS_0);

    if(dev->status == 0) {
        log_data(dev->id, "dispositivo desligado");
    } else {
        log_data(dev->id, "dispositivo ligado");
    }

    free(text);
    cJSON_Delete(root);
}

void print_device(char* mac_addr){
    int i = find_device_by_mac(mac_addr);
    printw("MAC: %s\n", devices[i].id);
    printw("Aciona alarme: %d\n", devices[i].trigger_alarm);
    printw("Entrada: %s\n", devices[i].input);
    if(devices[i].mode == ENERGY_ID){
        printw("Saída: %s\n", devices[i].output);
        printw("Dimerizável: %d\n", devices[i].is_dimmable);
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
        } else if(strcmp(type, "mode") == 0) {
            devices[devices_size].mode = cJSON_GetObjectItem(root, "mode")->valueint;
        } else if(strcmp(type, "reconectar") == 0) {
            handle_reconnect_request(published);
        } else if(strcmp(type, "remover") == 0) {
            remove_device(find_device_by_mac(cJSON_GetObjectItem(root, "id")->valuestring));
        } else if(strcmp(type, "status") == 0) {
            devices[find_device_by_mac(cJSON_GetObjectItem(root, "id")->valuestring)].status = cJSON_GetObjectItem(root, "status")->valueint;
                if(cJSON_GetObjectItem(root, "status")->valueint == 0) {
                    log_data(cJSON_GetObjectItem(root, "id")->valuestring, "dispositivo desligado");
                } else {
                    log_data(cJSON_GetObjectItem(root, "id")->valuestring, "dispositivo ligado");
                }
            check_alarm(cJSON_GetObjectItem(root, "id")->valuestring);
        } else if(strcmp(type, "frequencia") != 0){
            handle_device_data(published, type);
        } else {
            devices[find_device_by_mac(cJSON_GetObjectItem(root,"id")->valuestring)].is_active = 1;
        }
    }

    cJSON_Delete(root);
}

void *start_alarm(void *args) {
    while(alarm_status == 2) {
        beep();
        sleep(2);
    }
    return NULL;
}

void handle_alarm_status(){
    if(alarm_status != 0) {
        if(alarm_status == 2) {
            alarm_status = 0;
            pthread_join(alarm_tid, NULL);
        } else if(alarm_status == 1) {
            alarm_status = 0;
        }
        log_data("geral", "desativa alarme");
        return;
    }
    int allow_enable = 1;
    for(int i=0; i<devices_size; i++){
        if(devices[i].trigger_alarm && devices[i].status){
            allow_enable = 0;
            break;
        }
    }
    if (allow_enable){
        alarm_status = 1;        
        log_data("geral", "habilita alarme");
    }
}


void check_alarm(char *mac) {
    int index = find_device_by_mac(mac);
    if(devices[index].trigger_alarm && devices[index].status) {
        if(alarm_status == 1) {
            alarm_status = 2;
            log_data("geral", "dispara alarme");
            pthread_create(&alarm_tid, NULL, start_alarm, NULL);
        }
    }
}

void handle_reconnect_request(struct mqtt_response_publish *published) {
    cJSON *json = cJSON_Parse(published->application_message);

    Device device;
    strcpy(device.id, cJSON_GetObjectItem(json, "id")->valuestring);
    strcpy(device.input, cJSON_GetObjectItem(json, "input")->valuestring);
    strcpy(device.output, cJSON_GetObjectItem(json, "output")->valuestring);
   
    device.room = find_room_by_name(cJSON_GetObjectItem(json, "room")->valuestring);
    device.mode = cJSON_GetObjectItem(json, "mode")->valueint;
    device.trigger_alarm = cJSON_GetObjectItem(json, "trigger_alarm")->valueint;
    device.is_dimmable = cJSON_GetObjectItem(json, "is_dimmable")->valueint;
    device.status = 0;
    device.is_active = 1;
    devices[devices_size++] = device;

    log_data(device.id, "dispositivo reconectado");

    cJSON_Delete(json);
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

    for(int i = 0; i < queue_size; i++) {
        if(strcmp(devices_queue[i], device_mac) == 0) {
            cJSON_Delete(root);
            return;
        }
    }

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
            if(devices[i].mode == ENERGY_ID) {
                if(devices[i].is_active == 2) {
                    remove_device(find_device_by_mac(devices[i].id));
                }
                sprintf(topic, "fse2021/180113861/dispositivos/%s", devices[i].id);
                devices[i].is_active = 2;
                mqtt_publish(&client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);    
            }
        }
        sleep(10);
    }

    free(message);
    cJSON_Delete(item);
}

void handle_remove_device(char *mac_addr) {
    char topic[100];
    int index = find_device_by_mac(mac_addr);
    sprintf(topic, "fse2021/180113861/dispositivos/%s", mac_addr);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "type", cJSON_CreateString("remover"));
    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("central"));
    char *message = cJSON_Print(json);
    mqtt_publish(&client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);
    
    cJSON_Delete(json);
    free(message);

    remove_device(index);
}

void remove_device(int index) {
    log_data(devices[index].id, "dispositivo removido");
    
    for(int i = index; i < devices_size - 1; i++) {
        devices[i] = devices[i + 1];
    }
    devices_size--;
}

void* client_refresher(void *client) {
    while(1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}