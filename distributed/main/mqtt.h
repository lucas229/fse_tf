#ifndef MQTT_H
#define MQTT_H

void mqtt_start();

void mqtt_envia_mensagem(char * topico, char * mensagem);
void mqtt_inscrever();
void obter_mensagem(char *mensagem);

#endif