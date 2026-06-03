#include <unistd.h>
#include <errno.h>
#include "lines.h"

// TCP no garantiza que se envíe todos los bytes de golpe en un write() si el buffer esta lleno
// esta función sigue escribiendo hasta mandar todo, envía exactamente len bytes por el socket
int sendMessage(int socket, char *buffer, int len)
{
    int enviado = 0;  // cuantos bytes llevamos enviados
    
    // mientras queden bytes por enviar
    while (enviado < len) {
		// intentar escribir la cantidad restante (len - enviado) desde la posición actual del buffer
        int r = write(socket, buffer + enviado, len - enviado);
        
        if (r < 0) {
            return -1;  // error en write
        }
        enviado += r;	// sumamos lo que si se ha enviado en esta iteración
    }
    return 0;  // exito total, se han enviado len bytes
}

// esta función espera y acumula hasta recibir exactamente len bytes del socket
int recvMessage(int socket, char *buffer, int len)
{
    int recibido = 0;
    
	// mientras queden bytes por recibir
    while (recibido < len) {
		// lemos desde el socket y lo guardamos en la posición actual del buffer, intentando leer la cantidad restante (len - recibido)
        int r = read(socket, buffer + recibido, len - recibido);
        
        if (r <= 0) {
            return -1;  // error en read
        }
        recibido += r;
    }
    return 0;  // exito total, se han recibido len bytes
}

// lee bytes de un socket uno por uno hasta encontrar un salto de línea o un byte nulo, o hasta llenar el buffer (n-1 bytes para dejar espacio al '\0')	
ssize_t readLine(int fd, void *buffer, size_t n)
{
	ssize_t numRead;  /* num of bytes fetched by last read() */
	size_t totRead;	  /* total bytes read so far */
	char *buf;
	char ch;

	// validación de parámetros
	if (n <= 0 || buffer == NULL) { 
		errno = EINVAL;
		return -1; 
	}

	buf = buffer;
	totRead = 0;
	
	// bucle infinito que iremos rompiendo internamente
	for (;;) {
        	numRead = read(fd, &ch, 1);		// leemos un byte del socket

        	if (numRead == -1) {	
            		if (errno == EINTR)		// si una señal interrumpió el read(), lo volvemos a intentar
                		continue;
            	else
			return -1;		// otro tipo de error grave
        	} else if (numRead == 0) {	// eof (conexión cerrada por el otro lado)
            		if (totRead == 0)	// si no hemos leído nada, devolvemos 0
                		return 0;
			else
                		break;	// si ya hemos leído algo, lo devolvemos aunque no haya un salto de línea
        	} else {			// si leimos un caracter valido
            		if (ch == '\n')	// salto de línea --> fin de lectura
                		break;
            		if (ch == '\0')	// final de cadena de C --> fin de lectura
                		break;
            		if (totRead < n - 1) {	// si aun queda espacio en el buffer
						totRead++;
						*buf++ = ch; // guardamsos el caracter en el buffer y avanzamos el puntero del buffer
					}
			} 
	}
	
	*buf = '\0';  // aseguramos que el buffer finaliza como una cadena de C válida
    	return totRead;
}