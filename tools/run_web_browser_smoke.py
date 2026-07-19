#!/usr/bin/env python3
"""Run a persistent Chromium session until the exported Web oracle resolves."""

from __future__ import annotations

import argparse
import base64
import contextlib
import json
import os
import secrets
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Iterator
from urllib.parse import urlparse


def remove_directory_with_retries(
    path: Path, *, attempts: int = 12, initial_delay: float = 0.05
) -> None:
    """Remove a browser profile after Chromium's helper processes finish writing."""
    if attempts <= 0:
        raise ValueError("cleanup attempts must be positive")
    if initial_delay < 0:
        raise ValueError("cleanup delay must not be negative")

    delay = initial_delay
    for attempt in range(attempts):
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except OSError:
            if attempt + 1 == attempts:
                raise
            time.sleep(delay)
            delay = min(delay * 2.0, 0.5)


@contextlib.contextmanager
def browser_profile() -> Iterator[Path]:
    profile = Path(tempfile.mkdtemp(prefix="gdpp-chrome-"))
    try:
        yield profile
    finally:
        remove_directory_with_retries(profile)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chrome", default="google-chrome")
    parser.add_argument("--url", required=True)
    parser.add_argument("--dom-output", type=Path, required=True)
    parser.add_argument("--log-output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=150.0)
    return parser.parse_args()


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def read_exact(stream: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = stream.recv(size - len(result))
        if not chunk:
            raise ConnectionError("Chromium DevTools WebSocket closed unexpectedly")
        result.extend(chunk)
    return bytes(result)


class DevToolsSocket:
    """Small RFC 6455 client sufficient for Chromium's local DevTools endpoint."""

    def __init__(self, endpoint: str, timeout: float) -> None:
        parsed = urlparse(endpoint)
        if parsed.scheme != "ws" or not parsed.hostname:
            raise ValueError(f"unsupported DevTools endpoint: {endpoint}")
        port = parsed.port or 80
        self.socket = socket.create_connection((parsed.hostname, port), timeout=timeout)
        self.socket.settimeout(min(timeout, 2.0))
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        target = parsed.path or "/"
        if parsed.query:
            target += "?" + parsed.query
        request = (
            f"GET {target} HTTP/1.1\r\n"
            f"Host: {parsed.hostname}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.socket.sendall(request.encode("ascii"))
        response = bytearray()
        while b"\r\n\r\n" not in response:
            response.extend(read_exact(self.socket, 1))
            if len(response) > 16384:
                raise ConnectionError("oversized DevTools WebSocket handshake")
        status = bytes(response).split(b"\r\n", 1)[0]
        if b" 101 " not in status:
            raise ConnectionError(f"DevTools WebSocket handshake failed: {status!r}")

    def close(self) -> None:
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        self.socket.close()

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        mask = secrets.token_bytes(4)
        size = len(payload)
        header = bytearray([0x80 | opcode])
        if size < 126:
            header.append(0x80 | size)
        elif size <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", size))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", size))
        header.extend(mask)
        masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        self.socket.sendall(header + masked)

    def send_json(self, payload: dict[str, Any]) -> None:
        self._send_frame(0x1, json.dumps(payload, separators=(",", ":")).encode("utf-8"))

    def receive_json(self) -> dict[str, Any]:
        message = bytearray()
        message_opcode: int | None = None
        while True:
            first, second = read_exact(self.socket, 2)
            final = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            size = second & 0x7F
            if size == 126:
                size = struct.unpack("!H", read_exact(self.socket, 2))[0]
            elif size == 127:
                size = struct.unpack("!Q", read_exact(self.socket, 8))[0]
            mask = read_exact(self.socket, 4) if masked else b""
            payload = read_exact(self.socket, size)
            if masked:
                payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
            if opcode == 0x8:
                raise ConnectionError("Chromium closed the DevTools WebSocket")
            if opcode == 0x9:
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode in (0x1, 0x2):
                message_opcode = opcode
                message = bytearray(payload)
            elif opcode == 0x0 and message_opcode is not None:
                message.extend(payload)
            else:
                continue
            if final:
                if message_opcode != 0x1:
                    raise ConnectionError("unexpected binary DevTools message")
                return json.loads(message.decode("utf-8"))


class DevToolsSession:
    def __init__(self, endpoint: str, timeout: float) -> None:
        self.connection = DevToolsSocket(endpoint, timeout)
        self.next_id = 1
        self.events: list[str] = []

    def close(self) -> None:
        self.connection.close()

    def command(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        identifier = self.next_id
        self.next_id += 1
        request: dict[str, Any] = {"id": identifier, "method": method}
        if params is not None:
            request["params"] = params
        self.connection.send_json(request)
        while True:
            message = self.connection.receive_json()
            if message.get("id") == identifier:
                if "error" in message:
                    raise RuntimeError(f"DevTools {method} failed: {message['error']}")
                return message.get("result", {})
            self._record_event(message)

    def _record_event(self, message: dict[str, Any]) -> None:
        method = str(message.get("method", ""))
        params = message.get("params", {})
        if method == "Runtime.consoleAPICalled":
            values = []
            for argument in params.get("args", []):
                values.append(str(argument.get("value", argument.get("description", ""))))
            self.events.append(f"console.{params.get('type', 'log')}: {' '.join(values)}")
        elif method == "Runtime.exceptionThrown":
            details = params.get("exceptionDetails", {})
            self.events.append(f"exception: {details.get('text', details)}")
        elif method == "Log.entryAdded":
            entry = params.get("entry", {})
            self.events.append(f"{entry.get('level', 'log')}: {entry.get('text', '')}")


def devtools_page(port: int, url: str, deadline: float) -> str:
    endpoint = f"http://127.0.0.1:{port}/json/list"
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(endpoint, timeout=1.0) as response:
                targets = json.load(response)
            pages = [target for target in targets if target.get("type") == "page"]
            matching = [target for target in pages if target.get("url") == url]
            selected = matching[0] if matching else (pages[0] if pages else None)
            if selected and selected.get("webSocketDebuggerUrl"):
                return str(selected["webSocketDebuggerUrl"])
        except (OSError, urllib.error.URLError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    raise TimeoutError("Chromium did not expose a DevTools page")


def evaluate(session: DevToolsSession, expression: str) -> Any:
    result = session.command(
        "Runtime.evaluate",
        {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": True,
        },
    )
    if "exceptionDetails" in result:
        raise RuntimeError(f"browser evaluation failed: {result['exceptionDetails']}")
    remote = result.get("result", {})
    if remote.get("subtype") == "error":
        raise RuntimeError(str(remote.get("description", "browser evaluation failed")))
    return remote.get("value")


def run(args: argparse.Namespace) -> int:
    if args.timeout <= 0:
        raise ValueError("timeout must be positive")
    args.dom_output.parent.mkdir(parents=True, exist_ok=True)
    args.log_output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.timeout
    port = reserve_port()
    with browser_profile() as profile:
        chrome_log = args.log_output.with_suffix(args.log_output.suffix + ".chrome")
        with chrome_log.open("wb") as stderr:
            command = [
                args.chrome,
                "--headless=new",
                "--no-sandbox",
                "--disable-dev-shm-usage",
                "--disable-background-networking",
                "--use-gl=angle",
                "--use-angle=swiftshader-webgl",
                "--enable-webgl",
                "--enable-unsafe-swiftshader",
                "--autoplay-policy=no-user-gesture-required",
                "--remote-allow-origins=*",
                f"--remote-debugging-port={port}",
                f"--user-data-dir={profile}",
                args.url,
            ]
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=stderr)
            session: DevToolsSession | None = None
            failure = ""
            try:
                endpoint = devtools_page(port, args.url, deadline)
                session = DevToolsSession(endpoint, max(1.0, deadline - time.monotonic()))
                session.command("Runtime.enable")
                session.command("Log.enable")
                status = ""
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"Chromium exited early with status {process.returncode}")
                    status = str(
                        evaluate(
                            session,
                            "document.documentElement ? "
                            "(document.documentElement.dataset.gdppStatus || '') : ''",
                        )
                        or ""
                    )
                    if status in ("ok", "invalid"):
                        break
                    time.sleep(0.25)
                html = str(
                    evaluate(
                        session,
                        "document.documentElement ? document.documentElement.outerHTML : ''",
                    )
                    or ""
                )
                args.dom_output.write_text(html + "\n", encoding="utf-8")
                if status != "ok":
                    failure = (
                        "Web runtime reported an invalid AOT behavior oracle"
                        if status == "invalid"
                        else "Web runtime did not publish its successful DOM oracle before timeout"
                    )
            except Exception as error:  # Preserve browser evidence before returning a CI failure.
                failure = str(error)
            finally:
                if session is not None:
                    try:
                        session.command("Browser.close")
                    except (ConnectionError, OSError, RuntimeError):
                        pass
                    session.close()
                if process.poll() is None:
                    try:
                        process.wait(timeout=5.0)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        try:
                            process.wait(timeout=5.0)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
        chrome_output = chrome_log.read_text(encoding="utf-8", errors="replace")
        chrome_log.unlink(missing_ok=True)
        events = "\n".join(session.events if session is not None else [])
        args.log_output.write_text(
            chrome_output + ("\n" if chrome_output and events else "") + events + "\n",
            encoding="utf-8",
        )
        if failure:
            print(failure)
            return 1
    print("GDPP_WEB_BROWSER_ORACLE=PASS")
    return 0


def main() -> int:
    try:
        return run(parse_args())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Web browser smoke test failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
