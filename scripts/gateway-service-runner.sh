#!/bin/zsh
set -euo pipefail

umask 077

REPO_DIR="$(cd "${0:A:h}/.." && pwd -P)"
SNOOPY_HOME="${SNOOPY_HOME:-$HOME/.snoopy}"
SNOOPY_CONFIG="$SNOOPY_HOME/config/server.env"

if [[ -f "$SNOOPY_CONFIG" ]]; then
  set -a
  source "$SNOOPY_CONFIG"
  set +a
fi

GATEWAY_HOST="${ORB_GATEWAY_HOST:-$(/usr/sbin/ipconfig getifaddr en0 2>/dev/null || true)}"
if [[ -z "$GATEWAY_HOST" ]]; then
  print -u2 "Agent Orb Gateway: Wi-Fi address is unavailable"
  exit 1
fi

WHISPER_CLI="${ORB_WHISPER_CLI:-/opt/homebrew/bin/whisper-cli}"
WHISPER_MODEL="${ORB_WHISPER_MODEL:-$HOME/Library/Caches/agent-orb/ggml-base.bin}"
PYTHON_BIN="${ORB_PYTHON_BIN:-/opt/homebrew/bin/python3}"

for required_file in "$WHISPER_CLI" "$WHISPER_MODEL" "$PYTHON_BIN"; do
  if [[ ! -e "$required_file" ]]; then
    print -u2 "Agent Orb Gateway: missing dependency: $required_file"
    exit 1
  fi
done

export SNOOPY_SERVER_URL="http://${SNOOPY_SERVER_HOST:-127.0.0.1}:${SNOOPY_SERVER_PORT:-4317}"
export SNOOPY_SERVER_TOKEN="$(/usr/bin/security find-generic-password -a "$(id -un)" -s snoopy-server-token -w)"
export ORB_GATEWAY_TOKEN="$(/usr/bin/security find-generic-password -a "$(id -un)" -s agent-orb-gateway-token -w)"
export ORB_WHISPER_CLI="$WHISPER_CLI"
export ORB_WHISPER_MODEL="$WHISPER_MODEL"
export ORB_WHISPER_LANGUAGE="${ORB_WHISPER_LANGUAGE:-zh}"
export NO_PROXY="${NO_PROXY:+$NO_PROXY,}${SNOOPY_SERVER_HOST:-127.0.0.1},127.0.0.1,localhost"
export no_proxy="$NO_PROXY"
export PYTHONPATH="$REPO_DIR/src"

exec "$PYTHON_BIN" -u -m orb_gateway --host "$GATEWAY_HOST" --port 8787 --verbose
