#ifndef FRAME_H      // começa o include guard
#define FRAME_H

#include <Arduino.h>
#define MAX_STRING    80
#define MAX_PROTOCOL_MESSAGE  MAX_STRING + 4  // ddd,maior string já desprezados os caracteres de inicio e fim '<' e '>'

/**
 * @enum machineState
 * @brief Estados possíveis da máquina de recepção de frames.
 */
enum machineState {START, RECEIVING, ESCAPE, RECEIVED};

/**
 * @class SerialProtocol
 * @brief Gerencia a comunicação serial no protocolo master/slave usado pela TV-Box.
 *
 * Esta classe é responsável por:
 * - Receber e montar mensagens do tipo `<codigo,mensagem,TTL>`.
 * - Enviar mensagens serializadas para a TV-Box.
 * - Remover caracteres de acento que podem interferir na comunicação.
 * - Configurar a taxa de transmissão serial.
 *
 * @note O buffer `receivedChars` armazena a mensagem recebida,
 *       `sendChars` armazena a mensagem a ser enviada.
 *
 * @section img_sec Máquina de Estado do protocolo
 * \image html img/MaquinaEstadoProtocolo.png "Máquina de Estados"
 */
class SerialProtocol {
	public:
		/**
		* @brief Estado atual da máquina de recepção.
		*/
		byte machState;
		/**
		* @brief Buffer para armazenar a mensagem recebida.
		*/
		char receivedChars[MAX_PROTOCOL_MESSAGE+1];
		/**
		* @brief Buffer para armazenar a mensagem a ser enviada.
		*/
		char sendChars[MAX_PROTOCOL_MESSAGE+1];
		
		/**
		* @brief Construtor padrão da classe SerialProtocol.
		*
		* Inicializa os buffers e coloca a máquina em estado START.
		*/
		SerialProtocol();/**
		* @brief Destrutor virtual.
		*
		* Permite que classes derivadas possam sobrescrever o destrutor.
		*/
		virtual ~SerialProtocol();
		/**
		* @brief Recebe um frame da TV-Box e atualiza o buffer `receivedChars`.
		*
		* A máquina de estados interpreta os caracteres de início/fim
		* e caracteres de escape.
		*
		* @see machState
		*/
		void receiveFrame();
		/**
		* @brief Envia uma mensagem via serial para a TV-Box.
		*
		* @param message Mensagem a ser enviada. Deve estar formatada
		*                de acordo com o protocolo `<codigo,mensagem,TTL>`.
		*/
		void sendFrame(char* message);
		/**
		* @brief Remove acentos e caracteres especiais de uma string.
		*
		* Isso evita problemas de transmissão serial com caracteres acentuados.
		*
		* @param str String a ser processada.
		*/
		void removeAccentMarker(char* str);
		/**
		* @brief Configura a taxa de transmissão serial.
		*
		* @param baudRate Taxa em bauds (ex.: 9600, 115200).
		*/
		void setBaudRate(int baudRate);
};

#endif // FRAME_H