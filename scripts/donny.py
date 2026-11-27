#!/usr/bin/env python3
"""Interactive console for the `donny` linux-emulated application.

Runs inside the dev container only. It starts `donny.elf`, talks to its
line-based JSON console protocol, and lets you inspect and change the
simulated board model with tab-completed commands.

Usage:
    scripts/donny.py

The simulator prints each state response as `MODEL <json>` on stdout.

The console is a curses-based TUI with four panes:
  - Board (left, top, 25%): live view of the most recent simulator model.
  - Settings (middle, top, 25%): formatted JSON staged for `send`.
  - Console (right, top, 50%): text received from the simulator, with JSON
    sent to it echoed in reverse video.
  - Commands (bottom, 5 lines): prompt `] ` and command/status messages.

"""

from __future__ import annotations

import argparse
import curses
import json
import queue
import shlex
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import NoReturn


REPO_ROOT = Path(__file__).resolve().parents[1]
DONNY_ELF = REPO_ROOT / "components/simulator/exe/donny/build_linux/donny.elf"
DEV_CONTAINER_MARKER = Path("/etc/esp32-sensors-dev-container")
MODEL_PREFIX = "MODEL "
CONSOLE_HISTORY = 200


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

    power = subparsers.add_parser(
        "power", help="Stage simulated power state in prepared JSON")
    power.add_argument(
        "state",
        choices=("on", "off"),
        help="Turn the simulated run power on or off",
    )

    subparsers.add_parser("send", help="Send the prepared JSON to donny")

    switch = subparsers.add_parser(
        "switch", help="Stage a switch state in prepared JSON")
    switch.add_argument("name", choices=("config", "run"),
                        help="Which switch to change")
    switch.add_argument("state", choices=("on", "off"),
                        help="Desired switch state")

    no = subparsers.add_parser(
        "no", help="Remove a field from the prepared JSON")
    no_sub = no.add_subparsers(dest="no_target")
    no_sub.add_parser("power", help="Remove power from prepared JSON")
    no_switch = no_sub.add_parser(
        "switch", help="Remove a switch from prepared JSON")
    no_switch.add_argument("name", choices=("config", "run"),
                           help="Which switch to remove")

    subparsers.add_parser("show", help="Request the current model")
    subparsers.add_parser("help", help="Show command help")
    subparsers.add_parser("quit", help="Exit the console")
    subparsers.add_parser("exit", help="Exit the console")
    return parser


def render_model(payload: dict) -> str:
    """Format a model/state payload for display.

    Keys are ordered for readability: power first, then config/run switches,
    LEDs, sensors, and Wi-Fi, followed by any remaining fields.
    """
    ordered: dict[str, object] = {}
    preferred = (
        "power",
        "switch_config",
        "switch_run",
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
    """Driver for donny.elf with command state."""

    def __init__(self, elf: Path) -> None:
        self.elf = elf
        self.process: subprocess.Popen[str] | None = None
        self.parser = build_parser()
        self.model: dict | None = None
        self.pending: dict = {}
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.stop_event = threading.Event()
        self.last_message: str | None = None
        self.quiet = False
        self._closing = False
        self._lock = threading.Lock()
        self.auto_restart = False
        # (text, sent) pairs: sent lines are echoed in reverse video.
        self.console_lines: list[tuple[str, bool]] = []
        self._console_lock = threading.Lock()

    def _record_console(self, text: str, sent: bool = False) -> None:
        """Append one console line, trimming the oldest beyond the limit."""
        with self._console_lock:
            self.console_lines.append((text, sent))
            if len(self.console_lines) > CONSOLE_HISTORY:
                del self.console_lines[:-CONSOLE_HISTORY]

    def console_snapshot(self) -> tuple[tuple[str, bool], ...]:
        """Return a stable copy of the transcript for the TUI to render."""
        with self._console_lock:
            return tuple(self.console_lines)

    def start(self) -> None:
        """Start donny and background I/O readers."""
        self._spawn({})

    def _spawn(self, initial: dict) -> None:
        """Spawn donny process and attach readers."""
        process = subprocess.Popen(
            [str(self.elf)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        with self._lock:
            self.process = process
        assert process.stdout is not None
        assert process.stderr is not None
        threading.Thread(
            target=self._read_stdout,
            args=(process.stdout,),
            daemon=True,
        ).start()
        threading.Thread(
            target=self._read_stderr,
            args=(process.stderr,),
            daemon=True,
        ).start()
        self.send(initial)

    def _read_stdout(self, stream) -> None:
        """Consume protocol output from donny."""
        for raw in stream:
            line = raw.rstrip("\n")
            self._record_console(line)
            if not line.startswith(MODEL_PREFIX):
                continue
            payload_text = line[len(MODEL_PREFIX):]
            try:
                payload = json.loads(payload_text)
            except json.JSONDecodeError:
                message = f"invalid MODEL payload: {payload_text}"
                if not self.quiet:
                    print(message)
                self.events.put(("error", message))
                continue
            self.model = payload
            self._prompt_hint()
            if not self.quiet:
                print(f"MODEL {render_model(payload)}")
            self.events.put(("model", payload))
        if self.auto_restart and not self._closing:
            preserved = self._preserved_state()
            self.model = None
            self.events.put(("restarting", preserved))
            time.sleep(0.2)
            if not self._closing:
                try:
                    self._spawn(preserved)
                    return
                except Exception as exc:
                    self.events.put(
                        ("error", f"restart failed: {exc}")
                    )
        self.stop_event.set()
        self.events.put(("exit", None))

    def _read_stderr(self, stream) -> None:
        """Capture donny/ESP-IDF logs."""
        for raw in stream:
            line = raw.rstrip("\n")
            if line:
                self._record_console(line)
                self.events.put(("log", line))

    def send(self, document: dict) -> None:
        """Send one JSON document line to donny."""
        with self._lock:
            process = self.process
        if process is None or process.stdin is None:
            return
        if process.poll() is not None:
            return
        text = json.dumps(document, separators=(",", ":"))
        try:
            process.stdin.write(text)
            process.stdin.write("\n")
            process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError):
            return
        self._record_console(text, sent=True)

    def close(self) -> None:
        """Terminate donny and release resources."""
        self._closing = True
        with self._lock:
            process = self.process
        if process is None:
            return
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)

    def _preserved_state(self) -> dict:
        """Return model fields to restore on simulator restart."""
        model = self.model or {}
        preserved = {}
        for key in ("power", "switch_config", "switch_run"):
            if key in model:
                preserved[key] = model[key]
        return preserved

    def _prompt_hint(self) -> None:
        """Compatibility hook for tests/REPL integrations."""

    def completion_options(self, buffer: str, begidx: int) -> list[str]:
        """Return completion options for buffer position."""
        before = buffer[:begidx]
        parts = before.split()
        completing_first = not parts or (
            len(parts) == 1 and not before.endswith(" ")
        )
        if completing_first:
            return [
                "power", "send", "switch", "no", "show", "help", "quit",
                "exit",
            ]
        if parts[0] == "power":
            return ["on", "off"]
        if parts[0] == "switch":
            if len(parts) == 1 or (
                len(parts) == 2 and not before.endswith(" ")
            ):
                return ["config", "run"]
            return ["on", "off"]
        if parts[0] == "no":
            if len(parts) == 1 or (
                len(parts) == 2 and not before.endswith(" ")
            ):
                return ["power", "switch"]
            if parts[1] == "switch":
                return ["config", "run"]
            return []
        return []

    def completer(self, text: str, state: int) -> str | None:
        """Readline completer for commands and power states."""
        import readline

        buffer = readline.get_line_buffer()
        begidx = readline.get_begidx()
        options = self.completion_options(buffer, begidx)
        matches = [o for o in options if o.startswith(text)]
        if state < len(matches):
            return matches[state]
        return None

    def handle_command(self, line: str) -> bool:
        """Handle one command line. Return False to exit."""
        self.last_message: str | None = None
        try:
            args = self.parser.parse_args(shlex.split(line))
        except ValueError as exc:
            message = str(exc).strip()
            if message and message != "command exited":
                self.last_message = message
                if not self.quiet:
                    print(f"error: {message}")
            return True

        command = args.command
        if command in (None, "show"):
            self.send({})
            return True
        if command in ("quit", "exit"):
            return False
        if command == "help":
            self.last_message = (
                "commands: power send switch no show help quit")
            if not self.quiet:
                print(self.parser.format_help())
            return True
        if command == "power":
            self.pending["power"] = args.state
            return True
        if command == "send":
            self.send(self.pending.copy())
            self.pending.clear()
            self.last_message = "sent"
            return True
        if command == "switch":
            key = f"switch_{args.name}"
            self.pending[key] = args.state
            return True
        if command == "no":
            target = getattr(args, "no_target", None)
            if target == "power":
                self.pending.pop("power", None)
            elif target == "switch":
                key = f"switch_{args.name}"
                self.pending.pop(key, None)
            else:
                self.last_message = (
                    "usage: no power | no switch config | no switch run")
                return True
            return True

        self.last_message = f"unknown command: {command}"
        return True


class DonnyTUI:
    """Curses TUI for the donny console."""

    def __init__(self, stdscr, console: DonnyConsole) -> None:
        self.stdscr = stdscr
        self.console = console
        self.input_buf = ""
        self.status = "Type 'help' for commands. Ctrl-D or 'quit' to exit."
        self.running = True
        self.model_dirty = True
        self.settings_dirty = True
        self.console_dirty = True
        self.command_dirty = True
        self.all_dirty = True
        self._last_model = None
        self._last_pending = None
        self._last_console = None
        self._last_cmd = None
        self._cursor = None

    def _draw_pane(self, win, title: str, lines: list[str],
                   attributes: list[int] | None = None) -> None:
        """Draw a bordered pane with title and content lines."""
        win.erase()
        h, w = win.getmaxyx()
        if h < 2 or w < 2:
            win.noutrefresh()
            return
        win.border()
        title_text = f" {title} "
        if w > len(title_text) + 2:
            win.addnstr(0, 2, title_text, w - 4)
        max_lines = h - 2
        max_width = max(0, w - 2)
        for i, line in enumerate(lines[:max_lines]):
            if attributes is None:
                win.addnstr(1 + i, 1, line, max_width)
            else:
                win.addnstr(1 + i, 1, line, max_width, attributes[i])
        win.noutrefresh()

    def _layout(self):
        h, w = self.stdscr.getmaxyx()
        bottom_h = 5
        top_h = max(3, h - bottom_h)
        model_w = max(1, w // 4)
        settings_w = max(1, w // 4)
        console_w = max(1, w - model_w - settings_w)
        return (h, w, top_h, model_w, settings_w, console_w, bottom_h)

    def _draw(self, force: bool = False) -> None:
        """Render panes only when their content changes."""
        (h, w, top_h, model_w, settings_w, console_w,
         bottom_h) = self._layout()
        if self.all_dirty or force:
            self.stdscr.clear()
            self.stdscr.noutrefresh()
            self.model_dirty = True
            self.settings_dirty = True
            self.console_dirty = True
            self.command_dirty = True
            self.all_dirty = False
            self._cursor = None

        model_text = render_model(self.console.model) if self.console.model \
            is not None else "(no model yet)"
        pending_text = json.dumps(self.console.pending, indent=2)
        console_state = self.console.console_snapshot()

        if model_text != self._last_model:
            self.model_dirty = True
        if pending_text != self._last_pending:
            self.settings_dirty = True
        if console_state != self._last_console:
            self.console_dirty = True
        cmd_state = (self.input_buf, self.status)
        if cmd_state != self._last_cmd:
            self.command_dirty = True

        updated = False
        try:
            if self.model_dirty:
                model = curses.newwin(top_h, model_w, 0, 0)
                self._draw_pane(model, "Board", model_text.splitlines())
                self._last_model = model_text
                self.model_dirty = False
                updated = True
            if self.settings_dirty:
                settings = curses.newwin(top_h, settings_w, 0, model_w)
                self._draw_pane(
                    settings, "Settings", pending_text.splitlines())
                self._last_pending = pending_text
                self.settings_dirty = False
                updated = True
            if self.console_dirty:
                console = curses.newwin(
                    top_h, console_w, 0, model_w + settings_w)
                visible = console_state[-(top_h - 2):]
                self._draw_pane(
                    console, "Console",
                    [text for text, _ in visible],
                    [curses.A_REVERSE if sent else curses.A_NORMAL
                     for _, sent in visible],
                )
                self._last_console = console_state
                self.console_dirty = False
                updated = True
            if self.command_dirty:
                bottom = curses.newwin(bottom_h, w, top_h, 0)
                cmd_lines = [f"] {self.input_buf}"]
                if self.status:
                    cmd_lines.append(self.status)
                self._draw_pane(bottom, "Commands", cmd_lines)
                self._last_cmd = cmd_state
                self.command_dirty = False
                updated = True
        except curses.error:
            self.all_dirty = True
            return

        cursor_y = top_h + 1
        cursor_x = 1 + 2 + len(self.input_buf)
        cursor = (cursor_y, cursor_x)
        if cursor != self._cursor or updated:
            if 0 <= cursor_y < h and 0 <= cursor_x < w - 1:
                try:
                    curses.curs_set(1)
                    self.stdscr.move(cursor_y, cursor_x)
                except curses.error:
                    pass
            self._cursor = cursor
            curses.doupdate()

    def _drain_events(self) -> None:
        """Apply pending events from console threads."""
        while True:
            try:
                kind, payload = self.console.events.get_nowait()
            except queue.Empty:
                break
            if kind == "model":
                self.model_dirty = True
                if self.status == "simulator exited; restarting...":
                    self.status = ""
                    self.command_dirty = True
            elif kind == "restarting":
                self.status = "simulator exited; restarting..."
                self.command_dirty = True
                self.model_dirty = True
            elif kind == "error":
                self.status = str(payload)
                self.command_dirty = True
            elif kind == "exit":
                self.status = "donny exited."
                self.running = False
                self.command_dirty = True
            # "log" events need no flag: their text is in the transcript.

    def _handle_key(self, ch: int) -> None:
        """Handle one keypress."""
        if ch in (curses.KEY_ENTER, 10, 13):
            line = self.input_buf.strip()
            self.input_buf = ""
            self.command_dirty = True
            if not line:
                return
            keep_running = self.console.handle_command(line)
            message = self.console.last_message
            if message is not None:
                self.status = message
            elif keep_running:
                self.status = ""
            self.running = self.running and keep_running
            self.settings_dirty = True
            self.command_dirty = True
            return
        if ch in (curses.KEY_BACKSPACE, 127, 8):
            if self.input_buf:
                self.input_buf = self.input_buf[:-1]
                self.command_dirty = True
            return
        if ch in (4,):  # Ctrl-D
            self.running = False
            return
        if ch == ord("\t"):
            buf = self.input_buf
            text = buf.rsplit(" ", 1)[-1]
            begidx = len(buf) - len(text)
            options = self.console.completion_options(buf, begidx)
            matches = [o for o in options if o.startswith(text)]
            if len(matches) == 1:
                self.input_buf = buf[:begidx] + matches[0] + " "
                self.status = ""
            elif matches:
                common = matches[0]
                for m in matches[1:]:
                    i = 0
                    while i < len(common) and i < len(m) and common[i] == m[i]:
                        i += 1
                    common = common[:i]
                if len(common) > len(text):
                    self.input_buf = buf[:begidx] + common
                    self.status = ""
                else:
                    self.status = " ".join(matches)
            self.command_dirty = True
            return
        if ch == curses.KEY_RESIZE:
            self.all_dirty = True
            return
        if 0 <= ch <= 255:
            c = chr(ch)
            if c.isprintable():
                self.input_buf += c
                self.command_dirty = True

    def run(self) -> int:
        """Run the TUI event loop."""
        curses.curs_set(1)
        self.stdscr.nodelay(True)
        self.stdscr.timeout(100)
        while self.running and not self.console.stop_event.is_set():
            self._drain_events()
            self._draw()
            try:
                ch = self.stdscr.getch()
            except curses.error:
                ch = -1
            if ch != -1:
                self._handle_key(ch)
        self._drain_events()
        self._draw(force=True)
        self.stdscr.nodelay(False)
        self.stdscr.timeout(-1)
        return 0


def run_tui(stdscr, elf: Path) -> int:
    """Start console and run curses TUI."""
    console = DonnyConsole(elf)
    console.quiet = True
    console.auto_restart = True
    try:
        console.start()
        tui = DonnyTUI(stdscr, console)
        return tui.run()
    finally:
        console.close()


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

    return curses.wrapper(lambda stdscr: run_tui(stdscr, elf))


if __name__ == "__main__":
    raise SystemExit(main())
