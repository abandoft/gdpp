#!/usr/bin/env python3
"""Exercise the release downloader against a truncated first response."""

from __future__ import annotations

import http.server
import subprocess
import tempfile
import threading
from pathlib import Path


PAYLOAD = b"gdpp-release-download\n" * 256


class RetryHandler(http.server.BaseHTTPRequestHandler):
    attempts = 0

    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        type(self).attempts += 1
        self.send_response(200)
        self.send_header("Content-Length", str(len(PAYLOAD)))
        self.end_headers()
        if type(self).attempts == 1:
            self.wfile.write(PAYLOAD[:64])
            self.wfile.flush()
            self.connection.shutdown(1)
            return
        self.wfile.write(PAYLOAD)

    def log_message(self, _format: str, *_args: object) -> None:
        return


def main() -> int:
    source_root = Path(__file__).resolve().parents[1]
    downloader = source_root / "tools" / "download_with_retry.sh"
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RetryHandler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix="gdpp-download-test-") as temporary:
            destination = Path(temporary) / "nested" / "artifact.zip"
            url = f"http://127.0.0.1:{server.server_port}/artifact.zip"
            subprocess.run(
                ["bash", str(downloader), url, str(destination)],
                check=True,
                cwd=source_root,
            )
            if destination.read_bytes() != PAYLOAD:
                raise RuntimeError("retried download did not produce the complete payload")
            if RetryHandler.attempts < 2:
                raise RuntimeError("truncated transfer was not retried")
            if list(destination.parent.glob("artifact.zip.partial.*")):
                raise RuntimeError("successful download retained a partial artifact")
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
