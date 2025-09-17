#include "frame.h"

// Tabela de conversão para remover acentuação de textos.
// Infelimente, o display de 4 linhas é limitado e não aceita acentuações da
// Língua Portuguesa.
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

/*****************************************************************************/
/* Construtor                                                                */
/*****************************************************************************/
SerialProtocol::SerialProtocol():machState(START)
{
}

/*****************************************************************************/
/* Destrutor                                                                 */
/*****************************************************************************/
SerialProtocol::~SerialProtocol()
{
}

/*****************************************************************************/
/* setBaudRate()                                                             */
/*****************************************************************************/
void SerialProtocol::setBaudRate(int baudRate) {
			Serial.begin(baudRate); // send and receive at 9600 baud
}

/*****************************************************************************/
/* removeAccentMarker()                                                      */
/*****************************************************************************/
void SerialProtocol::removeAccentMarker(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        str[i] = win1252_to_ascii[ (unsigned char) str[i] ];
    }
}


/*****************************************************************************/
/* Não há timeout na função Serial.read().                                   */
/* Para contornar a limitação, a recepção de um frame completo pode envolver */
/* várias invocações desta função, daí as variáveis ndx e recvInProgress     */
/* serem estáticas.                                                          */
/* no loop() do Arduino, continuará invocando esta função até que o frame    */
/* se complete.                                                              */
/*****************************************************************************/
void SerialProtocol::receiveFrame() 
{
	static byte ndx = 0;
	unsigned char rc;
	while (Serial.available() > 0 && machState != RECEIVED) {
        rc = Serial.read();
		switch (machState) {
		   case START:
		     switch (rc) {
			    case '<':
				  ndx = 0;
				  machState = RECEIVING;
				  break;
				default:
				  machState = START;
				  break;
			 }
		     break;
		   case RECEIVING:
		     switch (rc) {
				case '<':
				  ndx = 0;
				  machState = RECEIVING;
				  break;
			    case '>':
				  machState = RECEIVED;
				  receivedChars[ndx] = '\0';
				  ndx = 0;
				  break;
			    case '\\':
				  machState = ESCAPE;
				  break;
				default:
				  machState = RECEIVING;
				  receivedChars[ndx++] = rc;
				  break;
			 }
		     break;
		   case ESCAPE:
		     switch (rc) {
			    case '>':
				case '\\':
				case '<':
				  machState = RECEIVING;
				  receivedChars[ndx++] = rc;
				  break;
				default:
				  machState = RECEIVING;
				  break;
			 }
		     break;
		   case RECEIVED:
		     // Não ocorre...
		     break;
		}
	}
}

/*****************************************************************************/
/* Uma mensagem é inserida num frame.                                        */
/* Algo como, "mensagem" vira "<mensagem>".                                  */
/* Se a mensagem contiver '>', '<', ou '\', deve ficar com '\' antecedendo.  */
/* Também há uma limitação importante, não se pode ultrapassar o tamanho     */
/* máximo do buffer alocado, MAX_PROTOCOL_MESSAGE+1.                         */
/*****************************************************************************/
void SerialProtocol::sendFrame(char* message) {
	int i = 0;
	sendChars[i++] = '<';
	while( *message != '\0' && i < (MAX_PROTOCOL_MESSAGE - 1) ) {
		switch (*message) {
			case '<':
			case '>':
			case '\\':
			  sendChars[i++] = '\\';
			  if (i == MAX_PROTOCOL_MESSAGE - 1) //Trunca a mensagem, pois só cabe o '>' e '\0' finalizador.
				  continue;			             //Não faz break, pois quer copiar o caractere seguinte em toda situação	  
			default:
			  sendChars[i++] = *message++;	
              break;			  
		}
	}
	sendChars[i++] = '>';
	sendChars[i]   = '\0';
	Serial.write(sendChars);
}