#!/bin/sh
# Pull dependency repos referenced by repository-root symlinks:
#   qlappcom -> ../anystik/qlappcom
#   qlcomp   -> ../anystik/qlcomp
#   qskcomp  -> ../anystik/qskcomp
#
# A fresh CI checkout has dangling symlinks, so we clone the remote
# repositories into the workspace parent directory (same mechanism as
# doxhttpd/.github/workflows/ci.yml and qxmusic/scripts/pollup_deps.sh).
#
# 以下符号链接不参与 qlvotube/qskvotube 构建，无需在 CI 拉取：
#   vendor -> ../cxvendor/vendor（内嵌业务三方件，桌面壳未引用）
set -e
set -x
pwd
git clone --depth 1 https://github.com/kitech/anystik.git ../anystik
