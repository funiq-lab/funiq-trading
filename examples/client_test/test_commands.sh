#!/usr/bin/env bash
set -euo pipefail
# Read-only health check; does not create orders or start trading services.
curl --fail --silent --show-error "http://127.0.0.1:8080/health"
