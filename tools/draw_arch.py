"""Draws the system architecture figure (docs/img/architecture.png).

    python tools/draw_arch.py [out.png]

Needs matplotlib and a Japanese font (Yu Gothic / Meiryo on Windows, Noto Sans CJK elsewhere).
"""
import sys
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

for name in ("Yu Gothic", "Meiryo", "Noto Sans CJK JP", "IPAexGothic", "MS Gothic"):
    if any(f.name == name for f in font_manager.fontManager.ttflist):
        plt.rcParams["font.family"] = name
        break

OUT = sys.argv[1] if len(sys.argv) > 1 else "docs/img/architecture.png"
C_IN, C_EDGE, C_CTRL, C_CORE, C_CLOUD, C_PC, C_OUT = (
    "#e8f1fb", "#e3f4e6", "#fdf3d8", "#efe6f8", "#fde6e3", "#eeeeee", "#e8f1fb")
BLUE, RED, GRAY = "#2a6db0", "#c0392b", "#555555"

fig, ax = plt.subplots(figsize=(16, 9), dpi=150)
ax.set_xlim(0, 16)
ax.set_ylim(0, 9)
ax.axis("off")


def box(x, y, w, h, title, body="", fc="#ffffff", ts=11, bs=9, lw=1.2, ec="#444444"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.12",
                                fc=fc, ec=ec, lw=lw))
    if body:
        ax.text(x + w / 2, y + h - 0.18, title, ha="center", va="top", fontsize=ts, fontweight="bold")
        ax.text(x + w / 2, y + h - 0.55, body, ha="center", va="top", fontsize=bs, linespacing=1.45)
    else:
        ax.text(x + w / 2, y + h / 2, title, ha="center", va="center", fontsize=ts, fontweight="bold")


def arrow(x1, y1, x2, y2, color="#333333", style="-|>", ls="-"):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle=style, mutation_scale=14,
                                 color=color, lw=1.6, linestyle=ls))


ax.text(8, 8.72, "システム構成: エッジAI（本体）とクラウドAIの役割分担", ha="center", fontsize=16, fontweight="bold")

# ---- inputs (left)
box(0.2, 6.4, 2.0, 1.3, "カメラ", "GC0308 320×240", C_IN, ts=10.5)
box(0.2, 5.0, 2.0, 1.1, "マイク", "音量だけ（録音しない）", C_IN, ts=10.5, bs=8.5)
box(0.2, 3.7, 2.0, 1.0, "タッチ画面", "タップ／長押し", C_IN, ts=10.5)
box(0.2, 1.45, 2.0, 1.75, "PCブラウザ", "ダッシュボード\n（LAN内のみ）", C_PC, ts=10.5, bs=8.5)

# ---- board
ax.add_patch(FancyBboxPatch((2.7, 1.2), 8.6, 7.0, boxstyle="round,pad=0.02,rounding_size=0.2",
                            fc="#fafafa", ec="#222222", lw=2.0))
ax.text(7.0, 8.05, "Stack-chan K151（CoreS3: ESP32-S3、PSRAM 8MB）", ha="center", va="top",
        fontsize=12.5, fontweight="bold")

box(3.0, 6.1, 3.7, 1.6, "エッジAI（本体で完結）",
    "顔検出 pico（1クラス、約10〜14fps）\n動き検出（フレーム差分）\n音量認識（適応しきい値）", C_EDGE)
box(7.3, 6.1, 3.7, 1.6, "追従・見回し制御",
    "積分制御（顔を中央へ）\n監視モード（±110°の往復）\n→ サーボ yaw 指令", C_CTRL)
box(3.0, 3.9, 8.0, 1.8, "loop()（単一所有者）: ポータブルコア（C++17、HW非依存）",
    "Interaction: 要求ID・状態・観測の鮮度・取消・遅着の拒否・タイムアウト\n"
    "Director: 挨拶・自分から質問・相づち・「だれか」（自律動作の方針）\n"
    "判断モード: 本体のみ／「見て」のときだけ／常に（自動の画像送信は1分に1回まで）", C_CORE, bs=8.6)
box(3.0, 1.45, 3.7, 2.1, "Web操作画面（LAN）",
    "首の角度・イベントのグラフ\nボタン: 質問・見て・判断・監視\n平文HTTP＋トークン", "#ffffff")
box(7.3, 1.45, 3.7, 2.1, "apiTask（別タスク）",
    "HTTPS（TLS検証）\nキュー: 要求1・結果2\nloop()は待たずに続行", "#ffffff")

# ---- outputs (right, top)
box(11.8, 6.55, 4.0, 1.15, "サーボ（首）", "SCS0009 ×2（yaw を使用）", C_OUT, ts=10.5)
box(11.8, 5.15, 4.0, 1.1, "画面", "顔の表情・字幕", C_OUT, ts=10.5)
box(11.8, 3.8, 4.0, 1.1, "スピーカー", "固定フレーズ・読み上げ", C_OUT, ts=10.5)
# ---- cloud (right, bottom)
box(11.8, 2.2, 4.0, 1.3, "クラウド: Gemini API",
    "受け取る: 画像1枚（「見て」のとき）\n返す: 状況の説明文・読み上げ音声", C_CLOUD, ts=10.5, bs=8.8, ec=RED)
box(11.8, 0.85, 4.0, 1.1, "クラウド: Sarashina API",
    "受け取る: 質問の文章＋状況の一行", C_CLOUD, ts=10.5, bs=8.8, ec=RED)

# ---- arrows
arrow(2.2, 7.05, 3.0, 7.05, BLUE)                 # camera -> edge
arrow(2.2, 5.6, 3.0, 6.3, BLUE)                   # mic -> edge
arrow(2.2, 4.2, 3.0, 4.5, BLUE)                   # touch -> core
arrow(6.7, 6.9, 7.3, 6.9)                         # edge -> control
arrow(4.85, 6.1, 4.85, 5.7)                       # edge -> core (検出位置・音量)
arrow(9.15, 6.1, 9.15, 5.7, style="<|-|>")        # control <-> core
arrow(11.0, 7.1, 11.8, 7.1, BLUE)                 # control -> servo
arrow(11.0, 5.45, 11.8, 5.7, BLUE)                # core -> screen
arrow(11.0, 4.3, 11.8, 4.35, BLUE)                # core -> speaker
arrow(9.15, 3.9, 9.15, 3.55, style="<|-|>")       # core <-> apiTask
arrow(4.85, 3.9, 4.85, 3.55, style="<|-|>")       # core <-> web
arrow(2.2, 2.3, 3.0, 2.3, GRAY, style="<|-|>")    # PC <-> web
arrow(11.0, 2.9, 11.8, 2.9, RED)                  # apiTask -> Gemini
arrow(11.0, 1.9, 11.8, 1.4, RED)                  # apiTask -> Sarashina

ax.text(4.0, 0.55, "音声はクラウドへ送らない。画像は「見て」（または「常にGemini」）のときだけ1枚送り、保存・ログしない（既定オフ）。",
        fontsize=9, color=RED)
ax.text(4.0, 0.2, "赤: クラウドへ出るデータ ／ 青: 本体の入出力。顔の有無・向き・音量は、本体だけで処理する。",
        fontsize=9, color="#444444")
ax.text(0.2, 0.2, "", fontsize=8)

fig.savefig(OUT, bbox_inches="tight", facecolor="white")
print("wrote", OUT)
