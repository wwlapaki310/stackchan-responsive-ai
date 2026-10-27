# 2台のPCでの開発・引き継ぎ

更新日: 2026-10-26 / フェーズ: 実機なしの設計・ホスト検証

## 現在地

- 実装済み: OS非依存のC++17ポリシー。要求ID、キャンセル、遅着・重複応答拒否、タイムアウト、顔観測の鮮度、タッチ表情と首追従の独立制御。
- 検証対象: tests/host.cpp。ホスト検証のみ。2026-10-26にg++の警告をエラー扱いする設定でビルド・実行しPASS。
- 未実装: ESP-IDFアダプタ、カメラ・顔検出、サーボ、画面、API通信、物理動作の計測、比較ベースライン。
- 論文: paper/manuscript.mdに英語作業草稿、paper/manuscript-ja.mdに同じ節構成・文献番号の日本語作業草稿。背景・関連研究・設計・実験計画を執筆。実装説明は提案であり実機実績ではない。
- 実装済み追加: tools/replay.cppの合成イベント再生、tools/host_lab.pyの一括検証・JSONL出力・遅延/失敗注入HTTP模擬API。
- 検証: python tools/host_lab.py checkをLinux/g++で実行しPASS（コア単体、13イベント、ループバックHTTP成功/失敗/不正入力）。
- APIキー不要で現在のホスト検証を実行可能。HOST_LAB.mdに利用方法と帰宅後の手順。
- 現在のGitHub公開設定はPrivate。最終課題の提出前に公開条件を確認する。

## 実機ブリングアップ記録（2026-10-27、Windows PC、COM3）

- ボード: ESP32-S3 (QFN56) rev v0.2、16MB quad flash、3.3V。USB-Serial/JTAG(VID 303A PID 1001)。esptool v5.3.1で確認。SKUの最終確認（K151か）は筐体ラベルで未確認。
- 工場ファームのバックアップ: esptoolのread-flashが約70KBで「Packet content transfer stopped」となり失敗（921600/460800/115200で同じ）。ユーザーが別途バックアップ保有と申告したため打ち切り。ダンプは未取得で、ここに証跡はない。
- 経路: Arduino（arduino-cli 1.5.1、esp32コア3.3.11、FQBN esp32:esp32:m5stack_cores3）。ESP-IDFは未導入。
- ライブラリ版: M5StackChan 1.1.1（ライブラリ登録経由）、M5Unified 0.2.28、M5GFX 0.2.35、IRremoteESP8266 2.9.0、M5Unit-NFC 0.1.2。
- ビルド: Windowsの長いパスで失敗するため`--build-path C:\scb`のような短いパスを使う。初回はM5Unifiedのビルドで10分超。
- firmware/minimal_ui: 画面とタッチ。ビルド470,519B(14%)、書き込みはハッシュ検証済み。ユーザーが画面表示とタッチ座標・カウント更新を目視で確認。シリアルログは取得できず（起動直後を取り逃し）。
- firmware/minimal_servo: ビルド524,343B(16%)、RAM28,468B(8%)、書き込みはハッシュ検証済み。上部タッチでのみ、速度200・yaw±15°を掃引する。ユーザーが目視でyaw±15°の動作を確認（2026-10-27）。pitchは未確認、動作ログ・動画なし。
- firmware/core_synthetic: interaction.hppをloop()の単一タスクに接続。入力は合成のみ（カメラ・通信なし）。ビルド525,663B(16%)、RAM28,588B(8%)、書き込みはハッシュ検証済み。画面の左/中/右タップ=質問開始/キャンセル/タッチ反応、最初のタップで合成顔(正弦波)と首追従(±15°)を有効化。ビルドには`--build-property "compiler.cpp.extra_flags=-I<repo>/include"`が必要。**動作は未確認。**
- firmware/camera_preview: M5CoreS3 1.0.1の`CoreS3.Camera`(esp32-camera)でフレーム取得し画面表示。ビルド591,027B(18%)、RAM39,788B(12%)、書き込みはハッシュ検証済み。シリアルログ(DTR有効で取得): 初期化OK、320x240 RGB565(len=153600)、画面表示込みで約17〜20fps、空きヒープ約279KB、PSRAM約8.07MB。画面のプレビューは未目視確認。顔検出なし。首は動かさない。1回のログのみで、長時間・条件別の計測ではない。
- 注意: シリアルは`SerialPort`で`DtrEnable=true`にしないと出力が出ない。ログの"first frame"行は5秒窓ごとに再表示される軽微なバグ(frames==0判定)。M5CoreS3の依存の一部(M5-FPC1020A)がネットワーク断で入らなかったがビルドは通った。
- 顔検出: Arduinoコア3.3.11の同梱ライブラリにesp-dlなし（esp-dlはESP-IDF向け）。pico（MIT、nenadmarkus/pico、コミット7d550c78b2c31a4e1dfc5bcdfe9da013297b5cc8）のrnt/picornt.cと学習済みfacefinderカスケードをfirmware/libraries/PicoRnt/に取り込み（LICENSE、SOURCE.txt同梱）。カスケードの学習データの由来と、picoの顔検出としての精度はここでは未検証。
- firmware/face_detect: 320x240 RGB565を160x120グレーへ2倍間引きしpicoで検出。首は動かさない。ビルド831,855B(26%)、RAM59,004B(18%)、書き込みはハッシュ検証済み。ビルドに`--libraries firmware\libraries`が必要。シリアル(5秒窓、4窓): 約17fps、検出処理の平均約12.9〜13.5ms/フレーム、最大約14.4〜18.5ms、顔検出0/85〜87フレーム。**検出の成否は未確認。**
  - 切り分け(2026-10-27): 検出器に渡すグレー画像をシリアルでPCへ取り出しPNG化して目視した。ネイティブuint16読みでは画像が崩れ、バイト入れ替え読みで正しく見えた。**カメラのRGB565はバイト入れ替えが必要**（face_detect.inoのtoGray()に反映済み）。修正前の0/85〜87は壊れた画像による結果。
  - 修正後も0/79〜82フレーム。ただし取り出した画像の顔は横向きでうつむいており、picoは正面顔向け。正面を向いた状態の検出は未確認。
  - ユーザーの目視報告(2026-10-27): 修正後、画面に緑枠が「たまに」出た。検出率・誤検出・距離や明るさ条件は未計測。
  - 修正後の速度: 約15.6〜16.4fps、検出平均約15.8〜16.8ms/フレーム。
  - 検出時のx符号（カメラの左右とyawの向き）は未校正。
  - 取り出したPNGは人物の顔が写るためコミットしない（スクラッチパッドのみ）。
- クラウドAPI（公式 https://docs.sarashina.ai/ で確認、2026-10-27）: `POST https://api.sarashina.ai/v1/chat/completions`、`Authorization: Bearer`、環境変数名`SARASHINA_API_KEY`、モデルsarashina3-mini/nano/guard。許可パラメータ外は400。`max_completion_tokens`は1〜8000。RPM: mini 6、nano 60。429は`Retry-After`。PCからの合成質問1件でnano、HTTP200、約1.45秒（PC→クラウドの値で、本体の値ではない）。サービスの利用条件・データ取り扱いは未確認。無償ベータとする二次情報あり。
- TLS: ルートCAはAmazon Root CA 1（SHA-1 8DA7F965EC5EFC37910F1C6E59FDC1CC6A6EDE16、amazontrust.comから取得し指紋照合）。チェーン: api.sarashina.ai → Amazon RSA 2048 M04 → Amazon Root CA 1（2026-10-27観測、リーフ期限2027-04-10）。firmware/answer_demo/root_ca.hに固定。ルートの更新・ローテーションで失効しうる。
- 秘密情報: `tools/gen_secrets.ps1`が環境変数SARASHINA_API_KEY、STACKCHAN_WIFI_SSID、STACKCHAN_WIFI_PASSからgit管理外のfirmware/answer_demo/secrets.hを生成。コミット前に`git grep hki_v1`で漏えいなしを確認済み。キーは会話に貼られたため提出後に再発行する。
- firmware/answer_demo: 質問選択（下部3ボタン）→HTTPSでAPI要求（別タスク）→顔追従・タッチ表情が継続→検証して表示。中止は右上。ビルドには`--libraries firmware\libraries`と`-I<repo>/include`が必要。ビルド1,288,843B(40%)、RAM66,972B(20%)、書き込みはハッシュ検証済み。Wi-Fi設定後の再ビルドは1,691,287B(53%)、RAM83,332B(25%)。
  - オフライン時（2026-10-27）: wifi=0 key_set=1、カメラOK、約12.6fps、空きヒープ約234KB。
  - Wi-Fi接続後（2026-10-27、シリアルログ）: wifi=1（NTP同期後）。ユーザー操作とみられる質問開始（start、質問番号1）から、本体測定でHTTPS要求→HTTP 200、605ms、reply_accepted（回答78バイト）まで一巡を確認。本体→クラウドは証明書検証付きHTTPS（Amazon Root CA 1固定）。この1件のみで、反復・失敗率・長時間は未計測。空きヒープは通信時約157KB。画面に回答が表示されたかの目視は未確認。
  - 未確認: キャンセルと遅着応答の拒否、通信失敗・タイムアウトからの回復、再起動後の再現、首追従の向き(kYawSign)、待機中の顔追従とタッチ表情の同時動作の目視。
- サーボ（公式資料）: SCS0009×2、UART1 GPIO6/7、1Mbps半二重。電源はPY32 IOエキスパンダ経由。BSP APIの角度は10=1°、yaw -1280〜1280、pitch 0〜900。M5はpitchを5〜85°に収めるよう勧める。

## PCの分担

| PC | 主な役割 | 実施してよい確認 |
|---|---|---|
| 実機なしPC | コア処理、テスト、ログ解析、論文、模擬API | ホストの意味論、ビルド、入力リプレイ |
| 実機ありPC | 書き込み、BSP、認識、首・画面、API統合 | K151上のメモリ・処理時間・動作・復旧 |

同じコミットのコアを両PCで使う。ハード未接続時は書き込みを試みない。

## 各PCの初回準備

```sh
git clone https://github.com/wwlapaki310/stackchan-responsive-ai.git
cd stackchan-responsive-ai
git status --short --branch
```

非公開リポジトリのため、各PCで通常のGitHub認証を設定する。認証情報はリポジトリに置かない。
ホスト要件: C++17対応コンパイラ。Linux/WSLではg++、macOSではclang++を使用できる。Windowsネイティブでは使用コンパイラと実行パスを記録する。

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Iinclude tests/host.cpp -o /tmp/stackchan-host
/tmp/stackchan-host
```

このコマンドはESP-IDFや実機を必要としない。実行結果はMCU性能データではない。

## 再開するとき

1. `git status --short --branch`で未コミット変更とブランチを確認。
2. 未コミット変更があれば勝手に消さない。前回の変更をコミットしてから続ける。
3. `git fetch origin`。
4. 引き継ぎ対象ブランチへ移動し、`git pull --ff-only`。
5. AGENTS.md、HANDOFF.md、最新コミットを読む。
6. ホストテストを実行してから変更する。

## 作業を終えるとき

1. 下の「次の一手」と実施した検証・未解決事項を更新。
2. 秘密情報・実験の生データ・機器固有パスが入っていないか差分を確認。
3. 変更をコミットし、`git push -u origin <作業ブランチ>`。
4. `git status --short --branch`が意図した状態であることを確認。
5. 別PCへブランチ名とコミットSHAを渡す。ローカルstashは引き継ぎにならない。

基本は1本の作業ブランチを交互に使う。別々の作業を並行する場合のみ分岐し、PRで統合する。
mainが進んでいたら差分を確認して通常のmergeで取り込む。force push、reset --hard、cleanによる回復は行わない。

## 実機ありPCで必ず残す記録

- ボードSKU/リビジョン、工場ファーム復旧方法、シリアルポート（機器固有設定はローカル）。
- ESP-IDF/Arduino/BSP/コンパイラ/推論モデルの正確なバージョン・コミット。
- ビルド・書き込み・監視コマンド、フラッシュ設定、使用メモリ、可動範囲。
- 確認した動作、失敗した動作、再現手順、サニタイズしたログの所在。
- 実験ログにrun_id、git SHA、設定、モデルID、実施日時を対応づける。

## 技術判断

- 公式ファームはESP-IDF、StackChan-BSPはArduino向け。BSPをそのままnative IDFに置けると仮定しない。
- 最初にnative IDFの既存実装利用かArduino-as-componentかを比較し、最小の再現可能な方法を選ぶ。
- coreは単一タスク所有。複数のタスクはキュー経由でイベントを渡し、直接同時呼び出ししない。
- 時刻は起動内単調増加ms。顔検出の時刻は推論終了ではなく画像取得時刻。
- coreは正規化した顔位置を返すだけ。サーボ角度への変換・速度制限・実際の中断はハードアダプタが担う。
- 回答テキストの検証・サイズ制限はAPIアダプタ側。replyがtrueを返した場合のみ表示へ渡す。
- 推論頻度の資源適応、動作軌道、答え表示時間は未実装。現在のcoreを完成した提案方式と呼ばない。

## 次の一手

実機なしPC: 次は通常並行方式と提案方式の比較アダプタを設計。現在のランナーは提案コアの意味論だけを確認し、A/B比較やタイミング計測は未実装。
実機ありPC: 画面・タッチは確認済み。次は首（minimal_servoの上部タッチ掃引を目視確認）-> pitch -> コアを単一タスクへ接続 -> カメラ。工場ファームの復旧方法は未確認のまま。
論文: MCU系先行研究の本文確認、CUI/MIRAとの差分表の精査。結果を先取りしない。

## 引き継ぎ記録

2026-10-26: 初期のポータブルコアとホストテストを追加。実機・APIアクセスなし。英語論文草稿を開始。

2026-10-26追記: ホストラボを追加。合成時刻と物理時間を明確に分離。実機ありPCはHOST_LAB.mdの接続手順から再開する。

2026-10-26追記: 日本語論文草稿を追加。結果と結論は実測後に執筆。日本語版には現在のホストラボの状況も記載しているため、投稿前に英語版との状態記述を統一する。
