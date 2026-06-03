/* definicion del programa RPC --> LOG_PROG es el nombre que le damos al sistema */
program LOG_PROG {
    /* definimos la version del programa */
    version LOG_VERS {
        /* definimos la firma de la funcion remota, recibe 2 str y devuelve un int */
        int imprimir_operacion_rpc(string nombre_usuario<256>, string operacion<256>) = 1;
    } = 1;
} = 0x20000001;