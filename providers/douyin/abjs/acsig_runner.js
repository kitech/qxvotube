// acsig_runner.js —— __ac_signature 参考实现(对照用,qjs -C 执行)
// 与 providers/douyin/acsig.cpp 同口径(CSDN 145899104 纯算 + FP 常量固化),
// 仅用于双机制对照:C++ 为准,本实现独立书写以捕捉移植笔误。
// 参数(quickjs scriptArgs,含脚本名):[0]=脚本 [1]=tsSec [2]=site
//                                      [3]=nonce [4]=ua
// 输出:SIG=<47 字符>
var A = scriptArgs;
var TWO32 = 4294967296;

// 逐字符哈希:((k ^ c) * 65599) mod 2^32
function cal1(s, k) {
  for (var i = 0; i < s.length; i++)
    k = ((k ^ s.charCodeAt(i)) * 65599) >>> 0;
  return k;
}

// 尾部校验哈希:((k * 65599) + c) mod 2^32
function cal3(s, k) {
  for (var i = 0; i < s.length; i++)
    k = (k * 65599 + s.charCodeAt(i)) >>> 0;
  return k;
}

// 自定义 base64 表:A-Z a-z 0-9 - .
function oneChr(n) {
  if (n < 26) return String.fromCharCode(65 + n);
  if (n < 52) return String.fromCharCode(97 + n - 26);
  if (n < 62) return String.fromCharCode(48 + n - 52);
  return (n === 62) ? '-' : '.';
}

// 25 位 → 5 字符(高 6 位一组);v 可达 2^46,不能直接用 JS 位移
function enc(v) {
  var s = '';
  for (var i = 24; i >= 0; i -= 6)
    s += oneChr(Math.floor(v / Math.pow(2, i)) % 64);
  return s;
}

function bin32(v) {
  var s = '';
  for (var i = 31; i >= 0; i--) s += (((v >>> i) & 1) ? '1' : '0');
  return s;
}

function hex2(v) {
  var s = (v >>> 0).toString(16);
  while (s.length < 2) s = '0' + s;
  return s.slice(-2);
}

var ts = Number(A[1]), site = A[2], nonce = A[3], ua = A[4];

var a = cal1(site, cal1('' + ts, 0)) % 65521;
var bits = '10000000110000' + bin32((ts ^ ((a * 65521) >>> 0)) >>> 0);
var b = parseInt(bits, 2);                     // ≤ 2^46,double 内精确
var c = cal1('' + b, 0);
var d = enc(Math.floor(b / 4));
var e = Math.floor(b / TWO32);
var f = enc(((((b % TWO32) * 268435456) % TWO32) | (e >>> 4)) >>> 0);
var g = (582085784 ^ (b % TWO32)) >>> 0;
var h = enc(((((e % 64) * 67108864) % TWO32) | (g >>> 6)) >>> 0);
var ch = oneChr(g & 63);
var j = ((cal1(ua, c) % 65521) * 65536 + (cal1(nonce, c) % 65521)) >>> 0;
var k = enc(Math.floor(j / 4));
var l = enc(((((j % 16) * 268435456) % TWO32)
             | ((524576 ^ (b % TWO32)) >>> 4)) >>> 0);
var m = enc(a);
var n = '_02B4Z6wo00f01' + d + f + h + ch + k + l + m;

print('SIG=' + n + hex2(cal3(n, 0)));
