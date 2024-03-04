import platform
import serial
import time
import serial.tools.list_ports
import threading

from  .ConfigBoard import BoardStatus

if platform.system() == "Windows":
    DEFAULT_COMPORT = "COM1"
else:
    DEFAULT_COMPORT = "/dev/ttyACM0"

DEFAULT_SERIAL_BAUDRATE = 921600

class UART(threading.Thread):
    def __init__(self, serial_port: str = DEFAULT_COMPORT):
        self.serial_worker          = serial.Serial()
        self.serial_worker.port     = serial_port
        self.serial_worker.baudrate = DEFAULT_SERIAL_BAUDRATE
        self.recv_cancel            = False
        #self.daemon                 = True

    def __del__(self):
        self.serial_worker.close()

    def __str__(self):
        return f"Serial port: {self.serial_worker.port}"

    def set_serial_port(self, serial_port: str):
        self.serial_worker.port = serial_port
    
    def set_serial_baudrate(self, serial_baudrate: int):
        self.serial_worker.baudrate = serial_baudrate

    def is_valid_connection(self) -> bool:
        try:
            self.open()
            self.close()
            return True
        except serial.SerialException as e:
            return False

    def get_serial_ports(self):
        return serial.tools.list_ports.comports()

    def reset_buffer(self):
        self.serial_worker.reset_input_buffer()
        self.serial_worker.reset_output_buffer()

    def cancel_recv(self):
        self.recv_cancel = True

    def open(self):
        self.serial_worker.open()
        self.reset_buffer()
    
    def close(self):
        self.reset_buffer()
        self.serial_worker.close()

    def is_connected(self):
        return self.serial_worker.is_open

    def send(self, data):
        self.serial_worker.write(data)
        self.serial_worker.write(b"\n\r")
        self.serial_worker.flush()
    
    def recv(self):
        if not self.is_connected():
            self.open()
        try:
            while not self.recv_cancel:
                time.sleep(0.1)
                
                bytestream = self.serial_worker.readline()
                if self.recv_cancel:
                    self.recv_cancel = False
                    return None
                return bytestream
        except serial.SerialException as e:
            print(e)
            return None
        except KeyboardInterrupt:
            self.recv_cancel = True
            return None
    
    def send_recv(self, data):
        self.send(data)
        return self.recv()

    def stop_worker(self):
        self.recv_cancel = True
        self.reset_buffer()
        self.close()
        self.join()