# Trabalho Final - FSE 2021-2
Repositório referente ao Trabalho Final da disciplina de Fundamentos de Sistemas Embarcados. A descrição do trabalho pode ser encontrada em: https://gitlab.com/fse_fga/trabalhos-2021_2/trabalho-final-2021-2.

## Compilação e Execução

### Servidor Central
Para executar o servidor central é necessário ter instalada a biblioteca ncurses.
A biblioteca pode ser instalada executando:
```
sudo apt install libncurses5-dev
```

Após clonar o repositório, para compilar e executar o servidor central, utilize os seguintes comandos a partir da pasta raíz:
```
cd central
make
make run
```

### Servidor Distribuído
Para executar o menuconfig é necessário acessar o diretório do servidor distribuído:
```
cd distributed
idf.py menuconfig
```
- As configurações de SSID e senha do WiFi podem ser encontradas na opção "Configuração do Wifi" do menuconfig.
- A configuração do modo de operação da ESP (energia ou bateria) pode ser encontrada na opção "Modo de Operação" do menuconfig.

Após a configuração pelo menuconfig, o código pode ser compilado e executado na ESP com o comando:

```
idf.py -p /dev/ttyUSB0 flash monitor
```

## Uso

Criamos um [vídeo de apresentação](https://youtu.be/THk1y65D4Zs) para demonstração da utilização desse programa.

## Observações
- O projeto utiliza o broker público test.mosquitto.org.
- O desenvolvimento foi feito utilizando ESP-IDF nativa.
- A versão do ESP-IDF utilizada no projeto foi a 4.4.1.
- Quando o alarme implementado na aplicação é disparado, o som é emitido pelo terminal.
- O log de eventos é guardado em um arquivo csv na pasta "Logs" do servidor central.
- A temperatura e a umidade que são apresentadas na tela do cômodo são a média das temperaturas e das umidades de todos os dispositivos de energia cadastrados no cômodo.

## Equipe


|      Aluno      | Matrícula |
| :-------------: | :-------: |
| Kleidson Alves  | 180113861 |
| Lucas Rodrigues | 180114077 | 
