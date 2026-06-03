#ifndef LINES_H
#define LINES_H

#include <unistd.h>

/* CONSTANTES */
#define MAX_USERNAME 256   /* longitud máxima del nombre */
#define MAX_MSG      256   /* longitud máxima del mensaje de texto */
#define MAX_PENDING  100   /* mensajes pendientes por usuario */
#define MAX_IP       64    /* longitud máxima de la IP almacenada */
#define MAX_PORT     16    /* longitud máxima del puerto (string) */
#define MAX_FILE     256   /* longitud máxima del nombre de un archivo */

/* ESTRUCTURAS DE DATOS */

/* representa un mensaje pendiente de entregar a un usuario desconectado */
struct Mensaje {
    unsigned int id;                 /* identificador numérico del mensaje */
    char remitente[MAX_USERNAME];    /* nombre del usuario que lo envió */
    char contenido[MAX_MSG];         /* contenido del mensaje */
    char fichero[MAX_FILE];          /* nombre del archivo adjunto (vacio si es un SEND normal)*/
};

/* representa un usuario registrado en el sistema, funciona como un nodo de una lista enlazada */
struct Usuario {
    int  activo;                        
    char nombre[MAX_USERNAME];                    /* nombre del usuario */
    int  conectado;                               /* 1 si el usuario tiene una sesión activa, 0 si está desconectado */
    char ip[MAX_IP];                              /* IP del hilo de escucha del cliente */
    char puerto[MAX_PORT];                        /* puerto del hilo de escucha */
    unsigned int msg_counter;                     /* último id asignado a sus mensajes */
    struct Mensaje pendientes[MAX_PENDING];       /* cola de mensajes sin entregar */
    int n_pendientes;                             /* cuántos mensajes hay en la cola */
    struct Usuario *next;                         /* puntero al siguiente usuario en la lista enlazada */
};

/* Estructura que pasamos al hilo: socket aceptado + IP del cliente */
struct ThreadArg {
    int  sc;                        /* el descriptor del socket conectado con el cliente (Socket de Cliente) */
    char ip_cliente[MAX_IP];        /* la IP desde la que se conectó el cliente (obtenida en el accept) */
};

/* cabeceras de las funciones de comunicación definidas en lines.h */
int sendMessage(int socket, char *buffer, int len);
int recvMessage(int socket, char *buffer, int len);
ssize_t readLine(int fd, void *buffer, size_t n);

#endif