# 播放条/队列/下载(qlmusic 移植,2026-11-18)

## 播放条布局(中央一排,qlmusic 口径)

```
[自动跳过] [上一] [播放/暂停] [下一] [循环] [随机] [喇叭] [下载pie]      [1x][CC][EQ][图]
```
- 自动跳过:圆圈+勾;关=灰。失败自动换下一项(阈値 99 次连续失败止损;
  同一项双通道报错只跳一次;播放成功清零计数)。
- 循环:off(圆点)/all(双弧箭头)/one(圆+1);`player.repeat` 持久化。
- 随机:交叉箭头;`player.shuffle` 持久化。
- 喇叭:点击弹层(自绘滑条 + 静音勾选);滚轮 ±5;0/静音画斜杠;
  `player.volume`/`player.mute` 持久化。
- 下载 pie:0-100 扇形 / -1 未定 / -2 失败红斜杠 / 100 后 1.2s 自动隐。

## 连播语义(advanceNext)

- 自然播完(fromAuto=true):仅 `repeat=all` 环绕;one 重播;shuffle 随机≠当前。
- 手动 next(fromAuto=false):尾部恒环绕(Spotify 惯例);prev 恒顺序。
- 三处队列统一:视频队列 / argv 播放列表 / 音频库歌单。

## 媒体下载(真实进度)

- `Downloder`: `ffmpeg -nostdin -y [-user_agent UA] [-headers Referer] [-http_proxy P] -i URL -c copy -progress pipe:1 -nostats out.mp4`
- 入口:工具菜单「下载当前视频」;输出 `~/Downloads/qlvotube/<标题>.mp4`(重名自动编号)。
- 依赖 resolve 得到的直链;防盗链头/代理随 provider 自动带(bilibili Referer/UA、YouTube 代理)。

## 播放队列快照(仅视频队列)

- 文件:`~/.config/qlvotube/queue.json`
  `{provider,chip,current,items:[{id,title,author,cover,durationMs,webUrl}]}`
- 写入:进入视频模式/切曲/退出窗口。
- 恢复:启动时重建队列与「播放队列」面板,摆好不自动播放;provider 不匹配则丢弃。

## macOS 默认窗口 3 倍屏宽(qlmusic 三件套)

1. 构造末尾 `layout()->setSizeConstraint(QLayout::SetNoConstraint)`(Qt4+;
   mac/Qt4 布局最小宽会把窗口顶到数屏宽)。
2. `showEvent` 首次显示夹取:`min(980, avail*0.9) × min(640, avail*0.9)` 并居中。
3. `avail < minimumSizeHint` 时打一次 `[win]` 诊断日志(各子件最小宽)。
