# MAVLink ドローンシミュレーター TODO

## 方針（決定済み）

- [x] **MAVLinkライブラリ**: 公式ヘッダーオンリーCライブラリ（`c_library_v2`）
- [x] **シミュレーション**: 位置ベース（指令に応じて座標を直接移動、シンプル実装）
- [x] **GCS連携**: QGroundControlとUDP接続してテストする
- [x] **追加機能**: 将来的には全実装（ミッション計画、複数ドローン、センサー、3D）
- [x] **開発フェーズ**: 4段階で進行

---

## 開発タスク

### Phase 1: 基盤構築 ✅
- [x] プロジェクト構造の作成とCMakeセットアップ
- [x] MAVLinkヘッダーの取得と組み込み
- [x] `DroneState` データ構造の定義
- [x] `DroneSimulator` 位置ベースシミュレーションの実装
- [x] `FlightController` 基本モードの実装

### Phase 2: MAVLink通信 ✅
- [x] `MavlinkUdpLink` UDP通信レイヤーの実装
- [x] `MavlinkManager` メッセージ送受信の実装
- [x] `MessageHandler` コマンド処理の実装
- [x] QGroundControlとの接続テスト対応（Heartbeat交換）

### Phase 3: GUI ✅
- [x] `MainWindow` レイアウト構築
- [x] `AttitudeIndicator` 姿勢表示の実装
- [x] `TelemetryPanel` テレメトリ表示の実装
- [x] `MapView` 2Dマップの実装
- [x] `MapView3D` 3Dビューの実装（OpenGL、タブ切替）
- [x] `ControlPanel` 操作パネルの実装

### Phase 4: 統合・テスト ✅
- [x] 全コンポーネントの統合
- [x] ビルド成功・起動確認
- [x] QGroundControlとの通信テスト対応
  - [x] UDPポート衝突バグ修正 (14540/14550分離)
  - [x] MISSION_REQUEST_LIST 応答送信の修正
  - [x] PARAM_REQUEST_LIST 応答の実装
  - [x] AUTOPILOT_VERSION 応答の実装
  - [x] GCS Heartbeat検出 & UI状態表示
  - [x] LogPanel（MAVLink通信ログ）の追加
- [ ] バグ修正とUI調整（継続的に対応）

---

## 将来の追加機能
- [ ] ミッション計画（ウェイポイント自動飛行）
- [ ] 複数ドローン対応
- [ ] センサーシミュレーション（カメラ等）
- [ ] 3Dビューの強化（地形、建物等）
