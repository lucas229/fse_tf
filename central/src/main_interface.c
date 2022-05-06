#include <unistd.h>
#include <stdlib.h>
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
pthread_t menu_tid, client_tid, freq_tid, alarm_tid;

void init_server() {
    init_file();

    char addr[] = "test.mosquitto.org";
    char port[] = "1883";
    sockfd = open_nb_socket(addr, port);
    init_client();

    pthread_create(&freq_tid, NULL, check_frequence, NULL);
    pthread_create(&menu_tid, NULL, init_menu, NULL);

    pthread_join(menu_tid, NULL);
}

void exit_server() {
    endwin();
    if(sockfd != -1) {
        close(sockfd);
    }
    pthread_cancel(client_tid);
    pthread_cancel(menu_tid);
    pthread_cancel(freq_tid);
    if(alarm_status != 0) {
        pthread_cancel(alarm_tid);
    }
    exit(0);
}

void init_client() {
    if(sockfd == -1) {
        perror("Failed to open socket.\n");
        exit_server();
    }

    static uint8_t sendbuf[4096];
    static uint8_t recvbuf[2048];
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    const char *client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    if(client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_server();
    }

    if(pthread_create(&client_tid, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_server();
    }

    mqtt_subscribe(&client, "fse2021/180113861/dispositivos/+", 0);
}

void *init_menu(void *args) {
    initscr();
    curs_set(0);
    noecho();
    timeout(1000);

    start_color();
    use_default_colors();
    init_pair(GREEN, COLOR_GREEN, -1);
    init_pair(RED, COLOR_RED, -1);
    init_pair(YELLOW, COLOR_YELLOW, -1);
    init_pair(BLUE, COLOR_BLUE, -1);
    init_pair(DEFAULT, -1, -1);

    while(1) {
        erase();
        attron(COLOR_PAIR(BLUE));
        printw("Menu inicial\n\n");

        attron(COLOR_PAIR(DEFAULT));
        printw("[C] Cadastrar dispositivo\n");
        printw("[G] Gerenciar cômodos\n");

        if(alarm_status == 0) {
            attron(COLOR_PAIR(RED));
        } else if(alarm_status == 1) {
            attron(COLOR_PAIR(YELLOW));
        } else {
            attron(COLOR_PAIR(GREEN));
        }
        printw("[A] Ligar/Desligar alarme\n");

        attron(COLOR_PAIR(DEFAULT));
        printw("[Q] Finalizar\n");

        refresh();

        int command = tolower(getch());
        if(command != ERR) {
            if(command == 'c') {
                menu_register();
            } else if(command == 'g') {
                room_menu();
            } else if(command == 'a') {
                handle_alarm_status();
            } else if(command == 'q') {
                exit_server();
            }
        }
    }
    return NULL;
}

void room_menu(){
    clear();

    while(1){
        erase();
        int room = 0;
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

            attron(COLOR_PAIR(BLUE));
            printw("Cômodo: %s\n\n", rooms[room]);

            attron(COLOR_PAIR(DEFAULT));
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
                printw("Não há dispositivos nesse cômodo.\n");
            } else {
                printw("Dispositivos:\n");
            }
            for(int i = 0; i < count; i++) {
                printw("[%d] ", i);
                if(list[i]->input_status == 0) {
                    attron(COLOR_PAIR(RED));
                } else {
                    attron(COLOR_PAIR(GREEN));
                }
                printw("%s", list[i]->input);

                if(list[i]->mode == ENERGY_ID) {
                    attron(COLOR_PAIR(DEFAULT));
                    printw(" | ");

                    if(list[i]->output_status == 0) {
                        attron(COLOR_PAIR(RED));
                    } else {
                        attron(COLOR_PAIR(GREEN));
                    }
                    printw("%s", list[i]->output);
                    if(list[i]->is_dimmable && list[i]->output_status!=0 ){
                        attron(COLOR_PAIR(DEFAULT));
                        printw(" ( %d )", list[i]->output_status);
                    }
                }
                printw("\n");
                attron(COLOR_PAIR(DEFAULT));
            }

            printw("\n[Q] Voltar\n");

            refresh();

            int command = tolower(getch());
            if(command != ERR) {
                if(command - '0' >= 0 && command  - '0' < count) {
                    device_menu(list[command - '0']);
                } else if(command == 'q') {
                    break;
                }
            }
        }
    }
}

int dimmable_menu(){
    clear();
    echo();
    curs_set(1);
    timeout(-1);

    printw("Intensidade do acionamento (0 a 255): ");
    refresh();
    char num[5];
    getstr(num);
    int number = atoi(num);
    if(number > 255){
        number = 255;
    } else if(number < 0){
        number = 0;
    }

    timeout(1000);
    noecho();
    curs_set(0);
    return number;
}

void device_menu(Device *dev) {
    clear();

    while(1) {
        erase();

        if(dev->input_status) {
            attron(COLOR_PAIR(GREEN));
            printw("Estado da entrada: Ligado\n");
        } else {
            attron(COLOR_PAIR(RED));
            printw("Estado da entrada: Desligado\n");
        }

        if(dev->mode == ENERGY_ID) {
            if(dev->output_status) {
                attron(COLOR_PAIR(GREEN));
                printw("Estado da saída: Ligado\n");
            } else {
                attron(COLOR_PAIR(RED));
                printw("Estado da saída: Desligado\n");
            }
        }

        attron(COLOR_PAIR(DEFAULT));

        if(!print_device(dev->id)) {
            break;
        }

        if(dev->mode == ENERGY_ID){
            printw("\n[A] Ligar/Desligar saída\n");
            printw("[R] Remover dispositivo\n");
        } else {
            printw("\n");
        }
        printw("[Q] Voltar\n");
        refresh();

        int command = tolower(getch());
        if(command != ERR) {
            if((command == 'a' || command == 'r') && dev->mode == BATTERY_ID) {
                continue;
            }
            if(command == 'a') {
                if(dev->is_dimmable) {
                    dev->output_status = dimmable_menu();      
                } else {
                    dev->output_status = !dev->output_status;
                }
                change_status(dev);
                check_alarm(dev->id);                
            } else if(command == 'r'){
                handle_remove_device(dev->id);
                break;
            } else if(command == 'q') {
                break; 
            }
        }
    }
}

int room_selection_menu() {
    clear();

    int choice = -1;
    while(1) {
        erase();

        attron(COLOR_PAIR(BLUE));
        printw("Selecione um cômodo:\n\n");

        attron(COLOR_PAIR(DEFAULT));
        for(int i = 0; i < rooms_size; i++) {
            printw("[%d] %s\n", i, rooms[i]);
        }
        if(rooms_size > 0) {
            printw("\n");
        }
        printw("[N] Novo cômodo\n");
        printw("[Q] Voltar\n");
        refresh();

        choice = tolower(getch());
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
    clear();

    timeout(-1);
    echo();
    curs_set(1);

    attron(COLOR_PAIR(BLUE));
    printw("Cadastrando novo cômodo...\n\n");

    attron(COLOR_PAIR(DEFAULT));
    printw("Nome do cômodo: ");
    refresh();

    char room_name[25];
    getstr(room_name);
    if(find_room_by_name(room_name) == -1){
        strcpy(rooms[rooms_size++], room_name);
        char new_topic[100];
        sprintf(new_topic, "fse2021/180113861/%s/temperatura", room_name);
        mqtt_subscribe(&client, new_topic, 0);
        sprintf(new_topic, "fse2021/180113861/%s/umidade", room_name);
        mqtt_subscribe(&client, new_topic, 0);
        sprintf(new_topic, "fse2021/180113861/%s/estado", room_name);
        mqtt_subscribe(&client, new_topic, 0);

        char command[100];
        sprintf(command, "Cadastro do cômodo %s", room_name);
        log_data("geral", command);
    }

    noecho();
    curs_set(0);
    timeout(1000);
}

void menu_register() {   
    clear();

    int choice = 0;
    while(1) {
        erase();
        attron(COLOR_PAIR(BLUE));
        printw("Selecione um dispositivo para cadastrar:\n\n");
        attron(COLOR_PAIR(DEFAULT));

        for(int i = 0; i < queue_size; i++) {
            printw("[%d] MAC: %s\n", i, devices_queue[i]);
        }
        if(queue_size == 0){
            printw("Não há dispositivos aguardando cadastro.\n");
        }
        printw("\n[Q] Voltar\n");
        choice = tolower(getch());
        if(choice == 'q') {
            return;
        } else if(choice >= '0' && choice - '0' < queue_size) {
            if(find_device_by_mac(devices_queue[choice - '0']) == -1) {
                break;
            }
        }
        refresh();
    }

    Device new_device;
    request_mode(devices_queue[choice - '0']);

    clear();
    attron(COLOR_PAIR(YELLOW));
    printw("Iniciando o cadastro do dispositivo...\n");
    refresh();
    sleep(3);

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
    attron(COLOR_PAIR(BLUE));
    printw("Cadastrando dispositivo...\n\n");

    attron(COLOR_PAIR(DEFAULT));
    printw("Nome do dispositivo de entrada: ");
    refresh();

    getstr(new_device.input);

    if(devices[devices_size].mode == ENERGY_ID) {
        clear();
        attron(COLOR_PAIR(BLUE));
        printw("Cadastrando dispositivo...\n\n");

        attron(COLOR_PAIR(DEFAULT));        
        printw("Nome do dispositivo de saída: ");

        refresh();
        getstr(new_device.output);
    }

    noecho();
    curs_set(0);
    timeout(1000);

    new_device.trigger_alarm = get_answer_menu("A entrada aciona alarme?");

    if(devices[devices_size].mode == ENERGY_ID) {
        new_device.is_dimmable = get_answer_menu("A saída é dimerizável?");
    } else {
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
    attron(COLOR_PAIR(BLUE));
    printw("Dispositivo cadastrado com sucesso\n\n");

    attron(COLOR_PAIR(DEFAULT));
    print_device(devices[devices_size - 1].id);
    printw("\nAperte qualquer tecla para continuar...\n");
    refresh();
    getch();

    timeout(1000);
}

int get_answer_menu(char *question) {
    clear();

    int answer = 0;
    while(1) {
        erase();
        attron(COLOR_PAIR(BLUE));
        printw("Cadastrando dispositivo...\n\n");

        attron(COLOR_PAIR(DEFAULT));        
        printw("%s\n[S] Sim\n[N] Não\n", question);
        refresh();
        answer = tolower(getchar());

        if(answer == 's'){
            answer = 1;
            break;
        } else if(answer == 'n'){
            answer = 0;
            break;
        }
    }

    return answer;
}

void register_device(Device new_device) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(item, "room", cJSON_CreateString(rooms[new_device.room]));
    cJSON_AddItemToObject(item, "input", cJSON_CreateString(new_device.input));
    cJSON_AddItemToObject(item, "output", cJSON_CreateString(new_device.output));
    cJSON_AddItemToObject(item, "trigger_alarm", cJSON_CreateNumber(new_device.trigger_alarm));
    cJSON_AddItemToObject(item, "is_dimmable", cJSON_CreateNumber(new_device.is_dimmable));

    char msg_topic[100];
    sprintf(msg_topic, "fse2021/180113861/dispositivos/%s", new_device.id);

    char *msg = cJSON_Print(item);
    mqtt_publish(&client, msg_topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);

    free(msg);
    cJSON_Delete(item);

    new_device.input_status = 0;
    new_device.output_status = 0;
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
    cJSON_AddItemToObject(root, "id", cJSON_CreateString(dev->id));
    cJSON_AddItemToObject(root, "status", cJSON_CreateNumber(dev->output_status));
    
    char *text = cJSON_Print(root);

    mqtt_publish(&client, topic, text, strlen(text), MQTT_PUBLISH_QOS_0);

    if(dev->output_status == 0) {
        log_data(dev->id, "dispositivo de saída desligado");
    } else {
        log_data(dev->id, "dispositivo de saída ligado");
    }

    free(text);
    cJSON_Delete(root);
}

int print_device(char* mac_addr){
    int i = find_device_by_mac(mac_addr);
    if(i == -1) {
        return 0;
    }
    printw("MAC: %s\n", devices[i].id);
    printw("Cômodo: %s\n", rooms[devices[i].room]);
    printw("Nome da entrada: %s\n", devices[i].input);
    if(devices[i].mode == ENERGY_ID){
        printw("Nome da saída: %s\n", devices[i].output);
        printw("Tipo: Energia\n");
        if(devices[i].is_dimmable) {
            printw("Intensidade: %d\n", devices[i].output_status);
        }
    } else if(devices[i].mode == BATTERY_ID){
        printw("Tipo: Bateria\n");
    }
    if(devices[i].trigger_alarm) {
        printw("Aciona alarme: Sim\n");
    } else {
        printw("Aciona alarme: Não\n");
    }
    return 1;
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
            if(devices[i].temperature == 0 || devices[i].humidity == 0) {
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
            devices[find_device_by_mac(cJSON_GetObjectItem(root, "id")->valuestring)].input_status = cJSON_GetObjectItem(root, "status")->valueint;

            if(cJSON_GetObjectItem(root, "status")->valueint == 0) {
                log_data(cJSON_GetObjectItem(root, "id")->valuestring, "dispositivo de entrada desligado");
            } else {
                log_data(cJSON_GetObjectItem(root, "id")->valuestring, "dispositivo entrada ligado");
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
        if(devices[i].trigger_alarm && devices[i].input_status){
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
    if(devices[index].trigger_alarm && devices[index].input_status) {
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
    device.input_status = 0;
    device.output_status = 0;
    device.is_active = 1;
    devices[devices_size++] = device;

    log_data(device.id, "dispositivo reconectado");

    cJSON_Delete(json);
}

void handle_device_data(struct mqtt_response_publish *published, char *type) {
    cJSON *json = cJSON_Parse(published->application_message);

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
                char topic[100];
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
    sprintf(topic, "fse2021/180113861/dispositivos/%s", mac_addr);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "type", cJSON_CreateString("remover"));
    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("central"));
    char *message = cJSON_Print(json);
    mqtt_publish(&client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);
    
    cJSON_Delete(json);
    free(message);

    int index = find_device_by_mac(mac_addr);
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
