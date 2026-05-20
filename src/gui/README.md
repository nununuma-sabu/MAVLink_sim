# GUI Directory Layout

`src/gui` は Qt ウィジェットで構成される画面層です。
アプリ全体を組み立てる `MainWindow` と、責務別のサブディレクトリで構成します。

## Structure

```text
src/gui/
├── MainWindow.h/.cpp
├── panels/
├── widgets/
└── map3d/
```

## Responsibilities

- `MainWindow.h/.cpp`
  - 画面全体のレイアウトを構築します。
  - GUI 部品、シミュレータ、MAVLink、ミッション管理を接続します。

- `panels/`
  - 操作・状態表示用のパネル群です。
  - アーム/離着陸操作、テレメトリ表示、ミッション編集、MAVLink ログ表示を担当します。

- `widgets/`
  - 単体で再利用しやすい表示ウィジェットです。
  - 人工水平儀と 2D マップビューを配置しています。

- `map3d/`
  - 3D ビューと、その表示に必要な地理データ取得・読み込みを担当します。
  - OpenStreetMap / Overpass API 由来の建物、道路、線路、地面表現を扱います。
