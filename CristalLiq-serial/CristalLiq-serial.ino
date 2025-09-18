/**
* @file CristalLiq-serial.ino
* @brief O Arduino Nano cuida da exibição no Display de quatro linhas e do buzzer.
* 
* O Arduino Nano comunica-se por via serial sobre USB com a TV-Box. O protocolo de comunicação está na classe  SerialProtocol.
* São mensagens de quadro encapsuladas com os caracteres '<' e '> e que aceitam caracteres de escape para \<, \> e \\.
* A semântica das mensagens é específica para a aplicação IFSPresente.
* No IFSP podem há seis tipos de mensagens.
* * <100,0,0>           --> PING                                             
* * <200,TEXTO,TIMEOUT> --> TIME (Linha 0, para sala, data e hora)           
* * <300,TEXTO,TIMEOUT> --> LECTURE_NAME (Linha 1, para nome da palestra)    
* * <400,TEXTO,TIMEOUT> --> SPEAKER (Linha 2, para nome do palestrante)      
* * <500,TEXTO,TIEMOUT> --> ATTENDEE (Linha 3, aponta participante registrado
* * <600,0,0>           --> SUCCESS (Beep de sucesso no registro)            
* * <601,0,0>           --> FAIL (Beep de falha no registro)                 

* Outras aplicações podem definir outros modelos de mensagens nos quadros do protocolo.
*/

#include <Wire.h>              // Biblioteca utilizada para fazer a comunicação com o I2C
#include <LiquidCrystal_I2C.h> // Biblioteca utilizada para fazer a comunicação com o display 20x4 
#include "frame.h"

#define PING         100
#define TIME         200
#define LECTURE_NAME 300
#define SPEAKER      400
#define ATTENDEE     500
#define SUCCESS      600
#define FAIL         601

#define BUZZER      2           // Pino digital ligado ao buzzer

#define COL        20           // Serve para definir o numero de colunas do display utilizado
#define ROW         4           // Serve para definir o numero de linhas do display utilizado
#define ADDRESS  0x27           // Serve para definir o endereço do display.
#define DISPLAY_UPDATE_DELAY 500
#define LOOP_DELAY            10
#define KEEP_AT_ZERO          1
#define KEEP_AT_LAST          1

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