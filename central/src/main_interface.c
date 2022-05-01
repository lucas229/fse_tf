#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "posix_sockets.h"
#include "cJSON.h"
#include "main_interface.h"

Device devices[MAX_DEVICES];
int devices_size = 0;

struct mqtt_client client;

int new_msg = 0;
char room_name[50];
char msg_topic[50];
char *msg;

int wait_message = 0;

int sockfd = -1;
pthread_t menu_tid, client_daemon, freq_tid;

int init_server() {
    char addr[] = "test.mosquitto.org";
    char port[] = "1883";
    char topic[] = "fse2021/180113861/dispositivos/+";
    sockfd = open_nb_socket(addr, port);
    init_client(topic);

    pthread_create(&freq_tid, NULL, check_frequence, NULL);
    pthread_create(&menu_tid, NULL, init_menu, NULL);

    while(1) {
        if(new_msg) {
            mqtt_publish(&client, msg_topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);
            free(msg);
            char new_topic[100];
            sprintf(new_topic, "fse2021/180113861/%s/temperatura", room_name);
            mqtt_subscribe(&client, new_topic, 0);
            sprintf(new_topic, "fse2021/180113861/%s/umidade", room_name);
            mqtt_subscribe(&client, new_topic, 0);
            sprintf(new_topic, "fse2021/180113861/%s/estado", room_name);
            mqtt_subscribe(&client, new_topic, 0);
            new_msg = 0;
        }
        usleep(100000);
    }
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

void show_status_menu() {
    for(int i = 0; i < devices_size; i++) {
        printf("Dispositivo %d:\n", i + 1);
        printf("Temperatura: %.1f ºC\n", devices[i].temperature);
        printf("Umidade: %.1f %%\n", devices[i].humidity);
        printf("Estado: %d\n", devices[i].status);
        printf("Ativo: %d\n\n", devices[i].is_active);
    }
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

void *init_menu(void *args) {
    while(wait_message < 2) {
        sleep(1);
    }    
    while(1) {
        int choice;
        printf("[1] Ver estados\n");
        printf("[2] Alterar estados\n");
        scanf("%d", &choice);
        if(choice == 1) {
            show_status_menu();
        } else if(choice == 2) {
            change_status_menu();
        }
    }
}

void exit_server() {
    if(sockfd != -1) {
        close(sockfd);
    }
    pthread_cancel(client_daemon);
    pthread_cancel(menu_tid);
    pthread_cancel(freq_tid);
    exit(0);
}

int find_device_by_mac(char *mac_addr){
    for(int i=0; i<devices_size; i++){
        if(strcmp(mac_addr, devices[i].id) == 0) {
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
            register_device(published);
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


void register_device(struct mqtt_response_publish *published) {
    cJSON *root = cJSON_Parse(published->application_message);

    printf("\nNome do cômodo: ");
    scanf(" %[^\n]", room_name);

    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("central"));
    cJSON_AddItemToObject(item, "room", cJSON_CreateString(room_name));
    
    cJSON *device_id = cJSON_GetObjectItem(root, "id");
    int status = cJSON_GetObjectItem(root, "status")->valueint;
    
    sprintf(msg_topic, "fse2021/180113861/dispositivos/%s", device_id->valuestring);
    msg = cJSON_Print(item);
    new_msg = 1;

    strcpy(devices[devices_size].id, device_id->valuestring);
    devices[devices_size].status = status;
    devices[devices_size].is_active = 1;
    devices_size++;
    wait_message++;
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
