# UI:视频分类栏 与 播放器快捷键(2026-11-18)

## 视频分类(左侧栏)

- 动态分类由 provider 的 `categories()` 提供,渲染在左侧栏
  「视频站点」下拉之后、音乐分类之前(header i18n `nav.video_categories`)。
- 首个固定「推荐」= 空 key(走 `homeFeed`);其余点击触发
  `SideNav::videoCategorySelected(key)` → `MainWindow::onVideoCategorySelected`
  → `loadVideoPage(1)`。
- ContentView 顶部 chips 行已移除(网格上移 40px);分类高亮由 SideNav 负责。
- 站点切换/搜索时清空分类段。

## 快捷键(全局,搜索框聚焦时自动让行)

| 键 | 功能 |
|---|---|
| Space / K | 播放 / 暂停 |
| ← / → | 快退 / 快进 5 秒(Shift:1 秒) |
| J / L | 快退 / 快进 10 秒 |
| ↑ / ↓ | 音量 ±5% |
| M | 静音切换 |
| F | 全屏(Esc 退出) |
| 0–9 | 跳到 0%–90% |
| Home / End | 开头 / 结尾(留 1 秒) |
| Shift+, / Shift+. (即 `<` / `>`) | 减速 / 加速(0.25 步进,0.25–2.0) |
| C | 字幕轨切换 |
| H / F1 / ? | 快捷键帮助弹窗 |

实现要点:
- `MainWindow::eventFilter` 装在 `qApp` 上;`isActiveWindow()` + 焦点不是
  文本输入(QLineEdit/QTextEdit/可编辑 QComboBox)才处理。
- `ContentView` 点击时 `setFocus()`,把焦点从搜索框拿回(否则空格会被当输入)。
- 时长记录 `m_durMs`(`CXVideoPlayer::duration()` 在 ffmpeg 引擎上可能返回 0,
  时长只经 `durationChanged` 信号给出)。
- Qt3 会把 `Shift+,/.` 直接上报成 `'<'/'>'`(Key_Less/Key_Greater);
  `QComboBox` 可编辑判断在 Qt3 是 `editable()`、Qt4+ 是 `isEditable()`。

## 已知问题(非本轮引入)

- **ffmpeg 引擎 seek 后播放停住**(进度不再更新):快捷键与点击进度条
  同样表现。快捷键已确认调用到 `m_player->seek(ms)`(参数正确);
  属引擎层问题,待单独修(候选:avformat_seek_file / 重建时钟与队列)。
