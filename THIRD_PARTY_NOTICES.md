# 第三者のソフトウェア・データ

このリポジトリ自体はMITライセンス（`LICENSE`）。以下は、リポジトリに含む、または生成物に含まれる第三者の成果物。

| 名前 | 用途 | ライセンス | 場所 |
|---|---|---|---|
| pico (PicoRnt) / Nenad Markus | 顔検出（本体上） | MIT | `firmware/libraries/PicoRnt/`（LICENSE、SOURCE.txt） |
| Open JTalk | 固定フレーズ音声の合成（PC上、生成時のみ） | Modified BSD | 生成物に含まれる |
| NAIST Japanese Dictionary | 同上の辞書 | BSD（Nara Institute of Science and Technology） | 生成物に含まれる |
| HTS Voice "Mei" | 同上の声 | CC BY 3.0 | 生成物に含まれる |
| pyopenjtalk-plus | 生成スクリプトが使うPythonバインディング | MIT | 依存（リポジトリには含まない） |
| Amazon Root CA 1 | Sarashina APIのTLS検証（公開の証明書） | 公開証明書 | `firmware/certs/` |

## 音声の表示（CC BY 3.0）
本体の固定フレーズ音声は、Open JTalkとHTS Voice "Mei"で合成した。
HTS Voice "Mei": Copyright (c) 2009-2013 Nagoya Institute of Technology, released by the MMDAgent Project Team, licensed under CC BY 3.0 (<http://www.mmdagent.jp/>)。音声を変換・縮小（16 kHz、トリミング、音量調整）している。
音声の出力ファイル `voice_clips.h` は大きいためgit管理外。`tools/gen_voice.py`で再生成できる。

## 依存（リポジトリに含まない。利用者がインストールする）
M5StackChan、M5Unified、M5GFX、M5CoreS3、IRremoteESP8266、M5Unit-NFC、ArduinoJson、ESP32 Arduinoコア。各ライブラリのライセンスに従う。
