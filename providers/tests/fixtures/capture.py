#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""重新录制 parse 单测 fixture(需外网;YouTube 需 QXVOTUBE_PROXY)。

用法:
    python3 tests/fixtures/capture.py            # 全部
    python3 tests/fixtures/capture.py bilibili   # 单平台

裁剪策略:数组只保留前 3 个元素,字符串截断到 300 字符,
YouTube 搜索/播放列表额外保留含 continuation / watchEndpoint 的条目,
使体积从 MB 级降到几十 KB,同时不破坏解析路径。
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
UA_WEB = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36")
UA_AND = "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip"
PROXY = os.environ.get("QXVOTUBE_PROXY", "")


def curl(url, ua=UA_WEB, headers=None, proxy=None, follow=True):
    cmd = ["curl", "-s", "--max-time", "40"]
    if follow:
        cmd.append("-L")
    if proxy:
        cmd += ["-x", proxy]
    cmd += ["-H", "User-Agent: " + ua]
    for h in headers or []:
        cmd += ["-H", h]
    cmd.append(url)
    return subprocess.run(cmd, capture_output=True).stdout


DROP = {"trackingParams", "clickTrackingParams", "loggingContext",
        "logContext", "responseContext", "commandMetadata", "attributedText",
        "serviceTrackingParams", "responseId", "visitTrackingParams",
        "innertubeApiKey", "INNERTUBE_API_KEY", "INNERTUBE_CONTEXT",
        "NONCE", "DEVICE", "EXPERIMENT_FLAGS"}


def prune(o, cap=2, keep_keys=("continuationCommand", "nextContinuationData",
                              "reloadContinuationData", "watchEndpoint")):
    if isinstance(o, dict):
        return {k: prune(v, cap, keep_keys) for k, v in o.items() if k not in DROP}
    if isinstance(o, list):
        kept = []
        special = []
        for it in o:
            s = json.dumps(it, ensure_ascii=False) if not isinstance(it, str) else it
            if any(k in s for k in keep_keys):
                special.append(prune(it, cap, keep_keys))
            elif len(kept) < cap:
                kept.append(prune(it, cap, keep_keys))
        return kept + special
    if isinstance(o, str) and len(o) > 300:
        return o[:300]
    return o


SKIM_KEYS = ("compactVideoRenderer", "videoRenderer", "nextContinuationData",
             "continuationCommand", "reloadContinuationData", "watchEndpoint")


def skim(o):
    """YouTube 响应只保留解析所需子树(体积 MB→KB)。"""
    if isinstance(o, dict):
        if any(k in o for k in SKIM_KEYS):
            return {k: prune(v, cap=1) for k, v in o.items() if k not in DROP}
        out = {}
        for k, v in o.items():
            if k in DROP:
                continue
            r = skim(v)
            if r not in (None, {}, []):
                out[k] = r
        return out
    if isinstance(o, list):
        r = [skim(x) for x in o]
        return [x for x in r if x not in (None, {}, [])]
    return None


def save(name, obj):
    path = os.path.join(HERE, name)
    with open(path, "w", encoding="utf-8") as f:
        slim = skim(obj) if name.startswith("youtube") else prune(obj)
        json.dump(slim, f, ensure_ascii=False)
    print(f"  {name}: {os.path.getsize(path)} bytes")


def router_data(raw):
    html = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else raw
    i = html.find("_ROUTER_DATA")
    if i < 0:
        return None
    b = html.index("{", i)
    j, _ = json.JSONDecoder().raw_decode(html, b)
    return j


def cap_bilibili():
    s = json.loads(curl("https://api.bilibili.com/x/web-interface/search/all/v2"
                        "?keyword=travel&page=1&page_size=3",
                        headers=["Referer: https://www.bilibili.com/"]))
    # 只保留 video 分组(裁剪会按数组前 N 截断,其它分组会挤掉它)
    groups = (s.get("data") or {}).get("result") or []
    s["data"]["result"] = [g for g in groups if g.get("result_type") == "video"]
    save("bilibili_search.json", s)
    vids = []
    for g in s.get("data", {}).get("result", []):
        if g.get("result_type") == "video":
            vids = [x.get("bvid") for x in g.get("data", [])]
    if vids:
        v = json.loads(curl("https://api.bilibili.com/x/web-interface/view?bvid=" +
                            vids[0], headers=["Referer: https://www.bilibili.com/"]))
        save("bilibili_view.json", v)


def cap_hongguo():
    s = json.loads(curl("https://hongguoduanju.com/api/search/result?query=travel&page=1",
                        headers=["Referer: https://hongguoduanju.com/",
                                 "Origin: https://hongguoduanju.com"]))
    save("hongguo_search.json", s)
    home = router_data(curl("https://hongguoduanju.com/",
                            headers=["Referer: https://hongguoduanju.com/"]))
    if home:
        save("hongguo_home.json", home)
    sid = s["searchList"][0]["video_data"]["series_id"]
    detail = router_data(curl("https://hongguoduanju.com/detail?series_id=" + sid,
                              headers=["Referer: https://hongguoduanju.com/"]))
    if detail:
        save("hongguo_detail.json", detail)


def cap_youtube():
    if not PROXY:
        print("  ! 跳过 YouTube(未设 QXVOTUBE_PROXY)")
        return
    ctx = {"client": {"clientName": "ANDROID", "clientVersion": "21.26.364",
                      "androidSdkVersion": 30, "userAgent": UA_AND,
                      "osName": "Android", "osVersion": "11"}}
    for name, body in (
        ("youtube_search.json", {"context": ctx, "query": "music"}),
        ("youtube_playlist.json", {"context": ctx,
                                   "browseId": "VLPLBCF2DAC6FFB574DE"}),
    ):
        r = subprocess.run(["curl", "-s", "--max-time", "60", "-x", PROXY,
                            "https://www.youtube.com/youtubei/v1/" +
                            ("search" if "search" in name else "browse") +
                            "?prettyPrint=false",
                            "-H", "Content-Type: application/json",
                            "-H", "User-Agent: " + UA_AND,
                            "--data", json.dumps(body)], capture_output=True)
        save(name, json.loads(r.stdout))


def cap_douyin():
    s = json.loads(curl("https://www.douyin.com/aweme/v1/web/hot/search/list/"
                        "?device_platform=webapp&aid=6383&channel=channel_pc_web",
                        headers=["Referer: https://www.douyin.com/"]))
    save("douyin_hot.json", s)


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("all", "bilibili"):
        cap_bilibili()
    if which in ("all", "hongguo"):
        cap_hongguo()
    if which in ("all", "youtube"):
        cap_youtube()
    if which in ("all", "douyin"):
        cap_douyin()
