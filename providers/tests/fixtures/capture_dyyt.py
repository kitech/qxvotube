#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""重录 douyin / youtube 分类相关 fixtures(需外网;YouTube 需代理)。

用法:
    python3 tests/fixtures/capture_dyyt.py [proxy]
默认 proxy=http://127.0.0.1:10809。

产出(douyin 走 __ac_signature+uifid+Argus 签名链,真响应裁剪):
    douyin_detail.json    详情(bit_rate 6 档,故意倒序以验证排序)
    douyin_search.json    搜索(合成卡:type=1 视频 + type=4 广告卡)
    yt_guide.json         导航(UC* 主题 + FE*/SP* 导航项)
    yt_browse_music.json  lockupViewModel(2 VIDEO + 1 PLAYLIST)
    yt_browse_gaming.json videoCardRenderer x2
    yt_browse_live.json   videoRenderer x2
裁剪原则:只保留解析所需字段;URL 用 example.invalid 占位,不落真实
媒体地址;YouTube 数组截到 2 项。
"""
import gzip
import http.cookiejar
import json
import os
import subprocess
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36')
PROXY = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:10809'
AWEME_ID = '7322042668907842867'


def acsig(ts, site, nonce, ua):
    def cal1(s, k):
        for c in s:
            k = ((k ^ ord(c)) * 65599) & 0xFFFFFFFF
        return k

    def cal3(s, k):
        for c in s:
            k = (k * 65599 + ord(c)) & 0xFFFFFFFF
        return k

    def ch(n):
        return chr(n + 65) if n < 26 else chr(n + 71) if n < 52 \
            else chr(n - 4) if n < 62 else chr(n - 17)

    def enc(v):
        return ''.join(ch((v >> i) & 63) for i in range(24, -1, -6))
    a = cal1(site, cal1(str(ts), 0)) % 65521
    b = int('10000000110000' + format((ts ^ (a * 65521)) & 0xFFFFFFFF, '032b'), 2)
    c = cal1(str(b), 0)
    d = enc(b >> 2)
    e = b // 4294967296
    f = enc(((b << 28) & 0xFFFFFFFF) | (e >> 4))
    g = 582085784 ^ b
    h = enc(((e << 26) | (g >> 6)) & 0xFFFFFFFF)
    i = ch(g & 63)
    j = ((cal1(ua, c) % 65521) << 16) | (cal1(nonce, c) % 65521)
    k = enc(j >> 2)
    l = enc(((j << 28) | ((524576 ^ b) >> 4)) & 0xFFFFFFFF)
    n = '_02B4Z6wo00f01' + d + f + h + i + k + l + enc(a)
    return n + format(cal3(n, 0), 'x')[-2:]


def write(name, obj):
    path = os.path.join(HERE, name)
    open(path, 'w').write(json.dumps(obj, ensure_ascii=False, indent=1))
    print('%-22s %d bytes' % (name, os.path.getsize(path)))


def capture_douyin():
    import hashlib
    cj = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

    def get(url):
        req = urllib.request.Request(url, headers={
            'User-Agent': UA, 'Accept-Language': 'zh-CN,zh;q=0.9',
            'Referer': 'https://www.douyin.com/'})
        r = op.open(req, timeout=25)
        b = r.read()
        try:
            b = gzip.decompress(b)
        except Exception:
            pass
        return r, b

    get('https://www.douyin.com/')
    nonce = [c.value for c in cj if c.name == '__ac_nonce'][0]
    sig = acsig(int(time.time()), 'www.douyin.com/', nonce, UA)
    cj.set_cookie(http.cookiejar.Cookie(
        0, '__ac_signature', sig, None, False, 'www.douyin.com', True, True,
        '/', True, True, None, False, None, None, {}))
    get('https://www.douyin.com/')
    uifid = [c.value for c in cj if c.name == 'UIFID_TEMP'][0]
    q = ('device_platform=webapp&aid=6383&channel=channel_pc_web'
         '&pc_client_type=1&cookie_enabled=true&screen_width=1920'
         '&screen_height=1080&browser_language=zh-CN&browser_platform=Win32'
         '&browser_name=Chrome&browser_version=146.0.0.0&browser_online=true'
         '&engine_name=Blink&engine_version=146.0.0.0&os_name=Windows'
         '&os_version=10&cpu_core_num=16&device_memory=8&platform=PC'
         '&downlink=10&effective_type=4g&round_trip_time=50&aweme_id=' + AWEME_ID)
    ts = str(int(time.time()))
    q2 = q + '&uifid=' + urllib.parse.quote(uifid) + '&timestamp=' + ts
    s2 = hashlib.md5((uifid + '_' + ts + '_A96D855A08C0A9707F8BEF0D9A527E4E_'
                      + q2).encode()).hexdigest()
    r, b = get('https://www.douyin.com/aweme/v1/web/aweme/detail/?'
               + q2 + '&x-secsdk-web-signature=' + s2)
    aw = json.loads(b).get('aweme_detail') or {}
    v = aw.get('video') or {}
    gears = [{'gear_name': br.get('gear_name'), 'bit_rate': br.get('bit_rate'),
              'play_addr': {'url_list': ['https://example.invalid/%s.mp4'
                                         % br.get('gear_name')]}}
             for br in (v.get('bit_rate') or [])[:6]]
    write('douyin_detail.json', {
        'status_code': 0,
        'aweme_detail': {
            'aweme_id': aw.get('aweme_id'),
            'desc': (aw.get('desc') or '')[:60],
            'create_time': aw.get('create_time'),
            'author': {'nickname': (aw.get('author') or {}).get('nickname')},
            'video': {'duration': v.get('duration'),
                      'cover': {'url_list': ['https://example.invalid/c.jpg']},
                      'bit_rate': list(reversed(gears))}}})
    write('douyin_search.json', {'status_code': 0, 'data': [
        {'type': 1, 'aweme_info': {
            'aweme_id': AWEME_ID, 'desc': '视频A', 'create_time': 1704795919,
            'author': {'nickname': '作者A'},
            'video': {'duration': 15000,
                      'cover': {'url_list': ['https://example.invalid/a.jpg']}}}},
        {'type': 4, 'card_info': {'title': '广告卡'}},
        {'type': 1, 'aweme_info': {
            'aweme_id': AWEME_ID[:-1] + '8', 'desc': '视频B',
            'create_time': 1704795929, 'author': {'nickname': '作者B'},
            'video': {'duration': 25000,
                      'cover': {'url_list': ['https://example.invalid/b.jpg']}}}}]})


def yt_post(bid):
    body = {'context': {'client': {
        'clientName': 'WEB', 'clientVersion': '2.20241126.01.00',
        'hl': 'en', 'gl': 'US'}}, 'browseId': bid}
    r = subprocess.run([
        'curl', '-s', '--proxy', PROXY, '--max-time', '35',
        'https://www.youtube.com/youtubei/v1/guide?prettyPrint=false'
        if bid is None else
        'https://www.youtube.com/youtubei/v1/browse?prettyPrint=false',
        '-H', 'Content-Type: application/json', '-H', 'User-Agent: ' + UA,
        '-H', 'X-Youtube-Client-Name: 1',
        '-H', 'X-Youtube-Client-Version: 2.20241126.01.00',
        '-H', 'Origin: https://www.youtube.com',
        '--data', json.dumps({k: v for k, v in body.items() if v})],
        capture_output=True)
    return json.loads(r.stdout)


def find_all(o, key, out):
    if isinstance(o, dict):
        if key in o:
            out.append(o[key])
        for v in o.values():
            find_all(v, key, out)
    elif isinstance(o, list):
        for v in o:
            find_all(v, key, out)


def src(sources, n=2):
    return [{'url': s.get('url'), 'width': s.get('width'),
             'height': s.get('height')} for s in (sources or [])[:n]]


def first_text(node):
    runs = (node or {}).get('runs') or [{}]
    return runs[0].get('text')


def lockup(lv):
    md = (lv.get('metadata') or {}).get('lockupMetadataViewModel') or {}
    rows = ((md.get('metadata') or {}).get('contentMetadataViewModel')
            or {}).get('metadataRows') or []
    parts = (rows[0] if rows else {}).get('metadataParts') or [{}]
    author = (parts[0].get('text') or {}).get('content')
    img = ((lv.get('contentImage') or {}).get('thumbnailViewModel')
           or {}).get('image') or {}
    return {'contentId': lv.get('contentId'),
            'contentType': lv.get('contentType'),
            'contentImage': {'thumbnailViewModel': {
                'image': {'sources': src(img.get('sources'))}}},
            'metadata': {'lockupMetadataViewModel': {
                'title': {'content': (md.get('title') or {}).get('content')},
                'metadata': {'contentMetadataViewModel': {'metadataRows': [
                    {'metadataParts': [{'text': {'content': author}}]}]}}}}}


def vcard(vc):
    return {'videoId': vc.get('videoId'),
            'thumbnail': {'thumbnails': src(
                (vc.get('thumbnail') or {}).get('thumbnails'))},
            'title': {'runs': [{'text': first_text(vc.get('title'))}]},
            'bylineText': {'runs': [{'text': first_text(vc.get('bylineText'))}]}}


def vrender(vr):
    return {'videoId': vr.get('videoId'),
            'thumbnail': {'thumbnails': src(
                (vr.get('thumbnail') or {}).get('thumbnails'))},
            'title': {'runs': [{'text': first_text(vr.get('title'))}]},
            'ownerText': {'runs': [{'text': first_text(vr.get('ownerText'))}]}}


def capture_youtube():
    g = yt_post(None)
    ents = []
    find_all(g, 'guideEntryRenderer', ents)
    write('yt_guide.json', {'items': [
        {'guideEntryRenderer': {
            'formattedTitle': ({'simpleText': (
                e.get('formattedTitle') or {}).get('simpleText')}
                if (e.get('formattedTitle') or {}).get('simpleText') else
                {'runs': [{'text': first_text(e.get('formattedTitle'))}]}),
            'navigationEndpoint': {'browseEndpoint': {
                'browseId': ((e.get('navigationEndpoint') or {})
                             .get('browseEndpoint') or {}).get('browseId'),
                'params': ((e.get('navigationEndpoint') or {})
                           .get('browseEndpoint') or {}).get('params')}}}}
        for e in ents]})
    lvs = []
    find_all(yt_post('UC-9-kyTW8ZkZNDHQJ6FgpwQ'), 'lockupViewModel', lvs)
    vid = [x for x in lvs
           if x.get('contentType') == 'LOCKUP_CONTENT_TYPE_VIDEO'][:2]
    pl = [x for x in lvs
          if x.get('contentType') == 'LOCKUP_CONTENT_TYPE_PLAYLIST'][:1]
    write('yt_browse_music.json', {'items': [
        {'lockupViewModel': lockup(x)} for x in vid + pl]})
    vcs = []
    find_all(yt_post('UCOpNcN46UbXVtpKMrmU4Abg'), 'videoCardRenderer', vcs)
    write('yt_browse_gaming.json', {'items': [
        {'videoCardRenderer': vcard(x)} for x in vcs[:2]]})
    vrs = []
    find_all(yt_post('UC4R8DWoMoI7CAwX8_LjQHig'), 'videoRenderer', vrs)
    write('yt_browse_live.json', {'items': [
        {'videoRenderer': vrender(x)} for x in vrs[:2]]})


if __name__ == '__main__':
    capture_douyin()
    capture_youtube()
