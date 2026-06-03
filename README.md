# Sistemas Distribuidos - Servicio de envío de mensajes

## Dependencias e instalación

### Python
```bash
pip3 install spyne lxml --break-system-packages
pip3 install zeep --break-system-packages
```

### Sistema (RPC y rpcbind)
```bash
sudo apt update
sudo apt install libtirpc-dev rpcbind
```

---

## Compilación

```bash
make clean
make
```

---

## Ejecución

Abrir **4 terminales** y ejecutar en el siguiente orden:

### Terminal 1 — Servidor RPC
```bash
./rpc_server
```

### Terminal 2 — Servicio Web (conversor de mensajes)
```bash
python3 src/ws_server.py
```

### Terminal 3 — Servidor de mensajería
```bash
export LOG_RPC_IP=127.0.0.1
./server -p 8888
```

### Terminal 4 — Cliente
```bash
python3 src/client.py -s localhost -p 8888
```

> Si se usan varios clientes en máquinas distintas, cada una debe tener su propio servicio web (Terminal 2) en ejecución.

---

## Notas

- El programa servidor termina al recibir la señal `SIGINT` (`Ctrl+C`).
- Si el servidor queda pausado accidentalmente (`Ctrl+Z` en lugar de `Ctrl+C`), liberar el puerto con:
  ```bash
  fuser -k 8888/tcp
  ```
