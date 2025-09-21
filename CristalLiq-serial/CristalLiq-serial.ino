/**
 * @mainpage Módulo Arduino - CristalLiq
 *
 * @section intro_sec Introdução
 * Este módulo implementa a comunicação entre o _display_ de cristal líquido
 * e o Arduino via porta serial sobre USB, utilizando a classe `SerialProtocol` para cuidar da transmissão dos quadros.
 *
 * @section features_sec Funcionalidades
 * - Recepção de comandos da TV-Box por meio  de protocolo serial sobre USB.
 * - Exibição das mensagens no _display_ LCD de 4 linhas.
 * - Emissão de sinais sonoros via _buzzer_.
 * - Ajuste e leitura do RTC.
 *
 * @section arch_sec Arquitetura
 * O sistema é dividido em:
 * - `CristalLiq-serial.ino`: ponto de entrada e lógica principal.
 * - `frame.h/.cpp`: implementação da classe SerialProtocol.
 * - `SerialProtocol`: protocolo de comunicação _master/slave_ por serial sobre USB.
 *
 * @section usage_sec Uso
 * 1. Carregar o código no Arduino Nano com o Arduino IDE.
 * 2. Conectar o _display_ LCD de 4 linhas, o _buzzer_ e o RTC.
 *
 * @section img_sec Máquina de Estado do protocolo
 * !![Máquina de Estados](MaquinaEstadoProtocolo.png) 
 */

/**
* @file CristalLiq-serial.ino
* @brief O Arduino Nano gerencia a exibição no Display de quatro linhas, o acionamento do _buzzer_ e o ajuste e leitura do RTC.
* 
* O Arduino Nano comunica-se por via serial sobre USB com a TV-Box. O protocolo de comunicação está na classe  SerialProtocol.
* São mensagens de quadro encapsuladas com os caracteres '<' e '>'. No interior do quadro é possível usar o caracter de escape para: '\<', '\>' e '\\'.
* A semântica das mensagens é específica para a aplicação IFSPresente.
* Há seis tipos de mensagens.
* * <100,0,0>            &rarr;  PING                                             
* * <200,TEXTO,TIMEOUT>  &rarr;  TIME (Linha 0, para sala, data e hora)           
* * <300,TEXTO,TIMEOUT>  &rarr;  LECTURE_NAME (Linha 1, para nome da palestra)    
* * <400,TEXTO,TIMEOUT>  &rarr;  SPEAKER (Linha 2, para nome do palestrante)      
* * <500,TEXTO,TIEMOUT>  &rarr;  ATTENDEE (Linha 3, aponta participante registrado
* * <600,0,0>            &rarr;  SUCCESS (Beep de sucesso no registro)            
* * <601,0,0>            &rarr;  FAIL (Beep de falha no registro)
* * <700,HH:MM:SS,0>     &rarr;  SETTIME (Define a hora do RTC)
* * <701,0,0>            &rarr;  GETTIME (Recebe a hora do RTC)     
*         
* Outras aplicações podem definir outros modelos de mensagens nos quadros do protocolo.
*/

#include <Wire.h>              // Biblioteca utilizada para fazer a comunicação com o I2C
#include <LiquidCrystal_I2C.h> // Biblioteca utilizada para fazer a comunicação com o display 20x4 
#include "frame.h"             // Implementação da classe SerialProtocol

/** 
 * @name Códigos de Mensagens do Protocolo.
 * @brief Constantes usadas na comunicação serial com a TV-Box.
 *
 * Cada constante representa um tipo de mensagem disparada pelo master (TV-Box).
* @{
*/
#define PING         100 /**< Obtém o timestamp do uptime e a versão do firmware. */ 
#define TIME         200 /**< Linha 0: sala, data e hora. */
#define LECTURE_NAME 300 /**< Linha 1: nome da palestra. */
#define SPEAKER      400 /**< Linha 2: nome do professor responsável pela aula em curso. */
#define ATTENDEE     500 /**< Linha 3: estudante que aponta sua presença. */
#define SUCCESS      600 /**< Beep de sucesso no registro, seja por leitor biométrico de digital ou por senha no teclado numérico. */
#define FAIL         601 /**< Beep de falha no registro, seja por leitor biométrico de digital ou por senha no teclado numérico. . */
#define SETTIME      700 /**< Comando para ajustar data/hora do RTC ligado ao Arduino. */
#define GETTIME      701 /**< Comando para solicitar data/hora do RTC ligado ao Arduino. */
/** @} */


/** 
 * @name Macros de controle dos dispositivos.
 * @brief Constantes usadas para I2C e controles adicionais.
 *
 * Essas constantes são relacionadas aos endereços I2C e demais ajustes sobre os dispositivos externos.
* @{
*/
#define BUZZER      2               /**< Pino digital ligado ao _buzzer_ */
#define COL        20               /**< Serve para definir o numero de colunas do display utilizado */ 
#define ROW         4               /**< Serve para definir o numero de linhas do display utilizado  */
#define ADDRESS  0x27               /**< Serve para definir o endereço do display. */
#define DISPLAY_UPDATE_DELAY 500    /**< Tempo em milissegundos em que um texto é exibido numa linha do display antes de sofrer _scroll_ */ 
#define LOOP_DELAY            10    /**< Tempo em que o loop principal do código do Arduino dorme à espera de uma mensagem */
#define KEEP_AT_ZERO           1    /**< Quando um texto é exibido numa linha do display, deve ficar um tempo a mais antes de iniciar o _scroll_ */
#define KEEP_AT_LAST           1    /**< Quando um texto é exibido numa linha do display, deve ficar um tempo a mais antes de reiniciar o _scroll_ */

struct ProtocolMessage {
  int code;
  int TTL;
  char message[MAX_STRING+1];
};
ProtocolMessage netMessage;

struct Display {
  char message[MAX_STRING + 1];
  char defaultMessage[MAX_STRING + 1];
  char toPrint[COL+1];
  int messageSize;
  int defaultMessageSize;
  int startPosition;
  byte keepAtZeroPosition;
  unsigned long TTL;
};

Display dispArray[ROW] = {
                          {"", "IFSPresente", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Local Disponivel", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Sem reserva de palestrante", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Aguardando Registro", "", 0, 0, 0, KEEP_AT_ZERO, 0}
                         };



char strReply[80];
char auxStr[80]; 
unsigned long uptime;
LiquidCrystal_I2C lcd(ADDRESS,COL,ROW); // Chamada da funcação LiquidCrystal para ser usada com o I2C
SerialProtocol usbProto;

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void copiaN(char dest[], int sizeDest, char origem[], int sizeOrigem, int start ) {

     //Se a string cabe no display, não pode iniciar impressão para além da posição zero
     if (sizeDest >= sizeOrigem) 
         start = 0;
     else {
         //E se for maior que o display, se mandar copiar de uma string grande uma parte final inferior ao tamanho do display
         //Recua ao ponto de cópia que preenche completamente o display
         if (start > sizeOrigem - sizeDest)
            start = sizeOrigem-sizeDest;
     }

     //Quantidade máxima de caracteres copiados da origem
     int maxToCopy = min(sizeDest, sizeOrigem);
     for (int i = 0; i < maxToCopy; i++) {
      dest[i] = origem[i+start];
     }
     //Preenche o final com espaços em branco, se necessário
     for (int i = maxToCopy; i < sizeDest; i++) {
        dest[i] = ' ';
     }
     //Põe o finalizador
     dest[sizeDest] = '\0';    
}

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void atualizaDisplay(int lines) {
   static unsigned long nextUpdate = 0;
   unsigned long currentTime = millis();
   bool updateNext = false;
  
  if (currentTime < nextUpdate) {  //Avalia se o tempo de realizar update no display expirou
      if (usbProto.machState != RECEIVED)
          return;
  }

   if (usbProto.machState != RECEIVED)
       nextUpdate = currentTime + DISPLAY_UPDATE_DELAY;

   for (int i = 0; i < ROW; i++) {
      if (lines != -1 && i != lines)
          continue;   
      if (dispArray[i].TTL < currentTime) {
        copiaN(dispArray[i].toPrint, 
               COL, 
               dispArray[i].defaultMessage, 
               dispArray[i].defaultMessageSize,
               dispArray[i].startPosition);
        if (dispArray[i].defaultMessageSize > COL) {
            if (dispArray[i].startPosition == 0 && dispArray[i].keepAtZeroPosition > 0) {
                dispArray[i].keepAtZeroPosition--;
            }
            else {
                dispArray[i].startPosition = (dispArray[i].startPosition + 1) % (dispArray[i].defaultMessageSize - COL + 1 + KEEP_AT_LAST);
                dispArray[i].keepAtZeroPosition = KEEP_AT_ZERO;
            }
        }
      }
      else {
        copiaN(dispArray[i].toPrint, 
               COL, 
               dispArray[i].message, 
               dispArray[i].messageSize,
               dispArray[i].startPosition);
        if (dispArray[i].messageSize > COL) {
            if (dispArray[i].startPosition == 0 && dispArray[i].keepAtZeroPosition > 0) {
                dispArray[i].keepAtZeroPosition--;
            }
            else {
                dispArray[i].startPosition = (dispArray[i].startPosition + 1) % (dispArray[i].messageSize - COL + 1 + KEEP_AT_LAST);
                dispArray[i].keepAtZeroPosition = KEEP_AT_ZERO;
            }
        }

      }
      lcd.setCursor(0,i);
      lcd.print(dispArray[i].toPrint);
   }
}

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void parseMessage() {
    char * strtokIndx; // this is used by strtok() as an index
    usbProto.removeAccentMarker(usbProto.receivedChars);
    strtokIndx = strtok(usbProto.receivedChars,",");      // O código da mensagem
    netMessage.code = atoi(strtokIndx);
    
    strtokIndx = strtok(NULL, ",");             // A mensagem
    strcpy(netMessage.message, strtokIndx);
        
    strtokIndx = strtok(NULL, ",");             // Tempo de vida da mensagem em milissegundos
    netMessage.TTL = atoi(strtokIndx);
}

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void setup() //Incia o display
{  
  pinMode(BUZZER, OUTPUT);
  lcd.init();         // Serve para iniciar a comunicação com o display já conectado
  //lcd.backlight();    // Serve para ligar a luz do display
  lcd.setContrast(255);
  lcd.setBacklight(255);
  lcd.noAutoscroll();
  lcd.noBlink();
  lcd.clear();        // Serve para limpar a tela do display
  usbProto.setBaudRate(9600); // send and receive at 9600 baud
  //Ajusta o tamanho das strings default em dispArray
  for (int i = 0; i < ROW; i++) {
    dispArray[i].messageSize = strlen(dispArray[i].message);
    dispArray[i].defaultMessageSize = strlen(dispArray[i].defaultMessage);
  }
}

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void loop() 
{
 CONTINUE: 
  atualizaDisplay(-1);        //Atualiza todas as linhas do display
  usbProto.receiveFrame();
  if (usbProto.machState == RECEIVED) {
    parseMessage();
    switch (netMessage.code) {
      case PING:
        strReply[0] = '\0';
        strcat(strReply, "001,");
        uptime = millis();
        itoa( uptime, auxStr, 10);
        strcat(strReply, auxStr);
        usbProto.sendFrame(strReply);
        break;
      case TIME:
        usbProto.sendFrame("002,OK");
        strcpy(dispArray[0].message, netMessage.message);
        dispArray[0].messageSize = strlen(netMessage.message);
        dispArray[0].TTL = millis() + netMessage.TTL;
        dispArray[0].startPosition = 0;
        dispArray[0].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case LECTURE_NAME:
        usbProto.sendFrame("002,OK");
        strcpy(dispArray[1].message, netMessage.message);
        dispArray[1].messageSize = strlen(netMessage.message);
        dispArray[1].TTL = millis() + netMessage.TTL;
        dispArray[1].startPosition = 0;
        dispArray[1].keepAtZeroPosition = KEEP_AT_ZERO;
        
        break;
      case SPEAKER:
        usbProto.sendFrame("002,OK");
        strcpy(dispArray[2].message, netMessage.message);
        dispArray[2].messageSize = strlen(netMessage.message);
        dispArray[2].TTL = millis() + netMessage.TTL;
        dispArray[2].startPosition = 0;
        dispArray[2].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case ATTENDEE:
        usbProto.sendFrame("002,OK");
        strcpy(dispArray[3].message, netMessage.message);
        dispArray[3].messageSize = strlen(netMessage.message);
        dispArray[3].TTL = millis() + netMessage.TTL;
        dispArray[3].startPosition = 0;
        dispArray[3].keepAtZeroPosition = KEEP_AT_ZERO;
        atualizaDisplay(3);  //Atualiza forçosamente só a linha 3, o display fica mais responsivo a tecladas rápidas.
        break;
      case SUCCESS:
        usbProto.sendFrame("002,OK");
        tone(BUZZER,1000,150);
        break;
      case FAIL:
        usbProto.sendFrame("002,OK");
        tone(BUZZER,2000,150);
        delay(300);
        tone(BUZZER,2000,150);
        break;
    }
    usbProto.machState = START;
    goto CONTINUE;
  }
  delay(LOOP_DELAY);  // delay do loop principal
}