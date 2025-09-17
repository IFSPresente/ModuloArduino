#ifndef FRAME_H      // começa o include guard
#define FRAME_H

#include <Arduino.h>
#define MAX_STRING    80
#define MAX_PROTOCOL_MESSAGE  MAX_STRING + 4  // ddd,maior string já desprezados os caracteres de inicio e fim '<' e '>'

enum machineState {START, RECEIVING, ESCAPE, RECEIVED};

//extern byte machState;
//extern char receivedChars[];
//void receiveFrame();

class SerialProtocol {
	public:
		byte machState;
		char receivedChars[MAX_PROTOCOL_MESSAGE+1];
		char sendChars[MAX_PROTOCOL_MESSAGE+1];
		
		SerialProtocol();
		virtual ~SerialProtocol();
		void receiveFrame();
		void sendFrame(char* message);
		void removeAccentMarker(char* str);
		void setBaudRate(int baudRate);
};

#endif // FRAME_H