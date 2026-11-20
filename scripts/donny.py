#!/usr/bin/env python3
"""Interactive console for the `donny` linux-emulated application.

Runs inside the dev container only. It starts `donny.elf`, talks to its
line-based JSON console protocol, and lets you inspect and change the
simulated board model with tab-completed commands.

Usage:
    scripts/donny.py

Initial commands:
    power on    Set the run switch on and the config switch off.
    power off   Set the run and config switches off.
    show        Request and display the current model.
    help        Show command help.
    quit        Exit the console.

The simulator prints each state response as `MODEL <json>` on stdout.
"""

from __future__ import annotations

import argparse
import json
import os
import readline
import shlex
import subprocess
import sys
import threading
from pathlib import Path
from typing import NoReturn


REPO_ROOT = Path(__file__).resolve().parents[1]
DONNY_ELF = REPO_ROOT / "components/simulator/exe/donny/build_linux/donny.elf"
DEV_CONTAINER_MARKER = Path("/etc/esp32-sensors-dev-container")
MODEL_PREFIX = "MODEL "


def in_container() -> bool:
    """Return True when running inside the dev container."""
    return DEV_CONTAINER_MARKER.exists()


def die(message: str, code: int = 1) -> NoReturn:
    """Print an error and exit."""
    print(f"donny.py: {message}", file=sys.stderr)
    raise SystemExit(code)


class CommandParser(argparse.ArgumentParser):
    """ArgumentParser that raises instead of exiting the process."""

    def exit(self, status: int = 0, message: str | None = None) -> NoReturn:
        if message:
            raise ValueError(message.rstrip())
        raise ValueError("command exited")

    def error(self, message: str) -> NoReturn:
        raise ValueError(message)


def build_parser() -> CommandParser:
    """Build the interactive command parser."""
    parser = CommandParser(prog="", add_help=True)
    subparsers = parser.add_subparsers(dest="command")

    power = subparsers.add_parser("power", help="Change simulated power state")
    power.add_argument(
        "state",
        choices=("on", "off"),
        help="Turn the simulated run power on or off",
    )

    subparsers.add_parser("show", help="Request the current model")
    subparsers.add_parser("help", help="Show command help")
    subparsers.add_parser("quit", help="Exit the console")
    subparsers.add_parser("exit", help="Exit the console")
    return parser


def render_model(payload: dict) -> str:
    """Format a model/state payload for display.

    Keys are ordered for readability: config/run switches first, then
    LEDs, sensors, and Wi-Fi, followed by any remaining fields.
    """
    ordered: dict[str, object] = {}
    preferred = (
        "config_switch",
        "run_switch",
        "leds",
        "sensors",
        "wifi",
    )
    for key in preferred:
        if key in payload:
            ordered[key] = payload[key]
    for key in sorted(payload):
        if key not in ordered:
            ordered[key] = payload[key]
    return json.dumps(ordered, indent=2)


class DonnyConsole:
    """Interactive driver for donny.elf."""

    def __init__(self, elf: Path) -> None:
        self.elf = elf
        self.process: subprocess.Popen[str] | None = None
        self.parser = build_parser()
        self.model: dict | None = None
        self.model_lock = threading.Lock()
        self.stop_event = threading.Event()

    def start(self) -> None:
        """Start donny and background I/O readers."""
        self.process = subprocess.Popen(
            [str(self.elf)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        assert self.process.stdout is not None
        assert self.process.stderr is not None
        threading.Thread(
            target=self._read_stdout,
            args=(self.process.stdout,),
            daemon=True,
        ).start()
        threading.Thread(
            target=self._read_stderr,
            args=(self.process.stderr,),
            daemon=True,
        ).start()
        # Request the initial snapshot without changing state.
        self.send({})

    def _read_stdout(self, stream) -> None:
        """Consume protocol output from donny."""
        for raw in stream:
            line = raw.rstrip("\n")
            if not line.startswith(MODEL_PREFIX):
                continue
            payload_text = line[len(MODEL_PREFIX):]
            try:
                payload = json.loads(payload_text)
            except json.JSONDecodeError:
                print(f"\n[invalid MODEL payload] {payload_text}")
                self._prompt_hint()
                continue
            with self.model_lock:
                self.model = payload
            print("\nMODEL")
            print(render_model(payload))
            self._prompt_hint()
        self.stop_event.set()

    def _read_stderr(self, stream) -> None:
        """Forward donny/ESP-IDF logs to stderr."""
        for raw in stream:
            sys.stderr.write(raw)
            sys.stderr.flush()

    @staticmethod
    def _prompt_hint() -> None:
        """Repaint a prompt hint after asynchronous output."""
        try:
            buf = readline.get_line_buffer()
        except Exception:
            buf = ""
        if buf:
            print(f"donny> {buf}", end="", flush=True)
        else:
            print("donny> ", end="", flush=True)

    def send(self, document: dict) -> None:
        """Send one JSON document line to donny."""
        if self.process is None or self.process.stdin is None:
            return
        if self.process.poll() is not None:
            return
        self.process.stdin.write(json.dumps(document, separators=(",", ":")))
        self.process.stdin.write("\n")
        self.process.stdin.flush()

    def close(self) -> None:
        """Terminate donny and release resources."""
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)

    def handle_command(self, line: str) -> bool:
        """Handle one command line. Return False to exit."""
        try:
            args = self.parser.parse_args(shlex.split(line))
        except ValueError as exc:
            message = str(exc).strip()
            if message and message != "command exited":
                print(message)
            return True

        command = args.command
        if command in (None, "show"):
            self.send({})
            return True
        if command in ("quit", "exit"):
            return False
        if command == "help":
            self.parser.print_help()
            return True
        if command == "power":
            if args.state == "on":
                self.send({"run_switch": "on", "config_switch": "off"})
            else:
                self.send({"run_switch": "off", "config_switch": "off"})
            return True

        print(f"unknown command: {command}")
        return True

    def completer(self, text: str, state: int):
        """Readline tab completer for interactive commands."""
        buffer = readline.get_line_buffer()
        begin = readline.get_begidx()
        before = buffer[:begin]
        tokens = before.split()

        if not tokens:
            options = ["power", "show", "help", "quit"]
        elif len(tokens) == 1 and before.endswith(" "):
            if tokens[0] == "power":
                options = ["on", "off"]
            else:
                options = []
        elif len(tokens) == 1:
            options = ["power", "show", "help", "quit"]
        elif (tokens[0] == "power"
              and len(tokens) == 2
              and before.endswith(" ")):
            options = []
        elif tokens[0] == "power" and len(tokens) == 2:
            options = ["on", "off"]
        else:
            options = []

        matches = [opt for opt in options if opt.startswith(text)]
        if state < len(matches):
            return matches[state]
        return None

    def repl(self) -> int:
        """Run the interactive command loop."""
        readline.set_completer(self.completer)
        readline.parse_and_bind("tab: complete")
        readline.set_completer_delims(" \t\n")

        print("donny interactive console")
        print("Type 'help' for commands. Ctrl-D or 'quit' to exit.")
        while not self.stop_event.is_set():
            try:
                line = input("donny> ")
            except EOFError:
                print()
                return 0
            except KeyboardInterrupt:
                print()
                continue
            line = line.strip()
            if not line:
                continue
            if not self.handle_command(line):
                return 0
        print("donny exited.")
        return 0


def main(argv: list[str] | None = None) -> int:
    """Program entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--elf",
        type=Path,
        default=DONNY_ELF,
        help="Path to donny.elf (default: %(default)s)",
    )
    args = parser.parse_args(argv)

    if not in_container():
        die("must be run inside the dev container")

    elf = args.elf
    if not elf.is_absolute():
        elf = (Path.cwd() / elf).resolve()
    if not elf.is_file():
        die(
            f"donny executable not found: {elf}\n"
            "Build it with:\n"
            "  ./scripts/dev.py build components/simulator/exe/donny"
        )

    console = DonnyConsole(elf)
    try:
        console.start()
        return console.repl()
    finally:
        console.close()


if __name__ == "__main__":
    raise SystemExit(main())
