#!/usr/bin/env bash
set -euo pipefail

# Usage: ./dev/run-in-docker.sh [--build]
# Mounts the current repository into /work inside the container and opens a shell.

IMAGE_NAME=testos-dev:latest

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

if [[ ${1:-} == --build ]]; then
  docker build --platform linux/amd64 -t ${IMAGE_NAME} -f ${REPO_ROOT}/dev/Dockerfile ${REPO_ROOT}
fi

docker run --rm -it \
  -v "${REPO_ROOT}:/work" \
  -p 5900:5900 \
  -w /work \
  --name testos-dev \
  ${IMAGE_NAME} /bin/bash
