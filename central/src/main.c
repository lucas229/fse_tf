#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "mqtt.h"
#include "posix_sockets.h"
#include "cJSON.h"

void publish_callback(void **unused, struct mqtt_response_publish *published);
void *client_refresher(void *client);
void exit_example(int status, int sockfd, pthread_t *client_daemon);
void register_device(struct mqtt_response_publish *published);
void handle_device_data(struct mqtt_response_publish *published, char *type);

const char* addr = "test.mosquitto.org";
const char* port = "1883";
int sockfd;
int new_msg = 0;
char room_name[50];
char msg_topic[50];
char *msg;

int main()
{
    const char* topic = "fse2021/180113861/dispositivos/+";

    sockfd = open_nb_socket(addr, port);
    if(sockfd == -1)
    {
        perror("Failed to open socket: ");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    struct mqtt_client client;
    uint8_t sendbuf[2048];
    uint8_t recvbuf[1024];
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    const char* client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    if(client.error != MQTT_OK)
    {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    pthread_t client_daemon;
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }
    mqtt_subscribe(&client, topic, 0);
    while(1) {
        if(new_msg) {
            mqtt_unsubscribe(&client, topic);
            mqtt_publish(&client, msg_topic, msg, strlen(msg), MQTT_PUBLISH_QOS_0);
            mqtt_subscribe(&client, topic, 0);
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
    pthread_join(client_daemon, NULL);
    exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
}

void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if(sockfd != -1)
    {
        close(sockfd);
    }
    if(client_daemon != NULL)
    {
        pthread_cancel(*client_daemon);
    }
    exit(status);
}


void publish_callback(void **unused, struct mqtt_response_publish *published)
{
    cJSON *root = cJSON_Parse(published->application_message);
    char *type = cJSON_GetObjectItem(root, "type")->valuestring;

    if(strcmp(type, "cadastro") == 0) {
        register_device(published);
    } else {
        handle_device_data(published, type);
    }

    cJSON_Delete(root);
}

void handle_device_data(struct mqtt_response_publish *published, char *type) {
    char message[500];
    strcpy(message, published->application_message);
    message[published->application_message_size] = '\0';
    cJSON *json = cJSON_Parse(message);

    float data = cJSON_GetObjectItem(json, type)->valueint;
    if(strcmp(type, "temperature") == 0) {
        printf("Temperatura: %.1f ºC\n", data);
    } else {
        printf("Umidade: %.1f %%\n", data);
    }

    cJSON_Delete(json);
}

void register_device(struct mqtt_response_publish *published) {
    char* topic_name = (char*) malloc(published->topic_name_size + 1);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    printf("Received publish('%s'): %s\n", topic_name, (const char*) published->application_message);

    cJSON *root = cJSON_Parse(published->application_message);

    cJSON *device_id = cJSON_GetObjectItem(root, "id");
    printf("\nNome do cômodo: ");
    scanf(" %[^\n]", room_name);

    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "comodo", cJSON_CreateString(room_name));

    sprintf(msg_topic, "fse2021/180113861/dispositivos/%s", device_id->valuestring);
    msg = cJSON_Print(item);
    new_msg = 1;

    cJSON_Delete(root);
    free(topic_name);
}

void* client_refresher(void *client)
{
    while(1)
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}
