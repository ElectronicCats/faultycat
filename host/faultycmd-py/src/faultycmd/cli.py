"""click-based CLI for faultycmd, with Rich-rendered output.

Top-level command groups mirror the firmware's CDC layout:

    faultycmd emfi      (F4 emfi_proto on CDC0)
    faultycmd crowbar   (F5 crowbar_proto on CDC1)
    faultycmd campaign  (F9-4 campaign_proto on CDC0/CDC1, --engine)
    faultycmd scanner   (F8-2 ``scan swd`` over CDC2 text shell)
    faultycmd tui       (F10-5 Textual dashboard)
    faultycmd info      (USB enumeration helper)

Each subcommand wraps the corresponding ``faultycmd.protocols.*``
client. ``click.echo`` is used only for plain status lines; Rich
:class:`Console` handles tables, progress bars, and live displays.

JTAG and direct-SWD verbs (init/deinit/freq/idcode/connect/read32/
write32/reset + scan-jtag) are WIP and intentionally hidden from
this release; the underlying ``ScannerClient`` keeps them as
underscored methods so the v3.1 unblock can re-expose them
without re-writing the protocol layer.
"""

from __future__ import annotations

import click
from rich.console import Console
from rich.live import Live
from rich.table import Table

from . import __version__
from .protocols import (
    CampaignClient,
    CampaignError,
    CrowbarClient,
    EmfiClient,
    EngineError,
    ScannerClient,
    ScannerError,
)
from .protocols.crowbar import CrowbarOutput, CrowbarTrigger
from .protocols.emfi import EmfiTrigger
from .usb import discover

console = Console()


# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------


def _parse_axis(spec: str) -> tuple[int, int, int]:
    """Parse 'START:END:STEP' or just 'X' (collapses axis)."""
    if ":" not in spec:
        v = int(spec, 0)
        return v, v, 0
    parts = spec.split(":")
    if len(parts) != 3:
        raise click.BadParameter(f"axis must be START:END:STEP, got {spec!r}")
    return int(parts[0], 0), int(parts[1], 0), int(parts[2], 0)


def _engine_to_client(engine: str, port: str | None) -> CampaignClient:
    if port is not None:
        return CampaignClient(port, engine=engine)  # type: ignore[arg-type]
    return CampaignClient.discover(engine)  # type: ignore[arg-type]


def _print_status_table(title: str, rows: list[tuple[str, str]]) -> None:
    table = Table(title=title, show_header=False, box=None, pad_edge=False)
    table.add_column("field", style="cyan", no_wrap=True)
    table.add_column("value", style="white")
    for k, v in rows:
        table.add_row(k, v)
    console.print(table)


# -----------------------------------------------------------------------------
# Top-level group
# -----------------------------------------------------------------------------


@click.group()
@click.version_option(__version__, prog_name="faultycmd")
def main() -> None:
    """faultycmd — host tool for FaultyCat v3."""


# -----------------------------------------------------------------------------
# `info`
# -----------------------------------------------------------------------------


@main.command()
def info() -> None:
    """List FaultyCat interfaces detected on this machine."""
    ports = discover()
    if not ports:
        console.print(
            "[yellow]No FaultyCat CDC found.[/yellow] "
            "Check that the board is plugged in and re-enumerated as [b]1209:fa17[/b]."
        )
        raise SystemExit(1)
    table = Table(title="FaultyCat CDC interfaces")
    table.add_column("IF", style="cyan", justify="right")
    table.add_column("role", style="green")
    table.add_column("device", style="white")
    role_by_iface = {
        0x00: "emfi",
        0x02: "crowbar",
        0x04: "scanner",
        0x06: "target-uart",
    }
    for p in ports:
        table.add_row(
            f"0x{p.interface:02X}",
            role_by_iface.get(p.interface, "?"),
            p.device,
        )
    console.print(table)


# -----------------------------------------------------------------------------
# `emfi`
# -----------------------------------------------------------------------------


@main.group()
@click.option("--port", default=None, help="Override the EMFI port.")
@click.pass_context
def emfi(ctx: click.Context, port: str | None) -> None:
    """Control the EMFI module (electromagnetic fault injection)."""
    ctx.obj = port


def _emfi_client(ctx: click.Context) -> EmfiClient:
    port = ctx.obj
    return EmfiClient(port) if port is not None else EmfiClient.discover()


@emfi.command()
@click.pass_context
def ping(ctx: click.Context) -> None:
    """Verify communication with the EMFI module."""
    with _emfi_client(ctx) as cli:
        rpl = cli.ping()
    console.print(f"[green]PONG[/green] {rpl!r}")


@emfi.command()
@click.pass_context
def status(ctx: click.Context) -> None:
    """Show the current state of the EMFI module."""
    with _emfi_client(ctx) as cli:
        st = cli.status()
    _print_status_table(
        "EMFI status",
        [
            ("state", st.state.name if hasattr(st.state, "name") else str(st.state)),
            ("err", st.err.name if hasattr(st.err, "name") else str(st.err)),
            ("last_fire_at_ms", str(st.last_fire_at_ms)),
            ("capture_fill", str(st.capture_fill)),
            ("pulse_width_us_actual", str(st.pulse_width_us_actual)),
            ("delay_us_actual", str(st.delay_us_actual)),
        ],
    )


@emfi.command()
@click.option(
    "--trigger",
    type=click.Choice([t.name.lower() for t in EmfiTrigger]),
    default="immediate",
    show_default=True,
)
@click.option("--delay-us", type=int, default=0, show_default=True)
@click.option(
    "--width-us", type=int, default=5, show_default=True, help="Pulse width in µs (1..50)."
)
@click.option(
    "--charge-timeout-ms",
    type=int,
    default=0,
    show_default=True,
    help="Max armed time in ms (0 = 60 s default).",
)
@click.pass_context
def configure(
    ctx: click.Context,
    trigger: str,
    delay_us: int,
    width_us: int,
    charge_timeout_ms: int,
) -> None:
    """Configure the parameters for the next EMFI fire."""
    trig = EmfiTrigger[trigger.upper()]
    with _emfi_client(ctx) as cli:
        cli.configure(trig, delay_us, width_us, charge_timeout_ms)
    console.print(
        f"[green]configured[/green] trigger={trig.name} delay={delay_us}us width={width_us}us"
    )


@emfi.command()
@click.pass_context
def arm(ctx: click.Context) -> None:
    """Arm the module (charge the high-voltage capacitor)."""
    with _emfi_client(ctx) as cli:
        cli.arm()
    console.print("[green]armed[/green]")


@emfi.command()
@click.option(
    "--trigger-timeout-ms",
    type=int,
    default=60000,
    show_default=True,
    help="Max time to wait for the trigger in ms.",
)
@click.pass_context
def fire(ctx: click.Context, trigger_timeout_ms: int) -> None:
    """Wait for the trigger and fire the EMFI pulse."""
    with _emfi_client(ctx) as cli:
        cli.fire(trigger_timeout_ms)
    console.print("[green]fire dispatched[/green]")


@emfi.command()
@click.pass_context
def disarm(ctx: click.Context) -> None:
    """Disarm the EMFI module."""
    with _emfi_client(ctx) as cli:
        cli.disarm()
    console.print("[green]disarmed[/green]")


@emfi.command()
@click.option(
    "--offset", type=int, default=0, show_default=True, help="Start position within the buffer."
)
@click.option(
    "--length",
    type=int,
    default=512,
    show_default=True,
    help="Bytes to read (max 512 per request).",
)
@click.option(
    "--out",
    "out_file",
    type=click.Path(dir_okay=False, writable=True),
    default=None,
    help="Output file (if omitted, prints as hex).",
)
@click.pass_context
def capture(
    ctx: click.Context,
    offset: int,
    length: int,
    out_file: str | None,
) -> None:
    """Read the analog capture from the last fire."""
    with _emfi_client(ctx) as cli:
        data = cli.capture(offset=offset, length=length)
    if out_file:
        with open(out_file, "wb") as fh:
            fh.write(data)
        console.print(f"[green]wrote[/green] {len(data)} bytes → {out_file}")
    else:
        console.print(f"[cyan]{len(data)} bytes[/cyan]: {data.hex()}")


# -----------------------------------------------------------------------------
# `crowbar`
# -----------------------------------------------------------------------------


@main.group()
@click.option("--port", default=None, help="Override the crowbar port.")
@click.pass_context
def crowbar(ctx: click.Context, port: str | None) -> None:
    """Control the crowbar (voltage glitch on the target)."""
    ctx.obj = port


def _crowbar_client(ctx: click.Context) -> CrowbarClient:
    port = ctx.obj
    return CrowbarClient(port) if port is not None else CrowbarClient.discover()


@crowbar.command("ping")
@click.pass_context
def crowbar_ping(ctx: click.Context) -> None:
    """Verify communication with the crowbar."""
    with _crowbar_client(ctx) as cli:
        rpl = cli.ping()
    console.print(f"[green]PONG[/green] {rpl!r}")


@crowbar.command("status")
@click.pass_context
def crowbar_status(ctx: click.Context) -> None:
    """Show the current state of the crowbar."""
    with _crowbar_client(ctx) as cli:
        st = cli.status()
    _print_status_table(
        "Crowbar status",
        [
            ("state", st.state.name if hasattr(st.state, "name") else str(st.state)),
            ("err", st.err.name if hasattr(st.err, "name") else str(st.err)),
            ("last_fire_at_ms", str(st.last_fire_at_ms)),
            ("pulse_width_ns_actual", str(st.pulse_width_ns_actual)),
            ("delay_us_actual", str(st.delay_us_actual)),
            ("output", st.output.name if hasattr(st.output, "name") else str(st.output)),
        ],
    )


@crowbar.command("configure")
@click.option(
    "--trigger",
    type=click.Choice([t.name.lower() for t in CrowbarTrigger]),
    default="immediate",
    show_default=True,
)
@click.option(
    "--output",
    "output_str",
    type=click.Choice(["lp", "hp"]),
    default="hp",
    show_default=True,
    help="lp = low power, hp = high power (real glitch).",
)
@click.option(
    "--delay-us", type=int, default=0, show_default=True, help="Delay after the trigger in µs."
)
@click.option(
    "--width-ns", type=int, default=200, show_default=True, help="Pulse width in ns (8..50000)."
)
@click.pass_context
def crowbar_configure(
    ctx: click.Context,
    trigger: str,
    output_str: str,
    delay_us: int,
    width_ns: int,
) -> None:
    """Configure the parameters for the next glitch."""
    trig = CrowbarTrigger[trigger.upper()]
    out = CrowbarOutput[output_str.upper()]
    with _crowbar_client(ctx) as cli:
        cli.configure(trig, out, delay_us, width_ns)
    console.print(
        f"[green]configured[/green] trigger={trig.name} output={out.name} "
        f"delay={delay_us}us width={width_ns}ns"
    )


@crowbar.command("arm")
@click.pass_context
def crowbar_arm(ctx: click.Context) -> None:
    """Arm the crowbar."""
    with _crowbar_client(ctx) as cli:
        cli.arm()
    console.print("[green]armed[/green]")


@crowbar.command("fire")
@click.option(
    "--trigger-timeout-ms",
    type=int,
    default=60000,
    show_default=True,
    help="Max time to wait for the trigger in ms.",
)
@click.pass_context
def crowbar_fire(ctx: click.Context, trigger_timeout_ms: int) -> None:
    """Wait for the trigger and fire the glitch."""
    with _crowbar_client(ctx) as cli:
        cli.fire(trigger_timeout_ms)
    console.print("[green]fire dispatched[/green]")


@crowbar.command("disarm")
@click.pass_context
def crowbar_disarm(ctx: click.Context) -> None:
    """Disarm the crowbar."""
    with _crowbar_client(ctx) as cli:
        cli.disarm()
    console.print("[green]disarmed[/green]")


# -----------------------------------------------------------------------------
# `campaign`
# -----------------------------------------------------------------------------


@main.group()
@click.option(
    "--engine",
    type=click.Choice(["emfi", "crowbar"]),
    default="crowbar",
    show_default=True,
    help="Engine to run the sweep on.",
)
@click.option("--port", default=None, help="Override the port.")
@click.pass_context
def campaign(ctx: click.Context, engine: str, port: str | None) -> None:
    """Run automated parameter sweeps."""
    ctx.obj = (engine, port)


def _campaign_client(ctx: click.Context) -> CampaignClient:
    engine, port = ctx.obj
    return _engine_to_client(engine, port)


@campaign.command("status")
@click.pass_context
def campaign_status(ctx: click.Context) -> None:
    """Show the state of the running sweep."""
    with _campaign_client(ctx) as cli:
        st = cli.status()
    _print_status_table(
        f"Campaign status ({ctx.obj[0]})",
        [
            ("state", st.state.name if hasattr(st.state, "name") else str(st.state)),
            ("err", st.err.name if hasattr(st.err, "name") else str(st.err)),
            ("step_n", f"{st.step_n}/{st.total_steps}"),
            ("results_pushed", str(st.results_pushed)),
            ("results_dropped", str(st.results_dropped)),
        ],
    )


@campaign.command("configure")
@click.option(
    "--delay", required=True, help="Delay range in µs (START:END:STEP, or a fixed value)."
)
@click.option("--width", required=True, help="Pulse width range (µs on EMFI, ns on crowbar).")
@click.option(
    "--power", required=True, help="Power range. On crowbar: 1=low, 2=high. Ignored on EMFI."
)
@click.option(
    "--settle-ms", type=int, default=0, show_default=True, help="Settle time between shots in ms."
)
@click.pass_context
def campaign_configure(
    ctx: click.Context,
    delay: str,
    width: str,
    power: str,
    settle_ms: int,
) -> None:
    """Define the sweep ranges."""
    with _campaign_client(ctx) as cli:
        cli.configure(
            _parse_axis(delay),
            _parse_axis(width),
            _parse_axis(power),
            settle_ms=settle_ms,
        )
    console.print("[green]configured[/green]")


@campaign.command("start")
@click.pass_context
def campaign_start(ctx: click.Context) -> None:
    """Start the sweep."""
    with _campaign_client(ctx) as cli:
        cli.start()
    console.print("[green]started[/green]")


@campaign.command("stop")
@click.pass_context
def campaign_stop(ctx: click.Context) -> None:
    """Stop the running sweep."""
    with _campaign_client(ctx) as cli:
        cli.stop()
    console.print("[green]stopped[/green]")


@campaign.command("drain")
@click.option(
    "--max",
    "max_count",
    type=int,
    default=18,
    show_default=True,
    help="Records per request (max 18).",
)
@click.pass_context
def campaign_drain(ctx: click.Context, max_count: int) -> None:
    """Download the accumulated sweep results."""
    rows: list[tuple] = []
    with _campaign_client(ctx) as cli:
        for r in cli.drain_all():
            rows.append(
                (
                    r.step_n,
                    r.delay,
                    r.width,
                    r.power,
                    f"0x{r.fire_status:02X}",
                    f"0x{r.verify_status:02X}",
                    f"0x{r.target_state:08X}",
                    r.ts_us,
                )
            )
    if not rows:
        console.print("[yellow]ring empty[/yellow]")
        return
    table = Table(title=f"Campaign results ({len(rows)})")
    for col, just in [
        ("step", "right"),
        ("delay", "right"),
        ("width", "right"),
        ("power", "right"),
        ("fire", "right"),
        ("verify", "right"),
        ("target", "right"),
        ("ts_us", "right"),
    ]:
        table.add_column(col, justify=just)
    for row in rows:
        table.add_row(*[str(c) for c in row])
    console.print(table)


@campaign.command("watch")
@click.option("--every-ms", type=int, default=200, show_default=True, help="Update interval in ms.")
@click.pass_context
def campaign_watch(ctx: click.Context, every_ms: int) -> None:
    """Follow the sweep live until it completes."""
    engine = ctx.obj[0]
    seen_results: list = []

    def _render() -> Table:
        t = Table(title=f"Campaign live ({engine}) — {len(seen_results)} results so far")
        for col in ("step", "delay", "width", "power", "fire", "verify", "target"):
            t.add_column(col, justify="right")
        for r in seen_results[-15:]:
            t.add_row(
                str(r.step_n),
                str(r.delay),
                str(r.width),
                str(r.power),
                f"0x{r.fire_status:02X}",
                f"0x{r.verify_status:02X}",
                f"0x{r.target_state:08X}",
            )
        return t

    last_status = None
    with (
        _campaign_client(ctx) as cli,
        Live(_render(), refresh_per_second=8, console=console) as live,
    ):
        for st, batch in cli.watch(every_ms=every_ms):
            last_status = st
            seen_results.extend(batch)
            live.update(_render())
    if last_status is not None:
        st = last_status
        console.print(
            f"[bold]done[/bold] state={st.state.name if hasattr(st.state, 'name') else st.state} "
            f"step={st.step_n}/{st.total_steps} pushed={st.results_pushed} dropped={st.results_dropped}"
        )


# -----------------------------------------------------------------------------
# `scanner`
# -----------------------------------------------------------------------------


@main.group()
@click.option("--port", default=None, help="Override the scanner port.")
@click.pass_context
def scanner(ctx: click.Context, port: str | None) -> None:
    """Scan the target's SWD pinout."""
    ctx.obj = port


def _scanner_client(ctx: click.Context) -> ScannerClient:
    port = ctx.obj
    return ScannerClient(port) if port is not None else ScannerClient.discover()


@scanner.command("scan-swd")
@click.option(
    "--targetsel",
    default=None,
    help="Optional hex value (discovery sweeps the whole bus by default).",
)
@click.option(
    "--timeout-s", type=float, default=30.0, show_default=True, help="Max scan time in seconds."
)
@click.pass_context
def scanner_scan_swd(
    ctx: click.Context,
    targetsel: str | None,
    timeout_s: float,
) -> None:
    """Detect the target's SWD pins."""
    with _scanner_client(ctx) as cli:
        cli.scan_swd(
            targetsel_hex=targetsel,
            timeout_s=timeout_s,
            on_progress=console.print,
        )


# -----------------------------------------------------------------------------
# `tui`
# -----------------------------------------------------------------------------


@main.command()
def tui() -> None:
    """Launch the interactive dashboard."""
    try:
        from .tui import run as _run_tui
    except ImportError as e:
        raise click.ClickException(f"TUI is not available: {e}.") from e
    _run_tui()


# -----------------------------------------------------------------------------
# Error handling
# -----------------------------------------------------------------------------


def _wrap_main() -> None:
    """Top-level wrapper that converts our protocol exceptions into
    user-friendly click messages."""
    try:
        main()
    except (EngineError, CampaignError, ScannerError) as e:
        console.print(f"[red]error[/red] {e}")
        raise SystemExit(2) from e
    except FileNotFoundError as e:
        console.print(f"[red]not found[/red] {e}")
        raise SystemExit(2) from e


if __name__ == "__main__":
    _wrap_main()
