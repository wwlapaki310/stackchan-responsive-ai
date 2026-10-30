# Stack-chan Responsive AI

**考えていても、そばにいるStack-chan**

クラウド生成AIの回答を待つ間も、本体の軽量AIで人を見つけ、首・表情・声・タッチ反応を止めない小型ロボット（M5Stack Stack-chan K151）。通常動作は、本体とWi-Fiとクラウドだけで完結する（PC側のプロキシは使わない）。

> 状態（2026-10-29）: 本体への実装と実機動作の一部確認まで進んだ作業中の版。提出物（動画・説明PDF）は準備中。論文は作業草稿で、評価は完了していない。確認済みと未確認は[「確認状況」](#確認状況)に分けて書く。

## 何ができるか

- **見る**: 本体カメラ（320×240）の画像から、本体上で顔検出（pico）または画像の変動（フレーム差分）を行い、首（yaw）を向ける。方式はWebから切り替えられる。
- **考える（クラウド）**: 顔を見つけて約3秒たつと、自分からSarashina APIへ質問する（Webやタッチでも質問できる）。回答は字幕で出す。
- **待つ間も止まらない**: 回答を待つ間も、顔追従・表情・タッチ反応・「えーっと」の相づちを続ける。回答が遅れて届いても、キャンセル済みや古い要求なら捨てる。
- **声と音**: 固定フレーズ（起動・挨拶・考え中・中止・エラーなど）を、本体のスピーカーで話す。マイクは音量だけを見て、大きな音に反応する（録音・保存・送信はしない）。
- **操作画面**: 同じLAN内のブラウザで、首の角度の推移、回答待ちの時間帯、イベント、カメラ映像と検出枠を見られる。本体の画面は顔と字幕だけにしている。

## 構成

```
                 ┌────────────── M5Stack Stack-chan K151（CoreS3: ESP32-S3, 8MB PSRAM）──────────────┐
  カメラ ───────▶│ loop()（単一所有者）                                                                  │
  タッチ ───────▶│  顔検出(pico) / 動き検出 ─▶ 追従制御（積分）─▶ サーボ（yaw）                           │
  マイク ───────▶│  portable core: Interaction（状態・鮮度・キャンセル） + Director（自律動作の方針）      │──▶ 画面（顔・字幕）
                 │  Web操作画面（LAN, トークン）                                                         │──▶ スピーカー
                 │        ▲ キュー（要求1・結果2）                                                       │
                 │  apiTask（別タスク）: HTTPS ──────────────────────────────────────────────────────────┼──▶ Sarashina API（TLS検証）
                 └──────────────────────────────────────────────────────────────────────────────────────┘
```

詳細は[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

### SWスタック

| 層 | 使っているもの |
|---|---|
| ハードウェア | M5Stack Stack-chan K151（CoreS3、ESP32-S3、フラッシュ16MB、PSRAM 8MB、GC0308カメラ、タッチ画面、スピーカー、マイク、SCS0009サーボ×2） |
| ビルド | arduino-cli 1.5.1、ESP32 Arduinoコア 3.3.11（FreeRTOS）、FQBN `esp32:esp32:m5stack_cores3` |
| 本体ライブラリ | M5StackChan 1.1.1（BSP、サーボ）、M5Unified 0.2.28、M5GFX 0.2.35、M5CoreS3 1.0.1（カメラ）、ArduinoJson 7.4.3、WebServer / ESPmDNS / HTTPClient / WiFiClientSecure（コア同梱） |
| 顔検出 | pico（Nenad Markus、MIT）を`firmware/libraries/PicoRnt`に同梱。正面顔のみ検出できる |
| ポータブルコア | `include/interaction.hpp`（要求ID・鮮度・状態・キャンセル）、`include/director.hpp`（挨拶・質問・相づちの方針）。C++17、ESP-IDF/Arduino/ハード非依存、ホストテスト済み |
| クラウド | Sarashina API（`sarashina3-nano`、HTTPS、Amazon Root CA 1を固定して検証）。任意でOpenAI TTS（`gpt-4o-mini-tts`）。キー未設定・残高なしでも文字だけで動く |
| 音声 | Open JTalk + HTS Voice "Mei"（CC BY 3.0）で事前に合成した固定フレーズ。表示は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |
| PC側の道具（開発・評価のみ） | Python（秘密情報・音声の生成、実機確認、シミュレーション）、g++（ホストテスト）。デモの動作には使わない |

## 動かす

前提: Windows/Linux/macOS、arduino-cli、ESP32コア 3.3.11、上の表のライブラリ、Python 3、K151（USB接続）。

```powershell
# 1. 秘密情報（git管理外）を環境変数から生成。キーをコミットしない。
#    SARASHINA_API_KEY, STACKCHAN_WIFI_SSID, STACKCHAN_WIFI_PASS（任意: OPENAI_API_KEY）
powershell -NoProfile -File tools/gen_secrets.ps1
# 2. 固定フレーズの音声を生成（任意。なければ無音で動く）
pip install pyopenjtalk-plus numpy
python tools/gen_voice.py
# 3. ビルドして書き込み（Windowsは長いパスを避けるため短いビルド先を使う）
arduino-cli compile --fqbn esp32:esp32:m5stack_cores3 --build-path C:\scb6 --libraries firmware\libraries `
  --build-property "compiler.cpp.extra_flags=-I<このリポジトリ>/include" firmware\answer_demo
arduino-cli upload -p COM3 --fqbn esp32:esp32:m5stack_cores3 --build-path C:\scb6 firmware\answer_demo
```

起動するとシリアル（115200）に`web http://<IP>/?t=<トークン>`が出る。同じLANのブラウザで開くと操作画面になる（`stackchan.local`も使える）。通信は平文のHTTPなので、信頼できるLANだけで使う。

操作: タップで反応、約0.9秒の長押しで中止・黙らせる。Webでは、質問、中止、自律・消音・読み上げ・マイク・カメラ配信の切り替え、追従方式（顔検出 / 動き / 顔→動き）の切り替えができる。

## 確認状況

**ホストテスト**（ポリシーの意味論だけ。実機のタイミング・AI・通信は証明しない）:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Iinclude tests/host.cpp -o /tmp/stackchan-host && /tmp/stackchan-host
```

**実機（K151、COM3、2026-10-27〜29）**。根拠はシリアルログとHANDOFF.mdの記録:

| 項目 | 状況 |
|---|---|
| 画面・タッチ・首（yaw ±15°） | 確認済み（目視） |
| カメラ・顔検出・首の向き | 動作を確認。検出率は低い（顔が前にある間に約4〜50%、条件を統制していない記録）。評価値ではない |
| Sarashinaへの直接HTTPS | 確認済み（HTTP 200、約0.7秒、証明書を固定して検証） |
| キャンセル、遅着応答の拒否 | 確認済み（`tools/web_check.py`） |
| 通信失敗（誤キー・到達不能・ハング）と回復 | 確認済み（`tools/fault_check.py`、故障注入） |
| 固定フレーズの声 | 聞こえたとユーザーが報告 |
| 自律の挨拶→質問→回答待ちの間の追従と相づち→字幕、ダッシュボード、マイクの「音」の記録 | 確認済み（ダッシュボードの画像とシリアルログ、2026-10-30）。評価値ではない |
| 質問の繰り返し | 読み上げをオフにして31回、パニック0、API 526〜806 ms。読み上げを有効にすると、約25回で2〜3回再起動した（原因未特定、既定をオフにした） |
| 視覚（「見て」: カメラ1枚をGeminiへ送り、見えているものに触れた一言を返す） | 実機で動作確認（5回、応答5〜14秒でばらつく、1回は時間切れ）。既定オフ、有効にしたときだけ画像がクラウドへ出る |
| 動き検出モード、状況文つきの自律質問 | 実装済み。実機での効果は未確認 |
| 回答の読み上げ（OpenAI TTS） | **未検証**（キーのクレジット残高なし） |
| 積分制御の実機での効果、Wi-Fi断からの回復 | 未検証 |

追従制御の比較は閉ループの**シミュレーションのみ**（[docs/sim](docs/sim/README.md)、`tools/track_sim.py`）。実機の性能ではない。

## ディレクトリ

| 場所 | 内容 |
|---|---|
| `include/` | ポータブルコア（`interaction.hpp`、`director.hpp`） |
| `firmware/answer_demo/` | 本体のメインのスケッチ。ほかは段階的な動作確認用（minimal_ui、minimal_servo、camera_preview、face_detect、core_synthetic） |
| `firmware/libraries/PicoRnt/` | 同梱した顔検出器（MITライセンス、出典つき） |
| `tools/` | 秘密情報・音声の生成、実機確認（web_check、fault_check）、シミュレーション、ホスト実験 |
| `tests/`、`HOST_LAB.md` | ホストテストと、合成イベントの再生器・模擬API |
| `docs/` | 構成の詳細、シミュレーション |
| `paper/` | 論文草稿（日本語・英語。評価前の作業草稿） |

## 研究と範囲

問い: MCUの資源内で、クラウド応答を待ちながら身体反応と回答への切り替えをどう両立するか。顔追従そのものの新規性は主張しない（公式にもUiFlow2の顔追従サンプルがある）。主張の対象は、待機中の並行動作と、遅着・取消の扱いを含む調停の構成。ユーザー実験なしで、自然さ・愛着・待ち時間の短縮は主張しない。新規性は引き続き検討中。

M5Stack公式の[StackChan Issue #70](https://github.com/m5stack/StackChan/issues/70)（会話中の視覚追従）は、公式のAIエージェントのファームに顔検出を足すとメモリ不足になると報告している。本プロジェクトは別のファームで、音声認識などは含まない。

範囲（2026-10-29に見直し）: 固定フレーズ音声、LAN操作画面、自律動作、マイクの音量反応は範囲内。ROS・シミュレータを使う場合は開発・評価の道具に限る。

## 再開するとき（開発者向け）

1. [HANDOFF.md](HANDOFF.md): 現在地、実機の記録、次の一手。
2. [AGENTS.md](AGENTS.md): Claude/Codex等で共通に守る開発ルール。
3. 論文草稿: [日本語版](paper/manuscript-ja.md) / [英語版](paper/manuscript.md)。

GitHubを正本にする。作業終了時にコミットしてpushし、別PCではfetch/pullしてから再開する。force pushしない。

## データとライセンス

APIキー、Wi-Fi情報、生画像、私的会話、機器ダンプ、講座PDFをコミットしない。MITライセンス（[LICENSE](LICENSE)）。第三者の成果物は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
