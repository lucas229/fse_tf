#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "mqtt.h"
#include "posix_sockets.h"

void publish_callback(void **unused, struct mqtt_response_publish *published);
void *client_refresher(void *client);
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int main()
{
    const char* addr = "test.mosquitto.org";
    const char* port = "1883";
    const char* topic = "datetime";
    int sockfd = open_nb_socket(addr, port);
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
    while(fgetc(stdin) != EOF);
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

void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    char* topic_name = (char*) malloc(published->topic_name_size + 1);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    printf("Received publish('%s'): %s\n", topic_name, (const char*) published->application_message);

    free(topic_name);
}

void* client_refresher(void* client)
{
    while(1)
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}
