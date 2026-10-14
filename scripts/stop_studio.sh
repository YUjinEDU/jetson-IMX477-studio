#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 YUjinEDU
# Stop script for Jetson IMX477 Camera Studio

if pgrep -f "bin/studio" >/dev/null; then
    pkill -TERM -f "bin/studio" 2>/dev/null || true
    for i in {1..8}; do
        if ! pgrep -f "bin/studio" >/dev/null; then
            break
        fi
        sleep 0.5
    done
    if pgrep -f "bin/studio" >/dev/null; then
        pkill -KILL -f "bin/studio" 2>/dev/null || true
    fi
    which notify-send >/dev/null 2>&1 && notify-send "IMX477 Studio" "카메라 스튜디오 서버가 안전하게 종료되었습니다." -i process-stop
else
    which notify-send >/dev/null 2>&1 && notify-send "IMX477 Studio" "실행 중인 스튜디오 서버가 없습니다." -i dialog-information
fi
