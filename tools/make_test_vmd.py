#!/usr/bin/env python3
"""動作確認用の合成VMDモーション(res/motion/test.vmd)を作る。実際のダンスではなく、
腕・頭・センターの動き、両足IKの持ち上げ、IKのオン/オフ、補間曲線のテスト用。

使い方: python3 tools/make_test_vmd.py [出力パス]
"""
import math
import struct
import sys

FPS_FRAMES = 90  # 3秒でループ


def sjis(text, size):
    data = text.encode("cp932")
    assert len(data) <= size, text
    return data + b"\x00" * (size - len(data))


def quat_axis_angle(axis, angle):
    s = math.sin(angle / 2.0)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2.0))


LINEAR = (20, 20, 107, 107)   # 緩やかな補間(MMDの既定)
EASE_IN_OUT = (80, 0, 47, 127)  # 強いイーズ(始めと終わりがゆっくり)


def interpolation(curve):
    """4本の曲線(移動X,Y,Z,回転)に同じ(x1,y1,x2,y2)を使う64バイト。曲線cは c*16 + k*4 の位置"""
    ip = bytearray(64)
    for c in range(4):
        for k in range(4):
            ip[c * 16 + k * 4] = curve[k]
    return bytes(ip)


def bone_key(name, frame, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0), curve=LINEAR):
    return (sjis(name, 15) + struct.pack("<I3f4f", frame, *translation, *rotation) + interpolation(curve))


keys = []
Z = (0.0, 0.0, 1.0)
X = (1.0, 0.0, 0.0)

# センター: 2回しゃがむ
for frame, y in [(0, 0.0), (15, -0.8), (30, 0.0), (45, -0.8), (60, 0.0), (FPS_FRAMES, 0.0)]:
    keys.append(bone_key("センター", frame, (0.0, y, 0.0)))

# 腕: 左右が逆に振れる(最初はイーズ、あとは既定の補間)
for frame, angle, curve in [(0, 0.0, LINEAR), (30, 0.9, EASE_IN_OUT), (60, -0.4, LINEAR), (FPS_FRAMES, 0.0, LINEAR)]:
    keys.append(bone_key("左腕", frame, rotation=quat_axis_angle(Z, angle), curve=curve))
    keys.append(bone_key("右腕", frame, rotation=quat_axis_angle(Z, -angle), curve=curve))

# 頭: うなずく
for frame, angle in [(0, 0.0), (22, 0.3), (45, -0.1), (67, 0.3), (FPS_FRAMES, 0.0)]:
    keys.append(bone_key("頭", frame, rotation=quat_axis_angle(X, angle)))

# 足IK: 左足、少し遅れて右足を持ち上げる。足IKボーンの移動は、足首の目標位置を動かす
for frame, y, z in [(0, 0.0, 0.0), (20, 4.0, 2.0), (40, 0.0, 0.0), (FPS_FRAMES, 0.0, 0.0)]:
    keys.append(bone_key("左足ＩＫ", frame, (0.0, y, z)))
for frame, y, z in [(0, 0.0, 0.0), (50, 0.0, 0.0), (65, 4.0, 2.0), (80, 0.0, 0.0), (FPS_FRAMES, 0.0, 0.0)]:
    keys.append(bone_key("右足ＩＫ", frame, (0.0, y, z)))

out = bytearray()
out += b"Vocaloid Motion Data 0002" + b"\x00" * 5
out += sjis("湊あくあ", 20)
out += struct.pack("<I", len(keys))
for k in keys:
    out += k
out += struct.pack("<I", 0)  # モーフ
out += struct.pack("<I", 0)  # カメラ
out += struct.pack("<I", 0)  # 照明
out += struct.pack("<I", 0)  # セルフシャドウ
# IKのオン/オフ: 右足IKをフレーム66〜72の間だけオフにする(持ち上げの最中。オフの間は足が下がる)
ik_frames = [
    (0, [("左足ＩＫ", 1), ("右足ＩＫ", 1)]),
    (66, [("左足ＩＫ", 1), ("右足ＩＫ", 0)]),
    (72, [("左足ＩＫ", 1), ("右足ＩＫ", 1)]),
]
out += struct.pack("<I", len(ik_frames))
for frame, states in ik_frames:
    out += struct.pack("<IBI", frame, 1, len(states))
    for name, on in states:
        out += sjis(name, 20) + struct.pack("<B", on)

path = sys.argv[1] if len(sys.argv) > 1 else "res/motion/test.vmd"
with open(path, "wb") as f:
    f.write(out)
print(f"wrote {path}: {len(keys)} bone keys, {len(out)} bytes")
