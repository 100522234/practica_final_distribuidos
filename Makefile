# Variables del compilador
CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include -I./src -I/usr/include/tirpc
LDFLAGS = -pthread -ltirpc

# Directorios de codigo fuente
SRCDIR = src
INCDIR = include

# Archivos que generará rpcgen automáticamente cuando lee logger.x
RPC_GEN = $(SRCDIR)/logger_clnt.c $(SRCDIR)/logger_svc.c $(SRCDIR)/logger_xdr.c $(SRCDIR)/logger.h

# archivos para el servidor principal
SERVER_SRCS = $(SRCDIR)/server.c $(SRCDIR)/lines.c $(SRCDIR)/logger_clnt.c $(SRCDIR)/logger_xdr.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_TARGET = server

# archivos para el servidor RPC
RPC_SRCS = $(SRCDIR)/logger_server.c $(SRCDIR)/logger_svc.c $(SRCDIR)/logger_xdr.c
RPC_OBJS = $(RPC_SRCS:.c=.o)
RPC_TARGET = rpc_server

# Regla por defecto que compila ambos servidores si escribes make
all: $(SERVER_TARGET) $(RPC_TARGET)

# Regla explícita: si no existen estos archivos, ejecuta rpcgen para crearlos
$(RPC_GEN): $(SRCDIR)/logger.x
	cd $(SRCDIR) && rpcgen -N -C logger.x

# Aseguramos que antes de compilar los .o, existan los archivos generados
$(SERVER_OBJS) $(RPC_OBJS): $(RPC_GEN)

# Enlazado del servidor principal C
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJS) $(LDFLAGS)

# Enlazado del servidor RPC
$(RPC_TARGET): $(RPC_OBJS)
	$(CC) $(CFLAGS) -o $(RPC_TARGET) $(RPC_OBJS) $(LDFLAGS)

# Silenciar warnings del código autogenerado por rpcgen
$(SRCDIR)/logger_svc.o: CFLAGS += -Wno-unused-parameter -Wno-cast-function-type
$(SRCDIR)/logger_clnt.o: CFLAGS += -Wno-unused-parameter -Wno-cast-function-type
$(SRCDIR)/logger_xdr.o: CFLAGS += -Wno-unused-parameter -Wno-cast-function-type

# Regla genérica para compilar los objetos .o a partir de los .c
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpieza: borra ejecutables, objetos y archivos generados por rpcgen
clean:
	rm -f $(SERVER_TARGET) $(RPC_TARGET) $(SRCDIR)/*.o
	rm -f $(RPC_GEN)

.PHONY: all clean