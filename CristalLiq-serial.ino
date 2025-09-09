#include <Wire.h>              // Biblioteca utilizada para fazer a comunicação com o I2C
#include <LiquidCrystal_I2C.h> // Biblioteca utilizada para fazer a comunicação com o display 20x4 

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
#define MAX_STRING_DISPLAY    80
#define MAX_PROTOCOL_MESSAGE  MAX_STRING_DISPLAY + 4  // ddd,maior string já desprezados os caracteres de inicio e fim
#define DISPLAY_UPDATE_DELAY 500
#define LOOP_DELAY            10
#define KEEP_AT_ZERO          1
#define KEEP_AT_LAST          1

const unsigned char win1252_to_ascii[256] = {
    /* 0x00–0x0F */
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    /* 0x10–0x1F */
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    /* 0x20–0x2F */
    ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    /* 0x30–0x3F */
    '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    /* 0x40–0x4F */
    '@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    /* 0x50–0x5F */
    'P','Q','R','S','T','U','V','W','X','Y','Z','[','\\',']','^','_',
    /* 0x60–0x6F */
    '`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    /* 0x70–0x7F */
    'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~',127,
    /* 0x80–0x8F */
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    /* 0x90–0x9F */
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    /* 0xA0–0xAF */
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    /* 0xB0–0xBF */
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    /* 0xC0–0xCF */
    'A','A','A','A','A','A','A','C','E','E','E','E','I','I','I','I',
    /* 0xD0–0xDF */
    'D','N','O','O','O','O','O','O','U','U','U','U','Y','P','B',223,
    /* 0xE0–0xEF */
    'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
    /* 0xF0–0xFF */
    'd','n','o','o','o','o','o','o','u','u','u','u','y','p','b','y'
};

struct ProtocolMessage {
  int code;
  int TTL;
  char message[MAX_STRING_DISPLAY+1];
};
ProtocolMessage netMessage;

struct Display {
  char message[MAX_STRING_DISPLAY + 1];
  char defaultMessage[MAX_STRING_DISPLAY + 1];
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


char receivedChars[MAX_PROTOCOL_MESSAGE+1]; //  '<ddd,maior string>
int index = 0;         // the index into the array storing the received digits
char strReply[80];
char auxStr[80]; 
unsigned long uptime;
boolean newData = false;

LiquidCrystal_I2C lcd(ADDRESS,COL,ROW); // Chamada da funcação LiquidCrystal para ser usada com o I2C

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void removeAccentMarker(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        str[i] = win1252_to_ascii[ (unsigned char) str[i] ];
    }
}

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
      if (newData == false)
          return;
  }

   if (newData == false)
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
/* Não há timeout nesta função.                                              */
/* Para contornar a limitação, a recepção de um frame completo pode envolver */
/* várias invocações desta função, daí as variáveis ndx e recvInProgress     */
/* serem estáticas.                                                          */
/* no loop() do Arduino, continuará invocando esta função até que o frame    */
/* se complete.                                                              */ 
/*****************************************************************************/
void recvWithStartEndMarkers() {
    static boolean recvInProgress = false; 
    static byte ndx = 0;
    char startMarker = '<';
    char endMarker = '>';
    unsigned char rc;

    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();
        if (recvInProgress == true) {
            if (rc != endMarker) {
                if (ndx >= MAX_PROTOCOL_MESSAGE-1) {
                    continue;
                }
                receivedChars[ndx++] = rc;                
            }
            else {
                receivedChars[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                ndx = 0;
                newData = true;
            }
        }
        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void parseMessage() {
    char * strtokIndx; // this is used by strtok() as an index
    removeAccentMarker(receivedChars);
    strtokIndx = strtok(receivedChars,",");      // O código da mensagem
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
  Serial.begin(9600); // send and receive at 9600 baud
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
  atualizaDisplay(-1);        //Atualiza todas as linhas do display
  recvWithStartEndMarkers();  //Quando uma mensagem completa é recebida, newData vai a TRUE
  if (newData) {
    parseMessage();
    switch (netMessage.code) {
      case PING:
        strReply[0] = '\0';
        strcat(strReply, "<001,");
        uptime = millis();
        itoa( uptime, auxStr, 10);
        strcat(strReply, auxStr);
        strcat(strReply, ">");
        Serial.write(strReply);
        break;
      case TIME:
        Serial.write("<002,OK>");
        strcpy(dispArray[0].message, netMessage.message);
        dispArray[0].messageSize = strlen(netMessage.message);
        dispArray[0].TTL = millis() + netMessage.TTL;
        dispArray[0].startPosition = 0;
        dispArray[0].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case LECTURE_NAME:
        Serial.write("<002,OK>");
        strcpy(dispArray[1].message, netMessage.message);
        dispArray[1].messageSize = strlen(netMessage.message);
        dispArray[1].TTL = millis() + netMessage.TTL;
        dispArray[1].startPosition = 0;
        dispArray[1].keepAtZeroPosition = KEEP_AT_ZERO;
        
        break;
      case SPEAKER:
        Serial.write("<002,OK>");
        strcpy(dispArray[2].message, netMessage.message);
        dispArray[2].messageSize = strlen(netMessage.message);
        dispArray[2].TTL = millis() + netMessage.TTL;
        dispArray[2].startPosition = 0;
        dispArray[2].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case ATTENDEE:
        Serial.write("<002,OK>");
        strcpy(dispArray[3].message, netMessage.message);
        dispArray[3].messageSize = strlen(netMessage.message);
        dispArray[3].TTL = millis() + netMessage.TTL;
        dispArray[3].startPosition = 0;
        dispArray[3].keepAtZeroPosition = KEEP_AT_ZERO;
        atualizaDisplay(3);  //Atualiza forçosamente só a linha 3, o display fica mais responsivo a tecladas rápidas.
        break;
      case SUCCESS:
        Serial.write("<002,OK>");
        tone(BUZZER,1000,150);
        break;
      case FAIL:
        Serial.write("<002,OK>");
        tone(BUZZER,2000,150);
        delay(300);
        tone(BUZZER,2000,150);
        break;
    }
  }
  newData = false;
  delay(LOOP_DELAY);  // delay do loop principal
}