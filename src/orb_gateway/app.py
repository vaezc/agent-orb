from __future__ import annotations

import argparse
import json
import logging
import mimetypes
import re
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from importlib.resources import files
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from .model import InvalidTransition, OrbRegistry


LOGGER = logging.getLogger("agent-orb")
DEVICE_ROUTE = re.compile(r"^/api/v1/devices/([A-Za-z0-9_-]{1,64})/(state|events|actions)$")


class OrbGatewayServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], registry: OrbRegistry | None = None):
        super().__init__(address, OrbRequestHandler)
        self.registry = registry or OrbRegistry()


class OrbRequestHandler(BaseHTTPRequestHandler):
    server: OrbGatewayServer

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/api/v1/health":
            self._json(HTTPStatus.OK, {"ok": True, "service": "agent-orb", "version": "0.1.0"})
            return
        if parsed.path == "/api/v1/devices":
            self._json(HTTPStatus.OK, {"devices": self.server.registry.list_snapshots()})
            return

        match = DEVICE_ROUTE.match(parsed.path)
        if match and match.group(2) in {"state", "events"}:
            device = self.server.registry.get(match.group(1))
            if match.group(2) == "state":
                self._json(HTTPStatus.OK, device.snapshot().to_dict())
                return
            query = parse_qs(parsed.query)
            try:
                after = int(query.get("after", ["-1"])[0])
                timeout = min(max(float(query.get("timeout", ["20"])[0]), 0), 25)
            except ValueError:
                self._error(HTTPStatus.BAD_REQUEST, "after and timeout must be numbers")
                return
            self._json(HTTPStatus.OK, device.wait_after(after, timeout).to_dict())
            return

        self._static(parsed.path)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        match = DEVICE_ROUTE.match(parsed.path)
        if not match or match.group(2) != "actions":
            self._error(HTTPStatus.NOT_FOUND, "endpoint not found")
            return

        try:
            payload = self._read_json()
            action = payload.get("action")
            if not isinstance(action, str) or not action:
                raise ValueError("action is required and must be a string")
            snapshot = self.server.registry.get(match.group(1)).apply(action, payload)
        except InvalidTransition as exc:
            self._error(HTTPStatus.CONFLICT, str(exc))
            return
        except (ValueError, json.JSONDecodeError) as exc:
            self._error(HTTPStatus.BAD_REQUEST, str(exc))
            return
        self._json(HTTPStatus.OK, snapshot.to_dict())

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(HTTPStatus.NO_CONTENT)
        self._cors_headers()
        self.end_headers()

    def log_message(self, format: str, *args: object) -> None:
        LOGGER.info("%s - %s", self.address_string(), format % args)

    def _read_json(self) -> dict:
        content_type = self.headers.get("Content-Type", "").split(";", 1)[0]
        if content_type != "application/json":
            raise ValueError("Content-Type must be application/json")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("invalid Content-Length") from exc
        if length <= 0 or length > 16_384:
            raise ValueError("request body must contain 1-16384 bytes")
        payload = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("request body must be a JSON object")
        return payload

    def _static(self, request_path: str) -> None:
        relative = "index.html" if request_path in {"", "/"} else request_path.lstrip("/")
        if ".." in Path(relative).parts:
            self._error(HTTPStatus.NOT_FOUND, "not found")
            return
        static_root = files("orb_gateway").joinpath("static")
        asset = static_root.joinpath(relative)
        if not asset.is_file():
            self._error(HTTPStatus.NOT_FOUND, "not found")
            return
        data = asset.read_bytes()
        content_type = mimetypes.guess_type(relative)[0] or "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8" if content_type.startswith("text/") else content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status: HTTPStatus, payload: dict) -> None:
        data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self._cors_headers()
        self.end_headers()
        self.wfile.write(data)

    def _error(self, status: HTTPStatus, message: str) -> None:
        self._json(status, {"error": status.phrase, "message": message})

    def _cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Agent Orb local gateway")
    parser.add_argument("--host", default="127.0.0.1", help="listen address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8787, help="listen port (default: 8787)")
    parser.add_argument("--verbose", action="store_true", help="show HTTP request logs")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    logging.basicConfig(
        level=logging.INFO if args.verbose else logging.WARNING,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    server = OrbGatewayServer((args.host, args.port))
    print(f"Agent Orb Gateway: http://{args.host}:{server.server_port}")
    print("按 Ctrl+C 停止")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n正在停止…")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
