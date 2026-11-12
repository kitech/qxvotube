#!/bin/bash
# qskvotube 视觉抓拍(2026-11 固化):WM_CLASS 精确定位截图。
# 教训:wmctrl 标题匹配会被同名标题终端/残留窗口污染,必须用
# `wmctrl -lx` 按 WM_CLASS(q6vtube) 过滤,且先 pkill 清残留。
# 用法:./qa_visual.sh [媒体] [引擎]   缺省 media=/tmp/opencode/cxvideo_selftest.mp4 engine=ffmpeg
set -u
cd "$(dirname "$0")"

MEDIA=${1:-/tmp/opencode/m5a_long.mp4}
ENG=${2:-ffmpeg}
[ -f "$MEDIA" ] || { echo "qa_visual: 媒体不存在: $MEDIA"; exit 2; }

pkill -9 -x q6vtube 2>/dev/null; sleep 1   # 清残留(残窗曾污染历史截图)
setsid env QSG_RHI_BACKEND=opengl \
    LD_LIBRARY_PATH="/opt/qt/6.7.3/gcc_64/lib:/opt/qt/qskinny/lib/qskinny" \
    QLVOTUBE_ENGINE=$ENG \
    QLVOTUBE_AUTOQUIT_MS=25000 \
    QLVOTUBE_AUTOPLAY="$MEDIA" \
    ./build/q6vtube > /tmp/opencode/qa_visual.log 2>&1 < /dev/null &
sleep 8
WID=$(wmctrl -lx | awk '/q6vtube/ {print $1; exit}')  # WM_CLASS 精确匹配
[ -z "$WID" ] && { echo "qa_visual: FAIL (窗口未找到)"; exit 1; }
xdotool windowactivate "$WID" 2>/dev/null
sleep 1
OUT=/tmp/opencode/qa_visual_${ENG}.png
import -window "$WID" "$OUT"
pkill -9 -x q6vtube 2>/dev/null
echo "qa_visual: $OUT(人工核对按钮/文字/进度条渲染;烧录时间码属视频内容非 UI)"
