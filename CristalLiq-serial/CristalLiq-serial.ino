/**
* @file CristalLiq-serial.ino
* @brief O Arduino Nano gerencia a exibição no Display de quatro linhas, o acionamento do _buzzer_ e o ajuste e leitura do RTC.
* 
* O Arduino Nano comunica-se por via serial sobre USB com a TV-Box. O protocolo de comunicação está na classe  SerialProtocol.
* São mensagens de quadro encapsuladas com os caracteres '<' e '>'. No interior do quadro é possível usar o caracter de escape para: '\<', '\>' e '\\'.
* A semântica das mensagens é específica para a aplicação IFSPresente.
* Há nove tipos de mensagens emitidas pela TV-Box.
* * <100|0|0>                        &rarr;  PING                                             
* * <200|TEXTO|TIMEOUT>              &rarr;  TIME (Linha 0, para sala, data e hora)           
* * <300|TEXTO|TIMEOUT>              &rarr;  LECTURE_NAME (Linha 1, para nome da palestra)    
* * <400|TEXTO|TIMEOUT>              &rarr;  SPEAKER (Linha 2, para nome do palestrante)      
* * <500|TEXTO|TIEMOUT>              &rarr;  ATTENDEE (Linha 3, aponta participante registrado
* * <600|0|0>                        &rarr;  SUCCESS (Beep de sucesso no registro)            
* * <601|0|0>                        &rarr;  FAIL (Beep de falha no registro)
* * <700|YYYY:MM:DD:HH:MM:SS|0>      &rarr;  SETTIME (Define a hora do RTC)
* * <701|0|0>                        &rarr;  GETTIME (Recebe a hora do RTC)     
*
* O Arduino responde com três tipos de mensagens.
* * <001|uptime em milissegundos|versão de firmware>  &rarr; Resposta ao ping 
* * <003|YYYY:MM:DD:HH:MM:SS|temperatura>             &rarr; Resposta ao gettime                     
* * <002|OK|>                                         &rarr; Resposta aos demais comandos 
*                 
* Outras aplicações podem definir outros modelos de mensagens nos quadros do protocolo.
*/

#include <Wire.h>              // Biblioteca utilizada para fazer a comunicação com o I2C
#include <LiquidCrystal_I2C.h> // Biblioteca utilizada para fazer a comunicação com o display 20x4 
#include <RTClib.h>            // Biblioteca utilizada para ajuste e leitura de 
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
/** @} */

/**
 * @struct ProtocolMessage
 * @brief Representa a decodificação de uma mensagem recebida.
 *
 * Uma mensagem num quadro vem em três campos separados por '|', <code|message|TTL>.
 */
struct ProtocolMessage {
  int code;                     /**< Código do serviço solicitado. */
  int TTL;                      /**< Tempo de vida para mensagens que são exibidas no _display_. */
  char message[MAX_STRING+1];   /**< Mensagem. */
};

/**
 * @var ProtocolMessage netMessage
 * @brief Mantém uma única mensagem recebida.
 */
ProtocolMessage netMessage;

/**
 * @struct Display
 * @brief Representa o estado de uma linha do display LCD.
 *
 * Esta estrutura guarda a mensagem principal, a mensagem padrão, a parte da
 * mensagem que deve ser impressa no momento, além de informações de tamanho,
 * posição e tempo de vida (TTL).
 */
struct Display {
  char message[MAX_STRING + 1];        /**< Mensagem atual a ser exibida. */
  char defaultMessage[MAX_STRING + 1]; /**< Mensagem padrão quando nenhuma outra estiver ativa. */
  char toPrint[COL+1];                 /**< Parte da mensagem que é impressa no display num dado momento. */
  int messageSize;                     /**< Tamanho da mensagem atual. */
  int defaultMessageSize;              /**< Tamanho da mensagem padrão. */
  int startPosition;                   /**< Posição inicial na mensagem a partir da qual imprime-se no display. */
  byte keepAtZeroPosition;             /**< Quando o trecho inicial da mensagem está sendo impresso, permanece por um tempo maior nesse estado. */
  unsigned long TTL;                    /**< Tempo de vida da mensagem em milissegundos. Após esse período, retorna à mensagem _default_ */
};

/**
 * @var Display dispArray[ROW]
 * @brief Informações para as quatro linhas do Display.
 *
 * Contém as quatro linhas do display LCD. Cada posição tem:
 * - Mensagem atual (`message`)
 * - Mensagem padrão (`defaultMessage`)
 * - Texto a imprimir (`toPrint`)
 * - Tamanho da mensagem (ajustado em `setup()`)
 * - Tamanho da mensagem padrão (ajustado em `setup()`)
 * - Posição inicial da string a partir da onde imprime no display
 * - Tempo de vida (TTL) de impressão da mensagem
 *
 * Inicialmente preenchido com mensagens padrão do sistema:
 * - Linha 0 → "IFSPresente"
 * - Linha 1 → "Local Disponivel"
 * - Linha 2 → "Sem reserva de palestrante"
 * - Linha 3 → "Aguardando Registro"
 */
Display dispArray[ROW] = {
                          {"", "IFSPresente", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Local Disponivel", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Sem reserva de palestrante", "", 0, 0, 0, KEEP_AT_ZERO, 0},
                          {"", "Aguardando Registro", "", 0, 0, 0, KEEP_AT_ZERO, 0}
                         };

/**
 * @var char* VERSION
 * @brief Versão do _firmware_.
 *
 * @note É retornado junto com o serviço ping.
 */
const char* VERSION = "1.0";

char strReply[80];
char auxStr[80];

/**
 * @var unsigned long uptime
 * @brief Tempo em que o Arduino está ligado em milissegundos.
 */
unsigned long uptime;

RTC_DS3231 rtc;  //Objeto rtc da classe DS3231
LiquidCrystal_I2C lcd(ADDRESS,COL,ROW); // Chamada da funcação LiquidCrystal para ser usada com o I2C

DateTime now;


/**
 * @var SerialProtocol usbProto
 * @brief Classe que implementa a transmissão e recepção de quadros pela serial sobre USB.
 */
SerialProtocol usbProto;


/**
 * @brief Copia um trecho de uma string de origem para um buffer de destino,
 *        ajustando posição inicial e preenchendo com espaços em branco, se necessário.
 *
 * Esta função garante que o conteúdo copiado caiba exatamente no tamanho
 * do display (ou outro destino), ajustando o índice inicial (`start`) caso:
 * - A string de origem seja menor ou igual ao destino → começa da posição zero.
 * - A string de origem seja maior que o destino, mas o ponto inicial da cópia desejada ultrapasse os limites → recua
 *   o início para preencher completamente o destino.
 *
 * Após a cópia, o restante do buffer de destino é preenchido com espaços em branco,
 * e o finalizador `'\0'` é adicionado ao fim.
 *
 * @param[out] dest       Buffer de destino (receberá a string copiada).
 * @param[in]  sizeDest   Tamanho da string que ficará em `dest`, número de caracteres visíveis no display. O buffer `dest` precisa ter pelo menos (sizeDest+1) bytes.
 * @param[in]  origem     String de origem.
 * @param[in]  sizeOrigem Tamanho da string de origem.
 * @param[in]  start      Posição inicial na string de origem a partir da qual
 *                        a cópia deve começar (pode ser ajustada internamente pelo algoritmo).
 *
 * @note Usa `min(sizeDest, sizeOrigem)` para calcular o máximo de caracteres a copiar.
 *       O destino é sempre terminado em `'\0'`.
 *
 * @section img_sec Máquina de Estado do protocolo
 * \image html img/copiaParcialDeString.png "Detalhamento do Algoritmo"
 */
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

/**
 * @brief Atualiza o conteúdo exibido no display LCD linha a linha.
 *
 * Esta função gerencia a exibição de mensagens no display, considerando:
 * - **Tempo mínimo entre atualizações** (usando `millis()` e `DISPLAY_UPDATE_DELAY`).
 * - **Mensagens temporárias (TTL)**: quando expiram, voltam para a mensagem padrão.
 * - **Rolagem (scroll)**: caso a mensagem seja maior que o número de colunas (`COL`),
 *   realiza deslocamento progressivo, mantendo o início por alguns ciclos
 *   (`KEEP_AT_ZERO`) antes de avançar.
 *
 * O comportamento difere conforme a mensagem ativa:
 * - Se `dispArray[i].TTL` expirou → mostra `defaultMessage` com rolagem.
 * - Caso contrário → mostra `message` com rolagem.
 *
 * @param[in] lines Índice da linha a ser atualizada:
 *                  - `-1` → atualiza todas as linhas.
 *                  - `0..ROW-1` → atualiza apenas a linha especificada.
 *
 * @note
 * - Usa `copiaN()` para preencher o buffer de exibição (`toPrint`).
 * - Usa o estado da máquina (`usbProto.machState`) para evitar atualizações
 *   durante recepção de dados.
 * - O cursor do LCD é posicionado no início de cada linha (`lcd.setCursor(0,i)`).
 *
 * ### Regras de rolagem
 * - Quando `startPosition == 0`, mantém a mensagem parada por `KEEP_AT_ZERO` ciclos.
 * - Depois, incrementa `startPosition` até o limite calculado,
 *   com espera em `KEEP_AT_LAST` no final.
 */
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

/**
 * @brief Interpreta a mensagem recebida pela serial USB e atualiza a estrutura global netMessage.
 *
 * Esta função:
 * - Remove os marcadores de acentuação gráfica da string recebida (`usbProto.receivedChars`).
 * - Usa `strtok` para separar os campos da mensagem, assumindo o caractere `|` como delimitador.
 * - Converte o primeiro campo para um código numérico (`netMessage.code`).
 * - Copia o segundo campo como texto da mensagem (`netMessage.message`).
 * - Converte o terceiro campo em milissegundos para o tempo de vida (`netMessage.TTL`).
 *
 * @note A função não recebe parâmetros nem retorna valor.
 *       Atua diretamente sobre as variáveis globais `usbProto` e `netMessage`.
 */
/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void parseMessage() {
    char * strtokIndx; // this is used by strtok() as an index
    usbProto.removeAccentMarker(usbProto.receivedChars);
    strtokIndx = strtok(usbProto.receivedChars,"|");      // O código da mensagem
    netMessage.code = atoi(strtokIndx);
    
    strtokIndx = strtok(NULL, "|");             // A mensagem
    strcpy(netMessage.message, strtokIndx);
        
    strtokIndx = strtok(NULL, "|");             // Tempo de vida da mensagem em milissegundos
    netMessage.TTL = atoi(strtokIndx);
}


/**
 * @brief Configuração inicial do sistema Arduino.
 *
 * Esta função é chamada automaticamente pelo _framework_ Arduino
 * logo após o reset ou inicialização da placa.
 * Ela é chamada uma única vez.
 *
 * Inicializações realizadas:
 * - Define o pino do buzzer (`BUZZER`) como saída.
 * - Inicializa o display LCD (`lcd.init()`).
 * - Configura contraste e backlight do display (mas não tem efeito no display que usamos).
 * - Desativa _autoscroll_ e cursor piscante.
 * - Limpa a tela do display (`lcd.clear()`).
 * - Configura a taxa de comunicação serial (`usbProto.setBaudRate(9600)`).
 * - Ajusta os tamanhos das mensagens padrão em `dispArray`.
 *
 * @note Esta função não recebe parâmetros e não retorna valor.
 *       É executada uma única vez antes de `loop()`.
 */
/*****************************************************************************/
/*                                                                           */
/*****************************************************************************/
void setup() 
{  
  Wire.begin();
  rtc.begin();
  pinMode(BUZZER, OUTPUT);
  lcd.init();           // Serve para iniciar a comunicação com o display já conectado
  //lcd.backlight();    // Serve para ligar a luz do display
  lcd.setContrast(255);
  lcd.setBacklight(255);
  lcd.noAutoscroll();
  lcd.noBlink();
  lcd.clear();                // Serve para limpar a tela do display
  usbProto.setBaudRate(9600); // Envia e recebe a 9600 baud
  //Ajusta o tamanho das strings default em dispArray
  for (int i = 0; i < ROW; i++) {
    dispArray[i].messageSize = strlen(dispArray[i].message);
    dispArray[i].defaultMessageSize = strlen(dispArray[i].defaultMessage);
  }
}

/**
 * @brief Loop principal do firmware.
 *
 * O `loop()` executa continuamente o ciclo de atualização do display,
 * recepção de mensagens da TV-Box e execução dos comandos recebidos.
 *
 * O comportamento segue o protocolo definido:
 * - **PING (100):** responde com uptime em ms e versão do firmware.
 * - **TIME (200):** atualiza linha 0 (sala, data, hora).
 * - **LECTURE_NAME (300):** atualiza linha 1 (nome da palestra).
 * - **SPEAKER (400):** atualiza linha 2 (nome do professor).
 * - **ATTENDEE (500):** atualiza linha 3 (participante) e força atualização imediata.
 * - **SUCCESS (600):** feedback sonoro curto (registro aceito).
 * - **FAIL (601):** feedback sonoro duplo (registro rejeitado).
 *
 * ### Estrutura do loop
 * 1. Atualiza o display (`atualizaDisplay(-1)`).
 * 2. Recebe frame via `usbProto.receiveFrame()`.
 * 3. Se um frame válido foi recebido (`machState == RECEIVED`):
 *    - Chama `parseMessage()` para decodificar.
 *    - Executa ação conforme `netMessage.code`.
 *    - Responde sempre `"002|OK"` após comandos de atualização.
 *    - Atualiza mensagens em `dispArray` (conteúdo, tamanho, TTL, rolagem).
 *    - Gera sinais sonoros quando uma digital for lida ou usuário/senha do teclado.
 *    - Reinicia estado da máquina (`machState = START`).
 *    - Reinicia o ciclo (`goto CONTINUE`) sem esperar o `delay`.
 * 4. Se nada foi recebido → aguarda `LOOP_DELAY` antes do próximo ciclo.
 *
 * @note
 * - O `goto CONTINUE` garante responsividade, reiniciando o ciclo imediatamente
 *   após processar uma mensagem (sem aguardar `LOOP_DELAY`).
 * - O uso de `atualizaDisplay(3)` no caso `ATTENDEE` deixa a linha 3 mais
 *   responsiva a eventos de digitação no teclado.
 * - A comunicação usa `usbProto`, responsável por framing com caracteres de escape.
 *
 * @see atualizaDisplay
 * @see parseMessage
 */
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
        //Retorna "001|UPTIME em milissegundos|VERSION"
        strReply[0] = '\0';
        strcat(strReply, "001|");
        uptime = millis();
        itoa( uptime, auxStr, 10);
        strcat(strReply, auxStr);
        strcat(strReply, "|");
        strcat(strReply, VERSION);
        usbProto.sendFrame(strReply);
        break;
      case TIME:
        usbProto.sendFrame("002|OK");
        strcpy(dispArray[0].message, netMessage.message);
        dispArray[0].messageSize = strlen(netMessage.message);
        dispArray[0].TTL = millis() + netMessage.TTL;
        dispArray[0].startPosition = 0;
        dispArray[0].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case LECTURE_NAME:
        usbProto.sendFrame("002|OK");
        strcpy(dispArray[1].message, netMessage.message);
        dispArray[1].messageSize = strlen(netMessage.message);
        dispArray[1].TTL = millis() + netMessage.TTL;
        dispArray[1].startPosition = 0;
        dispArray[1].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case SPEAKER:
        usbProto.sendFrame("002|OK");
        strcpy(dispArray[2].message, netMessage.message);
        dispArray[2].messageSize = strlen(netMessage.message);
        dispArray[2].TTL = millis() + netMessage.TTL;
        dispArray[2].startPosition = 0;
        dispArray[2].keepAtZeroPosition = KEEP_AT_ZERO;
        break;
      case ATTENDEE:
        usbProto.sendFrame("002|OK");
        strcpy(dispArray[3].message, netMessage.message);
        dispArray[3].messageSize = strlen(netMessage.message);
        dispArray[3].TTL = millis() + netMessage.TTL;
        dispArray[3].startPosition = 0;
        dispArray[3].keepAtZeroPosition = KEEP_AT_ZERO;
        atualizaDisplay(3);  //Atualiza forçosamente só a linha 3, o display fica mais responsivo a tecladas rápidas.
        break;

      case SETTIME:
        usbProto.sendFrame("002|OK");
        char * strtokIndx; // this is used by strtok() as an index
        unsigned int year;
        byte month;
        byte day;
        byte hour;
        byte minute;
        byte second;
        strtokIndx = strtok(netMessage.message,":");      // Pega o ano
        year = atoi(strtokIndx);
        strtokIndx = strtok(NULL, ":");                   // Pega o mês
        month = atoi(strtokIndx);
        strtokIndx = strtok(NULL, ":");                   // Pega o dia
        day = atoi(strtokIndx);
        strtokIndx = strtok(NULL, ":");                   // Pega a hora
        hour = atoi(strtokIndx);      
        strtokIndx = strtok(NULL, ":");                   // Pega o minuto
        minute = atoi(strtokIndx); 
        strtokIndx = strtok(NULL, ":");                   // Pega o segundo
        second = atoi(strtokIndx);        
        rtc.adjust(DateTime(year, month, day, hour, minute, second));          
        break;  
          
      case GETTIME:
        strReply[0] = '\0';
        now = rtc.now();  
        char tempBuf[12]; 
        //Converte um float para uma string
        dtostrf(rtc.getTemperature(), 4, 2, tempBuf); // largura=4, casas decimais=2
        
        sprintf(strReply, "003|%04d:%02d:%02d:%02d:%02d:%02d|%s",now.year(),
                                                                 now.month(),
                                                                 now.day(),
                                                                 now.hour(),
                                                                 now.minute(),
                                                                 now.second(),
                                                                 tempBuf);
                                                                 
        usbProto.sendFrame(strReply);
        break;
        
      case SUCCESS:
        usbProto.sendFrame("002|OK");
        tone(BUZZER,1000,150);
        break;
      case FAIL:
        usbProto.sendFrame("002|OK");
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