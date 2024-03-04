import threading
import time
import typer
from rich.console import Console
from rich.table import Table    

from .UART import UART
from .ConfigBoard import ConfigBoard

class FaultyWorker(threading.Thread):
    def __init__(self):
        super().__init__()
        #self.daemon = True
        self.workers = []
        self.board_uart = UART()
        self.board_configurator = ConfigBoard()
        self.pulse_count = self.board_configurator.BOARD_CONFIG["pulse_count"]   
        self.pulse_time = self.board_configurator.BOARD_CONFIG["pulse_time"]
    
    def add_worker(self, worker):
        self.workers.append(worker)
    
    def stop_workers(self):
        for worker in self.workers:
            worker.join()
    
    def run_workers(self):
        for worker in self.workers:
            worker.start()
    
    def set_serial_port(self, serial_port):
        self.board_uart.set_serial_port(serial_port)
    
    def validate_serial_connection(self):
        return self.board_uart.is_valid_connection()
    
    def set_pulse_count(self, pulse_count):
        self.pulse_count = pulse_count
        self.board_configurator.BOARD_CONFIG["pulse_count"] = pulse_count
    
    def set_pulse_time(self, pulse_time):
        self.pulse_time = pulse_time
        self.board_configurator.BOARD_CONFIG["pulse_time"] = pulse_time
    
    def start_faulty_attack(self):
        try:
            self.board_uart.open()
            time.sleep(0.1)
            typer.secho("Board connected.", fg=typer.colors.GREEN)
            typer.secho("[*] ARMING BOARD, BE CAREFULL!", fg=typer.colors.BRIGHT_YELLOW)
            self.board_uart.send(self.board_configurator.board_commands.COMMAND_DISARM.value.encode("utf-8"))
            time.sleep(1)
            self.board_uart.send(self.board_configurator.board_commands.COMMAND_ARM.value.encode("utf-8"))
            
            typer.secho("[*] ARMED BOARD.", fg=typer.colors.BRIGHT_GREEN)
            time.sleep(1)
            typer.secho(f"[*] SENDING {self.pulse_count} PULSES.", fg=typer.colors.BRIGHT_GREEN)
            for i in range(self.pulse_count):
                typer.secho(f"\t- SENDING PULSE {i+1} OF {self.pulse_count}.", fg=typer.colors.BRIGHT_GREEN)
                self.board_uart.send(self.board_configurator.board_commands.COMMAND_PULSE.value.encode("utf-8"))
                time.sleep(self.pulse_time)
            
            typer.secho("DISARMING BOARD.", fg=typer.colors.BRIGHT_YELLOW)
            self.board_uart.send(self.board_configurator.board_commands.COMMAND_DISARM.value.encode("utf-8"))
            self.board_uart.close()
            typer.secho("BOARD DISARMING.", fg=typer.colors.BRIGHT_YELLOW)
        except Exception as e:
            typer.secho(f"Error: {e}", fg=typer.colors.BRIGHT_RED)