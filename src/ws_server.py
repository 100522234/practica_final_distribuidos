import sys
import collections.abc
import http.cookies
import urllib.request
import urllib.parse

# añadido porque nos daba error para python 3.12
sys.modules['spyne.util.six.moves.collections_abc'] = collections.abc
sys.modules['spyne.util.six.moves.http_cookies'] = http.cookies
sys.modules['spyne.util.six.moves.urllib'] = sys.modules['urllib']
sys.modules['spyne.util.six.moves.urllib.request'] = urllib.request
sys.modules['spyne.util.six.moves.urllib.parse'] = urllib.parse


from spyne import Application, rpc, ServiceBase, Unicode
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from wsgiref.simple_server import make_server

# definimos el servicio web con spyne
class ConversorService(ServiceBase):
    
    # exponemos la funcion como un metodo RPC. recibe un Unicode (string) y devuelve un unicode
    @rpc(Unicode, _returns=Unicode)
    def NormalizarMensaje(ctx, mensaje):
        if not mensaje:
            return ""
        
        # elimina espacios en blanco repetidos
        mensaje_normalizado = " ".join(mensaje.split())
        return mensaje_normalizado

# configuramos la aplicacion indicando que servicios incluye y que protocolo usa
application = Application([ConversorService],
    tns='http://arcos.uc3m.es/conversor',   # espacio de nombres del servicio
    in_protocol=Soap11(validator='lxml'),   # protocolo de entrada
    out_protocol=Soap11()                   # protocolo de salida
)

if __name__ == '__main__':
    # creamos un servidor web estandar de python y le conectamos nuestra aplicacion spyne
    wsgi_app = WsgiApplication(application)
    puerto = 8000
    server = make_server('127.0.0.1', puerto, wsgi_app)
    
    print(f"Servidor SOAP (Conversor) iniciado en Python 3.12.")
    print(f"El WSDL está en: http://127.0.0.1:{puerto}/?wsdl")

    server.serve_forever() # se queda bloqueando el hilo y respondiendo peticiones web eternamente