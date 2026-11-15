#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""重新录制小红书 parse fixture(首页/详情/用户页)。

注意:小红书风控严格,本脚本按 XHS-Downloader 默认节流(均值 6s,clamp 1.2~30s),
连续运行会触发降级(详情返回壳)。建议一次只录一份。
用法:python3 tests/fixtures/capture_xhs.py [explore|detail|user|all]
"""
import json, os, random, subprocess, sys, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36")
JAR = os.path.join(HERE, ".xhs.jar")
DROP = {"trackId", "trackingParams", "ssrRendered", "exposed", "traceId",
        "fileId", "stream", "infoList"}


def sleep():
    d = min(max(random.lognormvariate(6.0 ** 1 - 0.18, 0.6), 1.2), 30)
    time.sleep(d)


def get(url):
    cmd = ["curl", "-sL", "--max-time", "30", url, "-H", "User-Agent: " + UA,
           "-H", "Accept: text/html,application/xhtml+xml",
           "-H", "Accept-Language: zh-CN,zh;q=0.9", "-b", JAR, "-c", JAR]
    return subprocess.run(cmd, capture_output=True).stdout.decode("utf8", "replace")


def state(html):
    i = html.find("__INITIAL_STATE__")
    if i < 0:
        return None
    b = html.index("{", i)
    raw = html[b:]
    end = raw.find("</script>")
    return json.JSONDecoder().raw_decode(raw[:end].replace("undefined", "null"))[0]


def prune(o, cap=3):
    if isinstance(o, dict):
        return {k: prune(v, cap) for k, v in o.items() if k not in DROP}
    if isinstance(o, list):
        return [prune(x, cap) for x in o[:cap]]
    if isinstance(o, str) and len(o) > 260:
        return o[:260]
    return o


def save(name, obj):
    p = os.path.join(HERE, name)
    json.dump(prune(obj), open(p, "w"), ensure_ascii=False)
    print(f"  {name}: {os.path.getsize(p)} bytes")


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "explore"):
        j = state(get("https://www.xiaohongshu.com/explore"))
        save("xhs_explore_state.json", j)
        sleep()
    if what in ("all", "detail"):
        j = json.load(open(os.path.join(HERE, "xhs_explore_state.json")))
        f = j["feed"]["feeds"][0]
        url = ("https://www.xiaohongshu.com/explore/" + f["id"]
               + "?xsec_token=" + urllib.parse.quote(f["xsecToken"])
               + "&xsec_source=pc_feed")
        save("xhs_detail_state.json", state(get(url)))
        sleep()
    if what in ("all", "user"):
        j = json.load(open(os.path.join(HERE, "xhs_explore_state.json")))
        uid = j["feed"]["feeds"][0]["noteCard"]["user"]["userId"]
        save("xhs_user_state.json",
             state(get("https://www.xiaohongshu.com/user/profile/" + uid)))


if __name__ == "__main__":
    main()
