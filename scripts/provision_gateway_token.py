#!/usr/bin/env python3
from __future__ import annotations

import argparse
import getpass
import os
import secrets
import subprocess
from pathlib import Path


SERVICE = "agent-orb-gateway-token"
HEADER = (
    Path(__file__).resolve().parents[1]
    / "firmware"
    / "agent-orb-dfr1221"
    / "include"
    / "gateway_token.h"
)


def keychain_token() -> str | None:
    result = subprocess.run(
        [
            "/usr/bin/security",
            "find-generic-password",
            "-a",
            getpass.getuser(),
            "-s",
            SERVICE,
            "-w",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    token = result.stdout.strip()
    return token if len(token) == 64 and all(char in "0123456789abcdef" for char in token) else None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Provision the Agent Orb device token without printing it."
    )
    parser.add_argument(
        "--rotate",
        action="store_true",
        help="replace an existing token instead of reusing it",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    token = (
        secrets.token_hex(32)
        if args.rotate
        else keychain_token() or secrets.token_hex(32)
    )
    subprocess.run(
        [
            "/usr/bin/security",
            "add-generic-password",
            "-U",
            "-a",
            getpass.getuser(),
            "-s",
            SERVICE,
            "-w",
            token,
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )

    HEADER.parent.mkdir(parents=True, exist_ok=True)
    temporary = HEADER.with_suffix(".tmp")
    temporary.write_text(
        '#pragma once\n\n#define ORB_GATEWAY_TOKEN "' + token + '"\n',
        encoding="utf-8",
    )
    os.chmod(temporary, 0o600)
    temporary.replace(HEADER)
    print(f"Gateway token provisioned in Keychain and {HEADER}")


if __name__ == "__main__":
    main()
