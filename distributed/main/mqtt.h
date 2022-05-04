#ifndef MQTT_H
#define MQTT_H

void mqtt_start();

void mqtt_send_message(char * topic, char * message);
void mqtt_subscribe();
void get_message(char *message);

#endif