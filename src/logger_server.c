#include "logger.h"
#include <stdio.h>

// funcion que se ejecuta automaticamente cuando el cliente RPC la invoca
int * imprimir_operacion_rpc_1_svc(char *nombre_usuario, char *operacion, struct svc_req *rqstp) {
    static int result = 0;  // variable estatica que se devuelve
    (void)rqstp; /* para evitar el warning de variable no usada */
    
    /* imprimimos el nombre de usuario y la operación tal como pide el enunciado */
    printf("%s\n", nombre_usuario);
    printf("%s\n", operacion);
    
    /* devolvemos un puntero al resultado (exigido por el estándar de rpcgen) */
    return &result;
}