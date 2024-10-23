import typer
import platform
import signal
import threading
import sys
from rich.console import Console
from rich.table import Table

from Modules import CmdInterface
from Modules.CmdInterface import is_valid_number
from Modules import Worker

if platform.system() == "Windows":
    DEFAULT_COMPORT = "COM1"
else:
    DEFAULT_COMPORT = "/dev/ttyACM0"


app = typer.Typer(
    name="FaultyCat",
    help="Script to control the FaultyCat and launch faulty attacks.",
    add_completion=False,
    no_args_is_help=True,
)

faulty_worker = Worker.FaultyWorker()
workers = []


def signal_handler(sig, frame):
    print("You pressed Ctrl+C!")
    faulty_worker.stop_workers()
    for work in workers:
        work.join()
    sys.exit(0)


@app.command("config")
def config():
    """Get the current configuration of the FaultyCat."""
    table_config = Table(title="Board configuration")
    table_config.add_column("Parameter", style="cyan")
    table_config.add_column("Value", style="magenta")
    table_config.add_row(
        "Pulse time", f"{faulty_worker.board_configurator.BOARD_CONFIG['pulse_time']}"
    )
    table_config.add_row(
        "Pulse power", f"{faulty_worker.board_configurator.BOARD_CONFIG['pulse_power']}"
    )

    Console().print(table_config)


@app.command("devices")
def devices():
    """Get the list of available devices."""
    table_devices = Table(title="Available devices")
    table_devices.add_column("Device", style="cyan")
    table_devices.add_column("Description", style="magenta")
    for device in faulty_worker.board_uart.get_serial_ports():
        table_devices.add_row(f"{device.device}", f"{device.description}")

    Console().print(table_devices)


@app.command("fault")
def faulty(
    comport: str = typer.Argument(
        default=DEFAULT_COMPORT,
        help="Serial port to use for uploading.",
    ),
    pulse_count: int = typer.Option(
        1, "--pulse-count", "-p", help="Number of pulses to send.", show_default=True
    ),
    pulse_timeout: float = typer.Option(
        1.0,
        "--pulse-timeout",
        "-t",
        help="Time in seconds between pulses.",
        show_default=True,
    ),
    cmd: bool = typer.Option(
        False, "--cmd", "-c", help="Launch the CMD Interface.", show_default=True
    ),
):
    """Setting up the FaultyCat. With this command you can configure the FaultyCat and launch faulty attacks."""
    typer.echo("Configuring the FaultyCat...")
    table_config = Table(title="Board configuration")
    table_config.add_column("Parameter", style="cyan")
    table_config.add_column("Value", style="magenta")
    table_config.add_row("Serial port", f"{comport}")
    table_config.add_row(
        "Pulse time", f"{faulty_worker.board_configurator.BOARD_CONFIG['pulse_time']}"
    )
    table_config.add_row(
        "Pulse power", f"{faulty_worker.board_configurator.BOARD_CONFIG['pulse_power']}"
    )
    table_config.add_row("Pulse count", f"{pulse_count}")
    table_config.add_row("Pulse timeout", f"{pulse_timeout}")

    Console().print(table_config)

    faulty_worker.set_serial_port(comport)
    faulty_worker.victim_board.set_serial_port("/dev/tty.usbmodem133401")
    faulty_worker.victim_board.set_serial_baudrate(115200)
    if cmd:
        victim_worker = threading.Thread(target=faulty_worker.start_monitor, daemon=True)
        victim_worker.start()
        CmdInterface.CMDInterface(faulty_worker).cmdloop()
        victim_worker.join()
        return

    if not faulty_worker.validate_serial_connection():
        typer.secho(
            f"FaultyCMD could not stablish connection withe the board on: {comport}.",
            fg=typer.colors.RED,
        )
        return

    faulty_worker.set_pulse_count(is_valid_number(pulse_count))
    faulty_worker.set_pulse_time(is_valid_number(pulse_timeout))

    faulty_worker.start_faulty_attack()
    


if __name__ == "__main__":
    print(
        """\x1b[36;1m
.@@@%@*%+ -@@+  #@@:  @@% =@@@@%- %@% %+            @=           | 
.@@-.-.#@+=@@*  %@@- .@@@.@@%:@@@ @@@ %+     :+++-  @*+++-       | FaultyCat v0.0.1
.@*.=.+@@:=@@*  %@@- .@@@.@@% @@@ @@@ %+    #%:.:## @%:.:##      | by JahazielLem
.@@%*+*=. :@@%==@@@#-*@@#.@@% @@@-@@@ %+    @+   =@.@+   =@.     | Company: PWNLabs - Electronics Cats
%@%       :#@@@%*#@@@%+  %@* :#@@@#: =%#**=.*####@..*####:       |                                                    
\x1b[0m"""
    )
    signal.signal(signal.SIGINT, signal_handler)
    app()
