"""Exercise the real native WebSocket client against an isolated loopback peer."""
import base64
import hashlib
import json
import socket
import struct
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

failures = []
count = 0
lock = threading.Lock()


def exact(stream, n):
    result = stream.read(n)
    if len(result) != n:
        raise EOFError("short frame")
    return result


def receive(stream):
    a, b = exact(stream, 2)
    assert a & 0x80 and b & 0x80
    n = b & 127
    if n == 126:
        n = struct.unpack('!H', exact(stream, 2))[0]
    if n == 127:
        n = struct.unpack('!Q', exact(stream, 8))[0]
    assert n <= 65536
    mask = exact(stream, 4)
    data = exact(stream, n)
    return a & 15, bytes(v ^ mask[i % 4] for i, v in enumerate(data))


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def frame(self, op, data, final=True):
        size = bytes([len(data)]) if len(data) < 126 else (b'\x7e' + struct.pack('!H', len(data)) if len(data) < 65536 else b'\x7f' + struct.pack('!Q', len(data)))
        self.wfile.write(bytes([op | (0x80 if final else 0)]) + size + data)
        self.wfile.flush()

    def do_GET(self):
        global count
        self.connection.settimeout(12)
        if self.path.startswith('/names?name='):
            name = self.path.removeprefix('/names?name=')
            assert self.headers.get('Cache-Control') == 'no-store'
            if name == 'Redirect':
                self.send_response(302); self.send_header('Location', '/unexpected')
                body = b''
            else:
                self.send_response(200)
                if name == 'Probe_VR':
                    assert self.headers.get('Authorization') == 'Bearer ' + 'a' * 43
                else:
                    assert self.headers.get('Authorization') is None
                body = b'L' * 8193 if name == 'TooLarge' else json.dumps({'name': name, 'state': 'free'}).encode()
            self.send_header('Content-Length', str(len(body))); self.send_header('Connection', 'close')
            self.end_headers(); self.wfile.write(body); self.close_connection = True
            return
        with lock:
            count += 1
            case = count
        try:
            assert self.path == ('/lan?queue&resume' if case == 5 else '/lan?queue')
            key = self.headers['Sec-WebSocket-Key']
            accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
            self.send_response(101)
            self.send_header('Upgrade', 'websocket')
            self.send_header('Connection', 'Upgrade')
            self.send_header('Sec-WebSocket-Accept', accept)
            self.end_headers()
            if case == 1:
                op, hello = receive(self.rfile)
                assert op == 1 and json.loads(hello) == {'t': 'hello', 'name': 'VR_Test', 'id': 'a' * 32, 'lan': 0}
                self.frame(1, b'L' * 200000)
                message = b'{"t":"welcome","vip":172818690,"token":"local-test"}'
                self.frame(1, message[:11], False)
                self.frame(0, message[11:])
                op, data = receive(self.rfile)
                assert op == 2 and data == b'\x00\x01\x02\xff\x80'
                self.frame(2, data)
                self.frame(8, struct.pack('!H', 4004))
                assert receive(self.rfile)[0] == 8
            elif case == 2:
                op, data = receive(self.rfile)
                assert op == 8  # Native close cancels its blocked receive.
                self.frame(8, data)
            elif case == 3:
                self.frame(2, bytes(40000), False)
                self.frame(0, bytes(30000))
                op, data = receive(self.rfile)
                assert op == 8
                self.frame(8, data)
            elif case in (4, 5):
                op, data = receive(self.rfile)
                hello = json.loads(data)
                assert op == 1 and hello['t'] == 'hello' and hello['name'] == 'VR_Worker'
                assert hello['id'] == 'a' * 32 and hello['lan'] == 0
                if case == 5:
                    assert hello['vip'] == '10.77.1.2' and hello['token'] == 'local-worker'
                else:
                    assert 'vip' not in hello and 'token' not in hello
                self.frame(1, b'{"t":"welcome","vip":172818690,"token":"local-worker"}')
                op, data = receive(self.rfile)
                assert op == 2 and len(data) == 19 and data[0] == 1 and data[16:] == bytes([7, 8, 9])
                assert struct.unpack('!II', data[4:12]) == (0x0a4d0102, 0x0a4d0103)
                assert struct.unpack('!HH', data[12:16]) == (47210, 47210)
                self.frame(2, data[:4] + data[8:12] + data[4:8] + data[12:])
                op, ack = receive(self.rfile)
                assert op == 2 and ack[16:] == bytes([6])
                if case == 4:
                    self.connection.shutdown(socket.SHUT_RDWR)  # abrupt loss, no close frame
                else:
                    self.frame(1, b'{"t":"kick","msg":"fixture end"}')
                    op, data = receive(self.rfile)
                    assert op == 8
                    self.frame(8, data)
            else:
                raise AssertionError('unexpected connection')
        except Exception as error:
            failures.append(f'case {case}: {type(error).__name__}: {error}')
        finally:
            self.close_connection = True


def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([sys.argv[1], str(server.server_port)], timeout=35)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    for failure in failures:
        print(failure, file=sys.stderr)
    return result.returncode or int(bool(failures) or count != 5)


if __name__ == '__main__':
    sys.exit(main())
