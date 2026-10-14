#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
# Start launcher for Jetson IMX477 Camera Studio

REPO_DIR="/home/yujin/codespace/jetson-IMX477-studio"
LOG_FILE="/tmp/imx477_studio.log"
PORT=8477
URL="http://127.0.0.1:${PORT}"

cd "$REPO_DIR" || exit 1

# 1. If already running and healthy, simply open the browser
if pgrep -f "bin/studio" >/dev/null && curl -s --max-time 1 "${URL}/api/state" >/dev/null; then
    which notify-send >/dev/null 2>&1 && notify-send "IMX477 Studio" "스튜디오가 이미 실행 중입니다. 브라우저를 엽니다." -i camera-video
    xdg-open "${URL}/" 2>/dev/null || true
    exit 0
fi

# 2. If a dead/hanging instance exists, clean it up first
if pgrep -f "bin/studio" >/dev/null; then
    pkill -TERM -f "bin/studio" 2>/dev/null || true
    sleep 1
    pkill -KILL -f "bin/studio" 2>/dev/null || true
    sleep 0.5
fi

# 3. Launch background service
nohup ./bin/studio --host 0.0.0.0 --port "${PORT}" --ai-width 320 > "${LOG_FILE}" 2>&1 &

# 4. Wait up to 10 seconds for the service to become ready
READY=false
for i in {1..20}; do
    if curl -s --max-time 1 "${URL}/api/state" >/dev/null; then
        READY=true
        break
    fi
    sleep 0.5
done

if [ "$READY" = true ]; then
    which notify-send >/dev/null 2>&1 && notify-send "IMX477 Studio" "120fps 카메라 스튜디오가 실행되었습니다." -i camera-video
    xdg-open "${URL}/" 2>/dev/null || true
else
    which notify-send >/dev/null 2>&1 && notify-send "IMX477 Studio" "스튜디오 실행에 실패했습니다. 로그: ${LOG_FILE}" -i dialog-error
fi
