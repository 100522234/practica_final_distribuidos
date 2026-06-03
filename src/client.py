from enum import Enum
import argparse
import socket
import threading

class client :

    # ******************** TYPES *********************
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum) :
        OK = 0
        ERROR = 1
        USER_ERROR = 2


    # ****************** ATTRIBUTES ******************
    _server = None              # direccion IP del servidor al que nos conectamos
    _port = -1                  # puerto TCP del servidor al que nos conectamos
    _listen_sock    = None      # el socket por el que el cliente escucha mensajes entrantes
    _connected_user = None      # nombre del usuario actualmente conectado, lo guardamos para no tener que pedirlo al usuario en cada operacion
    _registered_user = None     # nombre del usuario registrado en esta sesion
    _peer_info = {}     # almacena la informacion 2P2 --> {"nombre_usuario" :: "ip" :: "puerto"}

    # ****************** HELPERS ******************
    # Funciones auxiliares para la comunicacion por sockets
    @staticmethod
    def _send_str(sock, s):
        """ envia una cadena terminada en '\\0' por el socket """
        sock.sendall((s + '\0').encode('utf-8'))

    @staticmethod
    def _recv_str(sock):
        """ lee bytes del socket hasta encontrar un '\\0' y devuelve la cadena """
        buf = b''
        while True:
            ch = sock.recv(1)
            if not ch or ch == b'\x00': # si el socket se cierrra o encontrams el delimitador nulo, terminamos de leer
                break
            buf += ch
        return buf.decode('utf-8')
    
    @staticmethod
    def _recv_byte(sock):
        """ lee solo un byte del socket y lo devuelve como entero (codigo de respuesta)"""
        data = sock.recv(1)
        if not data:
            raise ConnectionError("Servidor cerró la conexión inesperadamente")
        return data[0]

    @staticmethod
    def _connect_to_server():
        """ abre una conexion TCP al servidor y la devuelve, lanza excepcion si el servidor no esta disponible """
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((client._server, client._port))
        return s
    
    @staticmethod
    def _find_free_port():
        """ busca un puerto TCP libre para el socket de escucha del cliente y lo devuelve """
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))     # el puerto 0 le dice al SO que asigne uno aleatorio
            return s.getsockname()[1]
        
    @staticmethod
    def _listener_thread_func(listen_port):
        """ funcion que se ejecuta en el hilo de escucha de mensajes entrantes del cliente (el que creamos al hacer CONNECT)
            se queda escuchando en segundo plano para recibir mensajes de otros """
        
        # crear el socket de esccucha local
        srv_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv_sock.bind(('', listen_port))
        srv_sock.listen(5)  # socket en escucha

        client._listen_sock = srv_sock

        # bucle infinito del que solo saldremos cuando cierren el socket con DISCONNECT
        while True:
            try:
                conn, _ = srv_sock.accept() # espera pasivamente una conexion entrante
            except Exception:
                # el socket ha sido cerrado desde disconnect(), paramos el hilo
                break

            try:
                op = client._recv_str(conn) # lee la operacion enviada por quien se ha conectado

                if op == "SEND_MESSAGE":    # el servidor nos empuja un mensjae de otro usuario
                    remitente = client._recv_str(conn)
                    msg_id = client._recv_str(conn)
                    texto = client._recv_str(conn)
                    
                    # imprimimos por pantalla
                    print(f"\ns> MESSAGE {msg_id} FROM {remitente}")
                    print(texto)
                    print("END")
                    print("c> ", end='', flush=True)

                elif op == "SEND_MESS_ACK":  # el servidor nos empuja un mensjae de otro usuario
                    # server avisa de que el destinatario ha recibido el mensaje
                    msg_id = client._recv_str(conn)

                    print(f"\nc> SEND MESSAGE {msg_id} OK")
                    print("c> ", end='', flush=True)

                elif op == "SEND MESSAGE_ATTACH":  # el servidor nos empuja un mensjae de otro usuario
                    remitente = client._recv_str(conn)
                    msg_id = client._recv_str(conn)
                    texto = client._recv_str(conn)
                    fichero = client._recv_str(conn)
                    
                    print(f"\nc> MESSAGE {msg_id} FROM {remitente}")
                    print(texto)
                    print("END")
                    print(f"FILE {fichero}")
                    print("c> ", end='', flush=True)

                elif op == "SEND_MESS_ATTACH_ACK":  # el servidor nos empuja un mensjae de otro usuario
                    msg_id = client._recv_str(conn)
                    fichero = client._recv_str(conn)

                    print(f"\nc> SENDATTACH MESSAGE {msg_id} {fichero} OK")
                    print("c> ", end='', flush=True)

                elif op == "GET FILE":  # otro cliente (P2P) nos ha solicitado un archivo
                    remitente_peticion = client._recv_str(conn)
                    fichero_pedido = client._recv_str(conn)
                    
                    try:
                        # abrimos el fichero en local y lo volcamos por el socket
                        with open(fichero_pedido, "rb") as f:
                            while True:
                                chunk = f.read(4096)
                                if not chunk:
                                    break
                                conn.sendall(chunk) # volcamos el archivo por el socket
                    except Exception as e:
                        # si el fichero no existe, no mandamos nada y cerramos
                        pass

            except Exception:
                pass
            finally:
                conn.close()

    @staticmethod
    def _normalize_message(msg):
        """ Llama al Servicio Web SOAP para normalizar los espacios del mensaje """
        import zeep
        try:
            # le decimos a zeep donde esta el WSDL de nuestro ws_server.py
            wsdl_url = "http://127.0.0.1:8000/?wsdl"
            
            # creamos el cliente
            cliente = zeep.Client(wsdl=wsdl_url)
            
            # usamos el servicio (la misma estructura de siempre)(llamamos a la funcion NormalizarMensaje)
            resultado = cliente.service.NormalizarMensaje(msg)
            
            return resultado if resultado else msg
        
        except Exception as e:
            # si el servidor SOAP esta apagado, devolvemos el mensaje original para que no pete
            return msg


    # ******************** METHODS *******************

    # *
    # * @param user - User name to register in the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user is already registered
    # * @return ERROR if another error occurred
    @staticmethod
    def  register(user) :
        try:
            # conectar, enviar REGISTER, nombre y leer la respuesta del servidor
            s = client._connect_to_server()
            client._send_str(s, "REGISTER")
            client._send_str(s, user)

            # leer la respuesta del servidor
            cod = client._recv_byte(s)
            s.close()

        except Exception as e:
            print("c> REGISTER FAIL")
            print(f"Motivo real del fallo: {e}")
            return client.RC.ERROR

        # procesar el codigo de repuesta del servidor
        if cod == 0:
            client._registered_user = user  # guarda el estado
            print("c> REGISTER OK")
            return client.RC.OK
        
        elif cod == 1:
            print("c> USERNAME IN USE")
            return client.RC.USER_ERROR
        
        else:
            print("c> REGISTER FAIL")
            return client.RC.ERROR

    # *
    # 	 * @param user - User name to unregister from the system
    # 	 * 
    # 	 * @return OK if successful
    # 	 * @return USER_ERROR if the user does not exist
    # 	 * @return ERROR if another error occurred
    @staticmethod
    def  unregister(user) :
        # identica a register        
        try:
            s = client._connect_to_server()
            client._send_str(s, "UNREGISTER")  # esto cambia respecto a register
            client._send_str(s, user)
            cod = client._recv_byte(s)
            s.close()
        except Exception:
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR

        if cod == 0:
            client._registered_user = None  # esto tb cambia, aqui es none
            print("c> UNREGISTER OK")
            return client.RC.OK
        elif cod == 1:
            print("c> USER DOES NOT EXIST")
            return client.RC.USER_ERROR
        else:
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR

    # *
    # * @param user - User name to connect to the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def  connect(user) :
        # para evitar doble conexion en el mismo cliente 
        if client._connected_user is not None:
            print("c> USER ALREADY CONNECTED")
            return client.RC.USER_ERROR

        # buscamos un puerto libre y levantamos el hilo de escucha antes de enviar el comando CONNECT al servidor 
        # para asegurarnos de que el cliente ya esta escuchando cuando el servidor le envie mensajes entrantes
        listen_port = client._find_free_port()

        # creamos un hilo secundario que ejecuta el listener thread y le pasamos el puerto que acabamos de encontrar
        # daemon=True es que el hilo muere automaticamente si cerramos el programa principal
        t = threading.Thread(target=client._listener_thread_func, args=(listen_port,), daemon=True)
        t.start()   # arrancamos el hilo --> cliente escuchando en 2º plano

        try:
            s = client._connect_to_server() # abrimos socket TCP al servidor

            # enviamos la peticion: operacion --> usuario --> puerto de escucha
            client._send_str(s, "CONNECT")
            client._send_str(s, user)
            client._send_str(s, str(listen_port))   # informamos al server por donde escuchams

            # esperamos que el servidor responda con 1 byte para error o exitp
            cod = client._recv_byte(s)
            s.close()

        except Exception:
            # si falla matamos el socket que habiamos abierto para el hilo de escucha
            if client._listen_sock is not None:
                client._listen_sock.close()
                client._listen_sock = None
            print("c> CONNECT FAIL")
            return client.RC.ERROR

        if cod == 0:
            client._connected_user = user   # guardamos el usuario para no tener que escribirlo en cada op
            print("c> CONNECT OK")
            return client.RC.OK
        
        # gestion de codigos de error
        elif cod == 1:
            if client._listen_sock is not None:
                client._listen_sock.close()
                client._listen_sock = None
            print("c> CONNECT FAIL, USER DOES NOT EXIST")
            return client.RC.USER_ERROR
        
        elif cod == 2:
            if client._listen_sock is not None:
                client._listen_sock.close()
                client._listen_sock = None
            print("c> USER ALREADY CONNECTED")
            return client.RC.USER_ERROR
        
        else:
            if client._listen_sock is not None:
                client._listen_sock.close()
                client._listen_sock = None
            print("c> CONNECT FAIL")
            return client.RC.ERROR

    # *
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def  users(silent=False) :
        if client._connected_user is None:
            if not silent: print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            return client.RC.USER_ERROR

        try:
            s = client._connect_to_server()
            client._send_str(s, "USERS")
            client._send_str(s, client._connected_user)
            cod = client._recv_byte(s)

            if cod == 0:
                # leemos cuantos usuarios va a mandar el servidor
                n_str = client._recv_str(s)
                n = int(n_str)

                # vaciar la cache antigua
                client._peer_info.clear()
                nombres_limpios = []

                # leemos la lista y separamos usuario:ip:puerto
                for _ in range(n):
                    data = client._recv_str(s)
                    partes = data.split(":")
                    if len(partes) >= 3:
                        nombres_limpios.append(partes[0])
                        client._peer_info[partes[0]] = (partes[1], int(partes[2]))  # guardar en diccionacios para transferencias P2P
                    else:
                        nombres_limpios.append(data)
                s.close()
                
                # imprimir por pantalla
                if not silent:
                    print(f"c> CONNECTED USERS ({n} users connected) OK")
                    for nombre in nombres_limpios:
                        ip, puerto = client._peer_info[nombre]
                        print(f"{nombre} :: {ip} :: {puerto}") # ahora imprime bien lo del usuario con la ip y puerto --> (IP:puerto)
                return client.RC.OK
                
            else: # gestion de erroers
                s.close()
                if cod == 1:
                    if not silent: 
                        print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                    return client.RC.USER_ERROR
                else:
                    if not silent: 
                        print("c> CONNECTED USERS FAIL")
                    return client.RC.ERROR

        except Exception:
            if not silent:
                print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

    # *
    # * @param user - User name to disconnect from the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist
    # * @return ERROR if another error occurred
    @staticmethod
    def  disconnect(user) :
        try:
            # avisar al servidor para que marque como desconectado
            s = client._connect_to_server()
            client._send_str(s, "DISCONNECT")
            client._send_str(s, user)
            cod = client._recv_byte(s)
            s.close()

        except Exception:
            cod = 3 

        # paramos el hilo matando el socket de escucha directamente
        try:
            if client._listen_sock is not None:
                client._listen_sock.shutdown(socket.SHUT_RDWR)
                client._listen_sock.close()
                client._listen_sock = None

        except Exception:
            pass

        client._connected_user = None   # como en unregister pero con en disconnect

        if cod == 0:
            print("c> DISCONNECT OK")
            return client.RC.OK
        
        # gestion de errores
        elif cod == 1:
            print("c> DISCONNECT FAIL, USER DOES NOT EXIST")
            return client.RC.USER_ERROR
        
        elif cod == 2:
            print("c> DISCONNECT FAIL, USER NOT CONNECTED")
            return client.RC.USER_ERROR
        
        else:
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR

    # *
    # * @param user    - Receiver user name
    # * @param message - Message to be sent
    # * 
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def  send(user,  message) :
        # antes de enviar el mensaje llamamos a la funcion aux que utiliza zeep
        # se conecta al puerto --> le pasa el mensaje --> devuelve el mensaje sin espacios en blanco
        message = client._normalize_message(message)

        # validar tamaños maximos
        if len(message) > 255 or len(user) > 255:
            print("c> SEND FAIL")
            return client.RC.ERROR
        
        if client._connected_user is None:
            print("c> SEND FAIL")
            return client.RC.ERROR

        # enviar la peticion al servidor central
        try:
            s = client._connect_to_server()

            # operacion --> remitente --> destinatario --> mensaje
            client._send_str(s, "SEND")
            client._send_str(s, client._connected_user)
            client._send_str(s, user)
            client._send_str(s, message)

            cod = client._recv_byte(s)  # espera el cod de estado (0=ok, 1=user error, 2=error general)

            if cod == 0:
                # si todo va bien el servidor devuelve el ID del mensaje, leemos y lo imprimimos
                msg_id = client._recv_str(s)
                s.close()
                print(f"c> SEND OK MESSAGE {msg_id}")
                return client.RC.OK
            
            else: # codigos de error
                s.close()
                if cod == 1:
                    print("c> SEND FAIL, USER DOES NOT EXIST")
                    return client.RC.USER_ERROR
                else:
                    print("c> SEND FAIL")
                    return client.RC.ERROR

        except Exception:
            print("c> SEND FAIL")
            return client.RC.ERROR

    # *
    # * @param user    - Receiver user name
    # * @param file    - file  to be sent
    # * @param message - Message to be sent
    # * 
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def sendAttach(user, file, message):
        # Normalizar el mensaje con el Servicio Web
        message = client._normalize_message(message)
        
        # validar tamaños maximos (255 caracteres + 1 byte para \0 = 256)
        if len(message) > 255 or len(file) > 255 or len(user) > 255:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR
        
        if client._connected_user is None:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR

        try:
            s = client._connect_to_server()
            client._send_str(s, "SENDATTACH")
            client._send_str(s, client._connected_user)
            client._send_str(s, user)
            client._send_str(s, message)
            client._send_str(s, file)
            cod = client._recv_byte(s)

            if cod == 0:
                msg_id = client._recv_str(s)
                s.close()
                print(f"c> SENDATTACH OK MESSAGE {msg_id}")
                return client.RC.OK
            
            else:
                s.close()
                if cod == 1:
                    print("c> SENDATTACH FAIL, USER DOES NOT EXIST")
                    return client.RC.USER_ERROR
                else:
                    print("c> SENDATTACH FAIL")
                    return client.RC.ERROR

        except Exception:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR
        
    @staticmethod
    def getFile(user, remote_file, local_file):
        # comprobar si tenemos la info en cache, si no tenemos refrescar de forma silenciosa
        if user not in client._peer_info:
            client.users(silent=True)
            
        # si sigue sin existir, el usuario no esta conectado
        if user not in client._peer_info:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.ERROR

        # sacamos la ip y puerto del usuario
        ip, port = client._peer_info[user]
        
        try:
            # conexion P2P directa --> directa
            peer_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            peer_sock.connect((ip, port))
            
            # protocolo GET FILE --> usuario --> que fichero quiere
            client._send_str(peer_sock, "GET FILE")
            client._send_str(peer_sock, client._connected_user)
            client._send_str(peer_sock, remote_file)

            # descarga del archivo en bucle
            # abrimos fichero local en modo escritura binaria (wb)
            with open(local_file, "wb") as f:
                while True:
                    chunk = peer_sock.recv(4096) # leemos 4kb para no petar la memoria
                    if not chunk:
                        break # el otro lado cerró la conexion al terminar
                    f.write(chunk) # escribir en disco lo que recibo por el socket
                    
            peer_sock.close()
            # opcional: imprimir un mensaje de exito local
            print(f"c> GETFILE OK. Archivo guardado como {local_file}")
            return client.RC.OK
            
        except Exception as e:
            # si falla la conexion --> la IP era vieja --> limpiamos la cache
            client._peer_info.pop(user, None)
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.ERROR
    

    # *
    # **
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():

        while (True) :
            try :
                command = input("c> ")
                line = command.split(" ")
                if (len(line) > 0):

                    line[0] = line[0].upper()

                    if (line[0]=="REGISTER") :
                        if (len(line) == 2) :
                            client.register(line[1])
                        else :
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif(line[0]=="UNREGISTER") :
                        if (len(line) == 2) :
                            client.unregister(line[1])
                        else :
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif(line[0]=="CONNECT") :
                        if (len(line) == 2) :
                            client.connect(line[1])
                        else :
                            print("Syntax error. Usage: CONNECT <userName>")

                    elif(line[0]=="DISCONNECT") :
                        if (len(line) == 2) :
                            client.disconnect(line[1])
                        else :
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif(line[0]=="USERS") :
                        if (len(line) == 1) :
                            client.users()
                        else :
                            print("Syntax error. Usage: CONNECTED_USERS <userName>")

                    elif(line[0]=="SEND") :
                        if (len(line) >= 3) :
                            #  Remove first two words
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else :
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif(line[0]=="SENDATTACH") :
                        if (len(line) >= 4) :
                            userName = line[1]
                            fileName = line[-1] # El fichero es siempre el ult argumento
                            # el mensaje es todo lo que hay en medio (puede contener espacios)
                            message = ' '.join(line[2:-1])
                            client.sendAttach(userName, fileName, message)
                        else :
                            print("Syntax error. Usage: SENDATTACH <userName> <message> <fileName>")

                    elif(line[0]=="QUIT") :
                        if (len(line) == 1) :
                            break
                        else :
                            print("Syntax error. Use: QUIT")

                    elif(line[0]=="GETFILE") :
                        if (len(line) == 4) :
                            client.getFile(line[1], line[2], line[3])
                        else :
                            print("Syntax error. Usage: GETFILE <userName> <fileName> <localFileName>")

                    else :
                        print("Error: command " + line[0] + " not valid.")

            except Exception as e:
                print("Exception: " + str(e))

    # *
    # * @brief Prints program usage
    @staticmethod
    def usage() :
        print("Usage: python3 client.py -s <server> -p <port>")


    # *
    # * @brief Parses program execution arguments
    @staticmethod
    def  parseArguments(argv) :
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535");
            return False
        
        client._server = args.s
        client._port = args.p

        return True


    # ******************** MAIN *********************
    @staticmethod
    def main(argv) :
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        client.shell()
        print("+++ FINISHED +++")
    

if __name__=="__main__":
    client.main([])