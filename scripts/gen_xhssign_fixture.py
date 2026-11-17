#!/usr/bin/env python3
"""用 Python xhshow 生成差分对拍 fixture(tests/fixtures/xhssign.json)。

随机注入约定(与 C++ SeqRng 消费序一一对应):
  sign_xs:      randint(0,2^32-1), randint(10,50), randint(15,50), randint(1000,1200)
  x-s-common:   x36 = 1 + seq[4] % 20(直接给定,不走 generate())

timestamp 全部为整秒 * 1000(避免浮点精度歧义)。
用法:
  python3 scripts/gen_xhssign_fixture.py /tmp/opencode/xhshow-master \
      > providers/tests/fixtures/xhssign.json
"""
import json
import sys

sys.path.insert(0, sys.argv[1] + "/src")

import random as _random
import time as _time

from xhshow.client import Xhshow
from xhshow.core.common_sign import XsCommonSigner
from xhshow.config import CryptoConfig
from xhshow.generators.fingerprint import FingerprintGenerator
from xhshow.core.crc32_encrypt import CRC32

MASTER = _random.Random(20261117)
TS_SEC = 1731900000.0
TS_MS = 1731900000000


class SeqRandom:
    """替换 xhshow.utils.random_gen 里的模块级 random:按固定序列消费。"""

    def __init__(self, seq):
        self.seq = list(seq)
        self.i = 0

    def randint(self, lo, hi):
        v = self.seq[self.i % len(self.seq)]
        self.i += 1
        return lo + v % (hi - lo + 1)

    def choices(self, options, weights=None, k=1):
        v = self.seq[self.i % len(self.seq)]
        self.i += 1
        return [options[v % len(options)]]

    def choice(self, options):
        v = self.seq[self.i % len(self.seq)]
        self.i += 1
        return options[v % len(options)]


def make_case(i, rng_master):
    seq = [rng_master.getrandbits(32) for _ in range(5)]
    method = ["GET", "POST", "GET"][i % 3]
    uri = [
        "/api/sns/web/v1/homefeed",
        "/api/sns/web/v1/user_posted",
        "/api/sns/web/v1/feed",
        "/api/sns/web/v2/search/notes",
    ][i % 4]
    a1 = "a1=%05x" % (i * 7919 % 0xFFFFF)
    payload = None
    if method == "POST":
        if i % 2:
            payload = {"note_id": "note%03d" % i, "cursor": str(i % 50)}
        else:
            payload = {"id": "post%d" % i}
    return seq, method, uri, a1, payload


def main():
    import xhshow.utils.random_gen as rgmod
    import xhshow.generators.fingerprint_helpers as fhmod
    import xhshow.generators.fingerprint as fpmod

    client = Xhshow()
    out = []
    for i in range(1000):
        seq, method, uri, a1, payload = make_case(i, MASTER)
        sr = SeqRandom(seq)
        rgmod.random = sr          # CryptoProcessor.random_gen 的随机源
        fhmod.random = sr          # 指纹 helpers(不影响 b1 子集,占位)
        xs = client.sign_xs(method, uri, a1,
                            payload=payload if method == "POST" else None,
                            timestamp=TS_SEC)

        # x-s-common:走 generate_b1 直注(x36 = 1 + seq[4]%20,与 C++ 一致)
        cfg = CryptoConfig()
        fg = FingerprintGenerator(cfg)
        fp = {
            "x33": "0", "x34": "0", "x35": "0",
            "x36": "%d" % (1 + seq[4] % 20),
            "x37": "0|0|0|0|0|0|0|0|0|1|0|0|0|0|0|0|0|0|1|0|0|0|0|0",
            "x38": "0|0|1|0|1|0|0|0|0|0|1|0|1|0|1|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0",
            "x39": 0,
            "x42": "3.4.4",
            "x43": "742cc32c",
            "x44": "%d" % TS_MS,
            "x45": "__SEC_CAV__1-1-1-1-1|__SEC_WSA__|",
            "x46": "false",
            "x48": "", "x49": "{list:[],type:}",
            "x50": "", "x51": "", "x52": "",
            "x82": "_0x17a2|_0x1954",
        }
        b1 = fg.generate_b1(fp)
        x9 = CRC32.crc32_js_int(b1)
        sign_struct = dict(cfg.SIGNATURE_XSCOMMON_TEMPLATE)
        sign_struct["x5"] = a1
        sign_struct["x8"] = b1
        sign_struct["x9"] = x9
        from xhshow.utils.encoder import Base64Encoder
        xsc = Base64Encoder(cfg).encode(
            json.dumps(sign_struct, separators=(",", ":"), ensure_ascii=False))

        out.append({
            "method": method, "uri": uri, "a1": a1,
            "payload": payload, "tsMs": TS_MS, "seq": seq,
            "expectXs": xs, "expectXsCommon": xsc,
        })
    json.dump({"tsMs": TS_MS, "cases": out}, sys.stdout, ensure_ascii=False)
    print()


main()
