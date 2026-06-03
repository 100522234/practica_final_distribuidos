#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include "lines.h"
#include "logger.h"

/* Array global de usuarios y su mutex */
static struct Usuario *head_usuarios = NULL; /* cabeza de la lista enlazada de usuarios */
static pthread_mutex_t mutex_usuarios = PTHREAD_MUTEX_INITIALIZER;  /* mutex para sincronizar el acceso a la lista enlazada head_usuarios, impide condiciones de carrera*/


/* FUNCIONES AUXILIARES SOBRE EL ARRAY DE USUARIOS */

/* Busca un usuario por nombre; devuelve puntero al nodo o NULL si no existe */
static struct Usuario *buscar_usuario(const char *nombre)
{
    struct Usuario *aux = head_usuarios;
    while (aux != NULL) {
        if (strcmp((*aux).nombre, nombre) == 0)
            return aux;
        aux = (*aux).next;
    }
    return NULL;
}

/* INTEGRACION RPC */

static void notificar_rpc(const char *usuario, const char *operacion) {
    /* Leemos la variable de entorno para saber dónde está el servidor RPC */
    char *host = getenv("LOG_RPC_IP");
    if (host == NULL) {
        /* Si la variable no está definida, no hacemos nada */
        return;
    }

    /* Creamos el cliente RPC */
    CLIENT *clnt = clnt_create(host, LOG_PROG, LOG_VERS, "tcp");
    if (clnt == NULL) {
        clnt_pcreateerror(host);
        return;
    }

    /* Hacemos la llamada RPC */
    int *result = imprimir_operacion_rpc_1((char *)usuario, (char *)operacion, clnt);  

    if (result == (int *) NULL) {
        clnt_perror(clnt, "Fallo en la llamada RPC");
    }
    
    clnt_destroy(clnt);
}

/* CAPA DE RED: ENVÍO SERVIDOR --> CLIENTE */

static int enviar_mensaje_a_cliente(const char *ip, const char *puerto, const char *remitente, unsigned int id, const char *contenido)
{
    /* resolver host */
    struct hostent *hp = gethostbyname(ip);
    if (!hp) return -1;

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) return -1;

    /* configuracion del bloque de direcciones del destinatario */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, (*hp).h_addr_list[0], (*hp).h_length);
    addr.sin_port = htons((unsigned short)atoi(puerto));

    /* conecta al hilo de escucha P2P/servidor del cliente receptor*/
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sd);
        return -1;
    }

    /* operación: "SEND_MESSAGE" */
    if (sendMessage(sd, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1) == -1) {
        close(sd); return -1;
    }
    /* remitente */
    if (sendMessage(sd, (char *)remitente, strlen(remitente) + 1) == -1) {
        close(sd); return -1;
    }
    /* id como cadena */
    char identificador_str[32];
    snprintf(identificador_str, sizeof(identificador_str), "%u", id);
    if (sendMessage(sd, identificador_str, strlen(identificador_str) + 1) == -1) {
        close(sd); return -1;
    }
    /* contenido */
    if (sendMessage(sd, (char *)contenido, strlen(contenido) + 1) == -1) {
        close(sd); return -1;
    }

    close(sd);
    return 0;
}

/* mismo procedimiento que enviar_mensaje_a_cliente, pero maneja archivos adjuntos */
static int enviar_mensaje_attach_a_cliente(const char *ip, const char *puerto, const char *remitente, unsigned int id, const char *contenido, const char *fichero)
{
    struct hostent *hp = gethostbyname(ip);
    if (!hp) return -1;

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, (*hp).h_addr_list[0], (*hp).h_length);
    addr.sin_port = htons((unsigned short)atoi(puerto));

    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sd); return -1;
    }

    if (sendMessage(sd, "SEND MESSAGE_ATTACH", strlen("SEND MESSAGE_ATTACH") + 1) == -1) { close(sd); return -1; }
    if (sendMessage(sd, (char *)remitente, strlen(remitente) + 1) == -1) { close(sd); return -1; }
    
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);
    if (sendMessage(sd, id_str, strlen(id_str) + 1) == -1) { close(sd); return -1; }
    if (sendMessage(sd, (char *)contenido, strlen(contenido) + 1) == -1) { close(sd); return -1; }
    
    // variable enviada que corresponde al string del nombre del fichero adjunto
    if (sendMessage(sd, (char *)fichero, strlen(fichero) + 1) == -1) { close(sd); return -1; }

    close(sd);
    return 0;
}

// envia nodificacion de ACK al remitente de un archivo adjunto confirmando la entrega
static void confirmar_entrega_attach(const char *ip, const char *puerto, unsigned int id, const char *fichero)
{
    struct hostent *hp = gethostbyname(ip);
    if (!hp) return;

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, (*hp).h_addr_list[0], (*hp).h_length);
    addr.sin_port = htons((unsigned short)atoi(puerto));

    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(sd); return; }

    if (sendMessage(sd, "SEND_MESS_ATTACH_ACK", strlen("SEND_MESS_ATTACH_ACK") + 1) == -1) { close(sd); return; }
    
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);
    sendMessage(sd, id_str, strlen(id_str) + 1);
    sendMessage(sd, (char *)fichero, strlen(fichero) + 1);

    close(sd);
}

/* Avisa al usuario que envió un mensaje de que ese mensaje fue entregado correctamente al destinatario */
static void confirmar_entrega(const char *ip, const char *puerto, unsigned int id)
{
    /* resolver el nombre de host a dirección IP */
    struct hostent *hp = gethostbyname(ip);
    if (!hp) return;

    /* crear socket TCP */
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) return;

    /* rellenar la estructura de dirección del cliente remitente */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, (*hp).h_addr_list[0], (*hp).h_length);
    addr.sin_port = htons((unsigned short)atoi(puerto));

    /* paso 1: conectar al hilo de escucha del remitente */
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sd); return;
    }

    /* paso 2: enviar "SEND_MESS_ACK\0" indicando la operación */
    if (sendMessage(sd, "SEND_MESS_ACK", strlen("SEND_MESS_ACK") + 1) == -1) {
        close(sd); return;
    }

    /* paso 3: enviar el id del mensaje entregado como cadena */
    char identificador_str[32];
    snprintf(identificador_str, sizeof(identificador_str), "%u", id);
    sendMessage(sd, identificador_str, strlen(identificador_str) + 1);

    /* paso 4: cerrar la conexión */
    close(sd);
}

/* LOGICA DE OPERACIONES*/
/* tipo_opES */

/* Gestiona el registro de un nuevo usuario en el sistema */
static void op_registro(int sc)
{
    /* leer el nombre de usuario enviado por el cliente */
    char nombre[MAX_USERNAME];
    if (readLine(sc, nombre, MAX_USERNAME) <= 0) return;
    notificar_rpc(nombre, "REGISTER");

    /* bloquear el mutex para que solo 1 hilo pueda acceder al array de usuarios de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* comprobar si ya existe un usuario con ese nombre */
    if (buscar_usuario(nombre) != NULL) {
        /* usuario ya registrado -->  devolver código 1 al cliente */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        printf("s> REGISTER %s FAIL\n", nombre);
        return;
    }

    struct Usuario *nuevo = malloc(sizeof(struct Usuario));
    if (nuevo == NULL) {
        /* fallo de malloc --> error */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 2;
        sendMessage(sc, &cod, 1);
        printf("s> REGISTER %s FAIL\n", nombre);
        return;
    }
    /* inicializar el nodo nuevo, en la cabeza de la lista */
    memset(nuevo, 0, sizeof(struct Usuario));
    strncpy((*nuevo).nombre, nombre, MAX_USERNAME - 1);
    (*nuevo).next = head_usuarios;
    head_usuarios = nuevo;

    /* liberar el mutex antes de responder */
    pthread_mutex_unlock(&mutex_usuarios);

    /* devolver código 0 (éxito) al cliente */
    char cod = 0;
    sendMessage(sc, &cod, 1);
    printf("s> REGISTER %s OK\n", nombre);
}

/* Gestiona la baja de un usuario en el sistema */
static void op_baja(int sc)
{
    /* leer el nombre de usuario enviado por el cliente */
    char nombre[MAX_USERNAME];
    if (readLine(sc, nombre, MAX_USERNAME) <= 0) return;
    notificar_rpc(nombre, "UNREGISTER");

    /* bloquear el mutex para acceder al array de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* recorrer la lista para encontrar el nodo y su anterior */
    struct Usuario *aux = head_usuarios;
    struct Usuario *ant = NULL;
    while (aux != NULL && strcmp((*aux).nombre, nombre) != 0) {
        ant = aux;
        aux = (*aux).next;
    }
    if (aux == NULL) {
        /* usuario no encontrado --> devolver código 1 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        printf("s> UNREGISTER %s FAIL\n", nombre);
        return;
    }
    /* desenlazar el nodo de la lista */
    if (ant == NULL)
        head_usuarios = (*aux).next;
    else
        (*ant).next = (*aux).next;
    free(aux); /* liberar la memoria del nodo */

    /* liberar el mutex antes de responder */
    pthread_mutex_unlock(&mutex_usuarios);

    /* devolver código 0 (éxito) al cliente */
    char cod = 0;
    sendMessage(sc, &cod, 1);
    printf("s> UNREGISTER %s OK\n", nombre);
}

/* Gestiona la conexión de un usuario */
static void op_conexion(int sc, const char *ip_cliente)
{
    /* leer nombre de usuario y puerto de escucha enviados por el cliente */
    char nombre[MAX_USERNAME];
    char puerto[MAX_PORT];

    if (readLine(sc, nombre, MAX_USERNAME) <= 0) return;
    if (readLine(sc, puerto,  MAX_PORT) <= 0) return;
    notificar_rpc(nombre, "CONNECT");

    /* bloquear el mutex para acceder al array de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* verificar que el usuario existe */
    struct Usuario *u = buscar_usuario(nombre);
    if (u == NULL) {
        /* usuario no registrado --> devolver código 1 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        printf("s> CONNECT %s FAIL\n", nombre);
        return;
    }

    /* verificar que el usuario no está ya conectado */
    if ((*u).conectado) {
        /* ya conectado --> devolver código 2 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 2;
        sendMessage(sc, &cod, 1);
        printf("s> CONNECT %s FAIL\n", nombre);
        return;
    }

    /* asignar recursos de red e inicializar conectividad*/
    (*u).conectado = 1;
    strncpy((*u).ip, ip_cliente, MAX_IP - 1);
    strncpy((*u).puerto, puerto, MAX_PORT - 1);

    /* copiar el buffer de mensajes pendientes locales al hilo para no bloquear el mutex */
    int n = (*u).n_pendientes;
    struct Mensaje pendientes_copia[MAX_PENDING];
    memcpy(pendientes_copia, (*u).pendientes, n * sizeof(struct Mensaje));
    (*u).n_pendientes = 0;

    /* liberar el mutex antes de responder y entregar mensajes */
    pthread_mutex_unlock(&mutex_usuarios);

    /* devolver código 0 (éxito) al cliente */
    char cod = 0;
    sendMessage(sc, &cod, 1);
    printf("s> CONNECT %s OK\n", nombre);

    /* entregar mensajes pendientes uno a uno */
    for (int i = 0; i < n; i++) {
        int ok;
        int tiene_adjunto = strlen(pendientes_copia[i].fichero) > 0;
        
        if (tiene_adjunto) {
            ok = enviar_mensaje_attach_a_cliente(ip_cliente, puerto, pendientes_copia[i].remitente, pendientes_copia[i].id,
                                                pendientes_copia[i].contenido, pendientes_copia[i].fichero);
        } else {
            ok = enviar_mensaje_a_cliente(ip_cliente, puerto, pendientes_copia[i].remitente, pendientes_copia[i].id,
                                        pendientes_copia[i].contenido);
        }

        if (ok == 0) {
            printf("s> SEND MESSAGE %u FROM %s TO %s\n", pendientes_copia[i].id, pendientes_copia[i].remitente, nombre);

            /* confirmar recibo al remitente original si sigue vivo */
            pthread_mutex_lock(&mutex_usuarios);
            struct Usuario *rem = buscar_usuario(pendientes_copia[i].remitente);
            if (rem != NULL && (*rem).conectado) {
                char ip_rem[MAX_IP], puerto_rem[MAX_PORT];
                strncpy(ip_rem, (*rem).ip, MAX_IP - 1);
                strncpy(puerto_rem, (*rem).puerto, MAX_PORT - 1);
                pthread_mutex_unlock(&mutex_usuarios);
                
                if (tiene_adjunto) {
                    confirmar_entrega_attach(ip_rem, puerto_rem, pendientes_copia[i].id, pendientes_copia[i].fichero);
                } else {
                    confirmar_entrega(ip_rem, puerto_rem, pendientes_copia[i].id);
                }
            } else {
                pthread_mutex_unlock(&mutex_usuarios);
            }
        } else {
            /* entrega fallida: volver a encolar el mensaje como pendiente */
            pthread_mutex_lock(&mutex_usuarios);
            struct Usuario *reencolar = buscar_usuario(nombre);
            if (reencolar != NULL && (*reencolar).n_pendientes < MAX_PENDING) {
                (*reencolar).pendientes[(*reencolar).n_pendientes++] = pendientes_copia[i];
            }
            pthread_mutex_unlock(&mutex_usuarios);
        }
    }
}

/* Gestiona la desconexión de un usuario */
static void op_desconexion(int sc)
{
    /* leer el nombre de usuario enviado por el cliente */
    char nombre[MAX_USERNAME];
    if (readLine(sc, nombre, MAX_USERNAME) <= 0) return;
    notificar_rpc(nombre, "DISCONNECT");

    /* bloquear el mutex para acceder al array de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* verificar que el usuario existe */
    struct Usuario *u = buscar_usuario(nombre);
    if (u == NULL) {
        /* usuario no registrado --> devolver código 1 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        printf("s> DISCONNECT %s FAIL\n", nombre);
        return;
    }

    /* verificar que el usuario está efectivamente conectado */
    if (!(*u).conectado) {
        /* no estaba conectado --> devolver código 2 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 2;
        sendMessage(sc, &cod, 1);
        printf("s> DISCONNECT %s FAIL\n", nombre);
        return;
    }

    /* marcar como desconectado y limpiar IP y puerto de escucha */
    (*u).conectado = 0;
    memset((*u).ip, 0, MAX_IP);
    memset((*u).puerto, 0, MAX_PORT);

    /* liberar el mutex antes de responder */
    pthread_mutex_unlock(&mutex_usuarios);

    /* devolver código 0 (éxito) al cliente */
    char cod = 0;
    sendMessage(sc, &cod, 1);
    printf("s> DISCONNECT %s OK\n", nombre);
}


/* SEND */

/* Gestiona el envío de un mensaje de un usuario a otro */
static void op_envio(int sc)
{
    /* leer remitente, destinatario y texto del mensaje */
    char remitente[MAX_USERNAME];
    char destinatario[MAX_USERNAME];
    char contenido[MAX_MSG];

    if (readLine(sc, remitente, MAX_USERNAME) <= 0) return;
    if (readLine(sc, destinatario, MAX_USERNAME) <= 0) return;
    if (readLine(sc, contenido, MAX_MSG) <= 0) return;
    notificar_rpc(remitente, "SEND");

    /* bloquear el mutex para acceder a la lista de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* verificar que el destinatario existe */
    struct Usuario *dest = buscar_usuario(destinatario);
    if (dest == NULL) {
        /* destinatario no registrado --> devolver código 1 */
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        return;
    }

    /* incrementar el contador de mensajes del remitente para asignar un id único */
    struct Usuario *rem = buscar_usuario(remitente);
    unsigned int identificador;
    if (rem != NULL) {
        (*rem).msg_counter++;
        if ((*rem).msg_counter == 0)
            (*rem).msg_counter = 1; /* saltar el 0 tras overflow */
        identificador = (*rem).msg_counter;
    } else {
        /* remitente no registrado: usar id=1 igualmente */
        identificador = 1;
    }

    /* preparar la estructura del mensaje con id, remitente y contenido */
    struct Mensaje msg;
    msg.id = identificador;
    strncpy(msg.remitente, remitente, MAX_USERNAME - 1);
    strncpy(msg.contenido, contenido, MAX_MSG - 1);

    /* guardar el mensaje en la cola de pendientes del destinatario */
    int mensaje_guardado = 0;
    if ((*dest).n_pendientes < MAX_PENDING) {
        (*dest).pendientes[(*dest).n_pendientes++] = msg;
        mensaje_guardado = 1;
    }

    /* copiar datos de conexión del destinatario para usarlos fuera del mutex */
    int  dest_conectado = (*dest).conectado;
    char ip_dest[MAX_IP], puerto_dest[MAX_PORT];
    strncpy(ip_dest, (*dest).ip, MAX_IP - 1);
    strncpy(puerto_dest, (*dest).puerto, MAX_PORT - 1);

    /* copiar datos del remitente para enviarle el ACK fuera del mutex */
    char ip_rem[MAX_IP], rem_puerto[MAX_PORT];
    int  rem_conectado = 0;
    if (rem != NULL) {
        rem_conectado = (*rem).conectado;
        strncpy(ip_rem, (*rem).ip, MAX_IP - 1);
        strncpy(rem_puerto, (*rem).puerto, MAX_PORT - 1);
    }

    /* liberar el mutex antes de responder y entregar */
    pthread_mutex_unlock(&mutex_usuarios);

    /* si la cola estaba llena no se pudo guardar --> error */
    if (!mensaje_guardado) {
        char cod = 2;
        sendMessage(sc, &cod, 1);
        return;
    }

    /* responder OK al remitente e indicarle el id asignado al mensaje */
    char cod = 0;
    sendMessage(sc, &cod, 1);
    char identificador_str[32];
    snprintf(identificador_str, sizeof(identificador_str), "%u", identificador);
    sendMessage(sc, identificador_str, strlen(identificador_str) + 1);

    /* si el destinatario está conectado, intentar entrega inmediata */
    if (dest_conectado) {
        int ok = enviar_mensaje_a_cliente(ip_dest, puerto_dest,
                                          remitente, identificador, contenido);
        if (ok == 0) {
            /* entrega exitosa: eliminar el mensaje de la cola de pendientes */
            pthread_mutex_lock(&mutex_usuarios);
            dest = buscar_usuario(destinatario);
            if (dest != NULL) {
                /* buscar el mensaje por id y desplazar los siguientes una posición */
                for (int i = 0; i < (*dest).n_pendientes; i++) {
                    if ((*dest).pendientes[i].id == identificador) {
                        for (int j = i; j < (*dest).n_pendientes - 1; j++)
                            (*dest).pendientes[j] = (*dest).pendientes[j + 1];
                        (*dest).n_pendientes--;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&mutex_usuarios);

            printf("s> SEND MESSAGE %u FROM %s TO %s\n",
                   identificador, remitente, destinatario);

            /* notificar al remitente si sigue conectado */
            if (rem_conectado)
                confirmar_entrega(ip_rem, rem_puerto, identificador);

        } else {
            /* error al entregar: asumir que el destinatario se desconectó y dejar el mensaje en la cola como pendiente */
            pthread_mutex_lock(&mutex_usuarios);
            dest = buscar_usuario(destinatario);
            if (dest != NULL) {
                (*dest).conectado = 0;
                memset((*dest).ip, 0, MAX_IP);
                memset((*dest).puerto, 0, MAX_PORT);
            }
            pthread_mutex_unlock(&mutex_usuarios);
            printf("s> MESSAGE %u FROM %s TO %s STORED\n",
                   identificador, remitente, destinatario);
        }
    } else {
        /* destinatario desconectado: el mensaje queda almacenado como pendiente */
        printf("s> MESSAGE %u FROM %s TO %s STORED\n",
               identificador, remitente, destinatario);
    }
}

static void op_envio_attach(int sc)
{
    /* bloque igual que op_envio */
    char remitente[MAX_USERNAME];
    char destinatario[MAX_USERNAME];
    char contenido[MAX_MSG];
    char fichero[MAX_FILE];

    if (readLine(sc, remitente, MAX_USERNAME) <= 0) return;
    if (readLine(sc, destinatario, MAX_USERNAME) <= 0) return;
    if (readLine(sc, contenido, MAX_MSG) <= 0) return;
    if (readLine(sc, fichero, MAX_FILE) <= 0) return;

    char operacion_str[512];
    snprintf(operacion_str, sizeof(operacion_str), "SENDATTACH %s", fichero);
    notificar_rpc(remitente, operacion_str);

    pthread_mutex_lock(&mutex_usuarios);

    struct Usuario *dest = buscar_usuario(destinatario);
    struct Usuario *rem = buscar_usuario(remitente);

    if (dest == NULL || rem == NULL) {
        pthread_mutex_unlock(&mutex_usuarios);
        char cod = 1;
        sendMessage(sc, &cod, 1);
        return;
    }

    if (++(*rem).msg_counter == 0) {
        (*rem).msg_counter = 1;
    }
    unsigned int identificador = (*rem).msg_counter;

    struct Mensaje msg;
    msg.id = identificador;
    strncpy(msg.remitente, remitente, MAX_USERNAME - 1);
    strncpy(msg.contenido, contenido, MAX_MSG - 1);
    strncpy(msg.fichero, fichero, MAX_FILE - 1);

    int mensaje_guardado = 0;
    if ((*dest).n_pendientes < MAX_PENDING) {
        (*dest).pendientes[(*dest).n_pendientes++] = msg;
        mensaje_guardado = 1;
    }

    int dest_conectado = (*dest).conectado;
    char ip_dest[MAX_IP], puerto_dest[MAX_PORT];
    strncpy(ip_dest, (*dest).ip, MAX_IP - 1);
    strncpy(puerto_dest, (*dest).puerto, MAX_PORT - 1);

    char ip_rem[MAX_IP], rem_puerto[MAX_PORT];
    int rem_conectado = 0;
    if (rem != NULL) {
        rem_conectado = (*rem).conectado;
        strncpy(ip_rem, (*rem).ip, MAX_IP - 1);
        strncpy(rem_puerto, (*rem).puerto, MAX_PORT - 1);
    }

    pthread_mutex_unlock(&mutex_usuarios);

    if (!mensaje_guardado) {
        char cod = 2;
        sendMessage(sc, &cod, 1);
        return;
    }

    char cod = 0;
    sendMessage(sc, &cod, 1);
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", identificador);
    sendMessage(sc, id_str, strlen(id_str) + 1);

    if (dest_conectado) {
        int ok = enviar_mensaje_attach_a_cliente(ip_dest, puerto_dest, remitente, identificador, contenido, fichero);
        if (ok == 0) {
            pthread_mutex_lock(&mutex_usuarios);
            dest = buscar_usuario(destinatario);
            if (dest != NULL) {
                for (int i = 0; i < (*dest).n_pendientes; i++) {
                    if ((*dest).pendientes[i].id == identificador) {
                        for (int j = i; j < (*dest).n_pendientes - 1; j++)
                            (*dest).pendientes[j] = (*dest).pendientes[j + 1];
                        (*dest).n_pendientes--;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&mutex_usuarios);

            printf("s> SEND MESSAGE %u FROM %s TO %s\n", identificador, remitente, destinatario);

            if (rem_conectado) confirmar_entrega_attach(ip_rem, rem_puerto, identificador, fichero);
        } else {
            pthread_mutex_lock(&mutex_usuarios);
            dest = buscar_usuario(destinatario);
            if (dest != NULL) {
                (*dest).conectado = 0;
                memset((*dest).ip, 0, MAX_IP);
                memset((*dest).puerto, 0, MAX_PORT);
            }
            pthread_mutex_unlock(&mutex_usuarios);
            printf("s> MESSAGE %u FROM %s TO %s STORED\n", identificador, remitente, destinatario);
        }
    } else {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", identificador, remitente, destinatario);
    }
}

/* USERS */

/* Gestiona la petición de un cliente que quiere saber qué usuarios están conectados en ese momento */
static void op_usuarios(int sc)
{
    /* leer el nombre del usuario que solicita la lista */
    char nombre[MAX_USERNAME];
    if (readLine(sc, nombre, MAX_USERNAME) <= 0) return;
    notificar_rpc(nombre, "USERS");

    /* bloquear el mutex para acceder a la lista de forma segura */
    pthread_mutex_lock(&mutex_usuarios);

    /* verificar que el usuario existe y está conectado */
    struct Usuario *u = buscar_usuario(nombre);
    if (u == NULL || !(*u).conectado) {
        pthread_mutex_unlock(&mutex_usuarios);
        char cod;
        if (u == NULL)
            cod = 2; /* usuario no registrado */
        else
            cod = 1; /* usuario no conectado */
        sendMessage(sc, &cod, 1);
        printf("s> CONNECTED USERS FAIL\n");
        return;
    }

    /* recorrer la lista y recopilar los nombres de usuarios conectados */
    /* cambiamos el tamaño del buffer para que quepa la IP, el puerto y los ':' */
    char lista[1024][MAX_USERNAME + MAX_IP + MAX_PORT + 2]; /* buffer temporal para los nombres conectados */
    int  n = 0;
    struct Usuario *aux = head_usuarios;
    while (aux != NULL) {
        if ((*aux).conectado)
            /* guardamos la informacion con el formato exigido --> usuario:IP:puerto */
            snprintf(lista[n++], sizeof(lista[0]), "%s:%s:%s", (*aux).nombre, (*aux).ip, (*aux).puerto);
        aux = (*aux).next;
    }

    /* liberar el mutex antes de enviar la respuesta */
    pthread_mutex_unlock(&mutex_usuarios);

    /* devolver código 0 (éxito) al cliente */
    char cod = 0;
    sendMessage(sc, &cod, 1);

    /* enviar el número de usuarios conectados como cadena */
    char num_conectados_str[16];
    snprintf(num_conectados_str, sizeof(num_conectados_str), "%d", n);
    sendMessage(sc, num_conectados_str, strlen(num_conectados_str) + 1);

    /* enviar cada nombre de usuario conectado uno a uno */
    for (int i = 0; i < n; i++)
        sendMessage(sc, lista[i], strlen(lista[i]) + 1);

    printf("s> CONNECTEDUSERS OK\n");
}

/* HILO POR CONEXIÓN */

/* Punto de entrada de cada hilo. Lee la operación solicitada por el cliente y llama al handler correspondiente */
void *atender_cliente(void *arg)
{
    /* extraemos info y libreamos memoria */
    struct ThreadArg *ta = (struct ThreadArg *)arg;
    int sc = (*ta).sc;
    char ip_cliente[MAX_IP];
    strncpy(ip_cliente, (*ta).ip_cliente, MAX_IP - 1);
    free(ta);

    /* leer el nombre de la operación */
    char tipo_op[32];
    if (readLine(sc, tipo_op, sizeof(tipo_op)) <= 0) {
        close(sc);
        pthread_exit(NULL);
    }

    /* segun lo que diga llamamos a la funcion correspondiente */
    if (strcmp(tipo_op, "REGISTER") == 0) op_registro(sc);
    else if (strcmp(tipo_op, "UNREGISTER") == 0) op_baja(sc);
    else if (strcmp(tipo_op, "CONNECT") == 0) op_conexion(sc, ip_cliente);
    else if (strcmp(tipo_op, "DISCONNECT") == 0) op_desconexion(sc);
    else if (strcmp(tipo_op, "USERS") == 0) op_usuarios(sc);
    else if (strcmp(tipo_op, "SEND") == 0) op_envio(sc);
    else if (strcmp(tipo_op, "SENDATTACH") == 0) op_envio_attach(sc);

    /* cerramos el socket y el hilo se destruye */
    close(sc);
    pthread_exit(NULL);
}


/* MAIN */

int main(int argc, char *argv[])
{
    /* parsear argumento -p <puerto> */
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        printf("Uso: %s -p <puerto>\n", argv[0]);
        return -1;
    }
    int puerto = atoi(argv[2]);

    /* ignorar SIGPIPE para que write() no mate al proceso si el cliente cierra */
    signal(SIGPIPE, SIG_IGN);

    /* crear socket TCP */
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) { perror("socket"); return -1; }

    /* reutilizar puerto si el servidor se reinicia rápido */
    int opt = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons((unsigned short)puerto);

    if (bind(sd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind"); close(sd); return -1;
    }
    if (listen(sd, SOMAXCONN) < 0) {
        perror("listen"); close(sd); return -1;
    }

    /* obtener IP local para mostrarla al arrancar */
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct hostent *he = gethostbyname(hostname);
    char *ip_local;
    if (he && (*he).h_addr_list[0])
        ip_local = inet_ntoa(*(struct in_addr *)(*he).h_addr_list[0]); /* usar la IP obtenida */
    else
        ip_local = "0.0.0.0"; /* fallback si no se puede obtener la IP local */

    printf("s> init server %s:%d\n", ip_local, puerto);
    printf("s> \n");

    /* hilos desacoplados: se liberan solos al terminar */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* bucle principal: aceptar conexiones --> el server se queda aqui eternamente aceptando conexiones */
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        /* se bloquea hasta que un cliente hace _connect_to_server() */
        int sc = accept(sd, (struct sockaddr *)&cli_addr, &cli_len);
        if (sc < 0) { perror("accept"); continue; }

        /* preparar toda la informacion del cliente (argumentos del hilo) */
        struct ThreadArg *ta = malloc(sizeof(struct ThreadArg));
        if (!ta) { close(sc); continue; }
        (*ta).sc = sc;
        strncpy((*ta).ip_cliente, inet_ntoa(cli_addr.sin_addr), MAX_IP - 1);

        /* lanzamos hilo que ejecuta atender_cliente */
        pthread_t thid;
        if (pthread_create(&thid, &attr, atender_cliente, ta) != 0) {
            perror("pthread_create");
            free(ta);
            close(sc);
        } /* asi el bucle main vuelve arriba para aceptar al siguiente cliente */
    }

    pthread_attr_destroy(&attr);
    close(sd);
    return 0;
}
