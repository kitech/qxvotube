#!/bin/bash
# qskvotube e2e 验收(2026-11 固化):双引擎 QLVOTUBE_QA 六步 + 退出码。
# 依赖:缺省 /tmp/opencode/m5a_long.mp4(30s,烧录时间码;shot 步在 15s,
# 媒体须长于 15s 否则截图无帧 ok=0)。串行执行勿并行。
# 用法:./qa_run.sh [媒体文件]   缺省 /tmp/opencode/m5a_long.mp4
set -u
cd "$(dirname "$0")"

MEDIA=${1:-/tmp/opencode/m5a_long.mp4}
[ -f "$MEDIA" ] || { echo "qa_run: 媒体不存在: $MEDIA(先跑 cxvideo selftest --ff-probe)"; exit 2; }

export QSG_RHI_BACKEND=opengl
export LD_LIBRARY_PATH="/opt/qt/6.7.3/gcc_64/lib:/opt/qt/qskinny/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

RET=0
for ENG in ffmpeg mpv; do
    pkill -9 -x q6vtube 2>/dev/null; sleep 1
    echo "=== qa_run: engine=$ENG ==="
    env QLVOTUBE_ENGINE=$ENG QLVOTUBE_QA=1 \
        QLVOTUBE_AUTOQUIT_MS=20000 \
        QLVOTUBE_AUTOPLAY="$MEDIA" \
        ./build/q6vtube > /tmp/opencode/qa_run_$ENG.log 2>&1
    RC=$?
    # 六步 = pause/seek/rate/cc/eq/shot;seek 步打印 issued=1 无 ok=1,
    # 判定:qa: step= 行数≥6 且无 ok=0
    STEPS=$(grep -c "^qa: step=" /tmp/opencode/qa_run_$ENG.log)
    BAD=$(grep -c "ok=0" /tmp/opencode/qa_run_$ENG.log)
    echo "engine=$ENG exit=$RC steps=$STEPS/6 bad=$BAD"
    if [ $RC -ne 0 ]; then
        echo "qa_run: FAIL (engine=$ENG 退出码 $RC)"; RET=1; continue
    fi
    if [ $STEPS -lt 6 ] || [ $BAD -ne 0 ]; then
        echo "qa_run: FAIL (engine=$ENG 六步不全或有失败,见 /tmp/opencode/qa_run_$ENG.log)"
        grep "^qa:" /tmp/opencode/qa_run_$ENG.log
        RET=1
    fi
done
pkill -9 -x q6vtube 2>/dev/null
[ $RET -eq 0 ] && echo "qa_run: ALL PASS"
exit $RET
