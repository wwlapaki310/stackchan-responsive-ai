# 実機なしラボと帰宅後の接続手順

## 一括検証

Python 3.9以上（標準ライブラリのみ）とC++17コンパイラを用意する。

```sh
python tools/host_lab.py check
```

現在の検証: コア単体、13イベントの再生、HTTP模擬APIの成功・遅延・503・不正入力・未対応stream・不明エンドポイント。
2026-10-26のLinux/g++環境でPASS。実機・実AI・MCUのタイミング評価ではない。
デフォルトはg++。clang++を使う場合はCXX環境変数を設定。WindowsはWSL推奨、ネイティブのコンパイラ互換性は未検証。

## イベントの再生

```sh
python tools/host_lab.py replay
python tools/host_lab.py replay --output experiments/raw/demo.jsonl
python tools/host_lab.py replay --scenario scenario.txt
```

出力先の既存ファイルは上書きしない。デフォルトは標準出力へJSON Linesを出す。
全行にsource=host_policy_replay、clock_domain=synthetic_msを付ける。t_msは入力した仮想時刻であり、ホストやMCUの処理時間ではない。

### 入力形式

```text
start 0
face 100 80 0.6 1
touch 150
cancel 600
start 700
reply 800 1
reply 1000 2
```

第2列は処理時刻ms。faceの追加列は画像取得時刻ms、正規化した水平位置[-1,1]、顔の有無0/1。reply/failの追加列は要求ID。
startが要求IDを採番する。tickは時刻を進め、期限を確認する。finishは回答提示完了を表す。
未知のイベントや逆行時刻は終了コード2。CLIは研究用の合成入力であり、信頼できない外部入力の本番プロトコルには使わない。
acceptedはreply/fail/cancel/face/startの戻り値、その他は構文を受理したことを表す。実機への実行成功ではない。

組み込みのシナリオは、待機中の顔追従・タッチ、キャンセルと新規要求、旧要求の遅着拒否、現要求の受理、タイムアウトを通る。
このランナーは現在の提案ポリシーだけを再生する。A/B比較、統計解析、実デバイスのキュー待ち測定は未実装。

## 模擬API

```sh
python tools/host_lab.py serve
```

初期設定は127.0.0.1:8765。/healthとPOST /v1/chat/completionsを提供。
リクエスト例（任意のHTTPクライアントで送る）:

```json
{"model":"mock-only","messages":[{"role":"user","content":"合成質問"}],"mock_delay_ms":2000,"mock_status":200}
```

応答のchoices[0].message.contentはtext/expression/gestureを持つJSON文字列。実際のLLMを呼ばず、一定の合成回答を返す。
mock_delay_msは0〜20000の整数、mock_statusは200/429/500/503。stream=trueは明示的に400。実API全仕様を模倣していない。
リクエスト本文をログ・保存・外部転送しない。APIキー不要。TLSなし・認証なしのテスト専用サーバーなので実APIや本番サービスにしない。

実機から試す場合のみ、信頼できるLANで --host 0.0.0.0 を指定し、PCのLANアドレスとファイアウォールを確認する。
インターネットへ公開・ポート転送しない。模擬APIは実験用であり、本番デモは本体から証明書を検証してクラウドHTTPSへ接続する。

## ログの接続契約

ホストの合成ログと将来の実機ログを混ぜない。実機側はdevice_monotonic_msを使い、別途run_id、git SHA、SKU、SDK/BSP/モデル版、実験条件を添える。
以下の時点を区別する: 画像取得、認識完了、イベント受信、方針決定、サーボ指令、物理動作開始、API送信、初回token、回答検証完了、画面提示。
現在の再生ログは方針の状態変化のみ。物理動作や通信処理の遅延値を推定しない。
画像・質問の私的内容・資格情報はログへ含めない。

## 帰宅後: 実機ありPCの最初の60分の目安

1. 最新コードをpull。AGENTS.md、HANDOFF.mdを読む。まずhost_lab.py checkが通るか確認する。
2. K151のSKUと現行ファームを記録し、公式の復旧方法と書き込みツールを確認。バックアップ前に上書きしない。
3. SDK経路を選ぶ。公式native ESP-IDFファームを最小変更するか、Arduino向けBSPを使うかを決め、正確な版を記録。
4. 画面・タッチを最小例で確認。首は機械的限界とサーボ仕様を確認してから小さい可動量で試す。
5. 既存コアをデバイスの単一タスクへ接続。最初は合成faceイベントと模擬回答を使い、表示状態を確認する。
6. 次にカメラ画像取得、最後に顔検出を接続。画像取得時刻をobserveへ渡す。
7. API通信を別タスクへ接続。キーは各PCのローカル設定から供給し、コミットしない。

この手順は実機で実行していない。所要時間は目安であり、ビルド/復旧手順の成功を保証するものではない。
次のAIへは「HANDOFF.mdとHOST_LAB.mdを読み、実機側の最小ビルドから続けて」と伝える。
