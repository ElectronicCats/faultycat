import cmd
import typer
from rich.console import Console
from rich.table import Table


def is_valid_number(number):
    if number < 0:
        raise typer.BadParameter("Number must be positive.")
    return number

class CMDInterface(cmd.Cmd):
    intro = "Type help or ? to list commands.\n"
    prompt = "?> "
    file = None
    doc_header = "Commands"
    misc_header = "Misc Commands"
    undoc_header = "Undocumented Commands"

    def __init__(self, faulty_worker):
        super().__init__()
        self.faulty_worker = faulty_worker

    def do_config(self, args):
        """Configure the FaultyCat."""
        print("Configuring the FaultyCat...")
        table_config = Table(title="Board configuration")
        table_config.add_column("Parameter", style="cyan")
        table_config.add_column("Value", style="magenta")
        table_config.add_row(
            "Serial port", f"{self.faulty_worker.board_uart.serial_worker.port}"
        )
        table_config.add_row(
            "Pulse time",
            f"{self.faulty_worker.board_configurator.BOARD_CONFIG['pulse_time']}",
        )
        table_config.add_row(
            "Pulse power",
            f"{self.faulty_worker.board_configurator.BOARD_CONFIG['pulse_power']}",
        )
        table_config.add_row("Pulse count", f"{self.faulty_worker.pulse_count}")
        Console().print(table_config)

    def do_set(self, args):
        """Set a parameter."""
        print("Setting a parameter...")
        args_list = args.split()
        if args == "help" or args == "?":
            print("Available parameters:")
            print("\t[time] pulse_time")
            print("\t[count] pulse_count")
            print("\tport")
            return

        if len(args_list) != 2:
            print("Invalid number of arguments.")
            return

        if args_list[0] == "pulse_time" or args_list[0] == "time":
            self.faulty_worker.set_pulse_time(is_valid_number(float(args_list[1])))

        if args_list[0] == "pulse_count" or args_list[0] == "count":
            self.faulty_worker.set_pulse_count(is_valid_number(int(args_list[1])))

        if args_list[0] == "port":
            self.faulty_worker.set_serial_port(args_list[1])
            if not self.faulty_worker.validate_serial_connection():
                typer.secho("Invalid serial port.", fg=typer.colors.BRIGHT_RED)
                return

        self.do_config(args)

    def do_start(self, args):
        """Start the FaultyCat."""
        print("Starting the FaultyCat...")
        self.faulty_worker.start_faulty_attack()

    def do_exit(self, line):
        """Exit the CLI."""
        return True
