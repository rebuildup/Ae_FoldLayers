AfterEffectsのプラグイン GM FoldLayersを再現したプラグインを作りたいです
現在はaexのみでMac版に対応していなかったり、そもそも公開がリンク切れしたりしています
AeSDKで作れるはずです
実際のファイルを解析して何か手がかりを探してください

解析結果と再実装の指針をまとめたレポートを作成しました。以下のリンクからご覧ください。

```
GM FoldLayers プラグイン解析と再現への手がかり
GM FoldLayers とは？
•	Photoshop に似たレイヤー管理 – GM FoldLayers は「After Effects のレイヤーを Photoshop のようにグループ分けできる」ことを売りにしたプラグインです。Good Motion が 2012 年頃にベータ版を公開しており、コンポジション内の複数レイヤーを “フォルダ” にまとめて折りたたむ機能を提供しました[1]。これによりプリコンポーズ（プリコンプ）を作らずにレイヤーを整理でき、グループに色ラベルを付けることもできました[1]。
•	公開中止と簡易版の登場 – オリジナル版は不具合が多く公開停止となりましたが、Good Motion の共同創業者 Petter Bergmar 氏がバグを減らした簡易版をリリースし話題になりました[2]。ただし Windows 版／Mac 版ともに配布ページがリンク切れとなっており、macOS （M1 対応）では動作しないという報告があります[3]。
•	他のツールとの比較 – GM FoldLayers の登場後、他社から類似ツール（Layer Groups など）やスクリプトが発売され、より高度なフォルダー機能やプリコンプ化機能を提供するようになりました[4]。既存ツールの機能を参考にしながら再実装を行うと良いでしょう。
.aex ファイルの解析結果
Windows 用プラグイン GM_FoldLayers.aex をバイナリ解析し、内部文字列やエクスポート関数などから機能の手がかりを得ました。主な発見は以下の通りです。
ファイル形式とエントリポイント
•	file コマンドの結果、プラグインは 64 bit の Windows DLL (PE32+) 形式であり、After Effects の標準プラグイン形式 .aex と一致していました。エクスポートテーブルを解析したところ、公開されているエクスポートは EntryPointFunc ひとつのみで、After Effects の「General Plugin」規約に沿っています。通常この関数を通じてプラグインが初期化・コマンド登録を行います。
埋め込まれていた文字列
バイナリ内の ASCII 文字列を抽出したところ、以下の有用な文字列が見つかりました（PE の内部文字列であるため正式な引用としては使用できませんが、手がかりとして紹介します）。
•	GM FoldLayers - This plugin makes it easy to divide composition layers into foldable groups. Copyright 2012 Goodmotion AB. – プラグイン自身の説明文。コンポジションレイヤーを「折りたたみ可能なグループ」に分ける機能を持つことを示しています。
•	Fold Group, Unfold Group, Fold/Unfold – メニューやボタンの名称と思われる文字列で、グループの折りたたみ／展開を切り替えるコマンドがあることが分かります。
•	app.project.activeItem.hideShyLayers = true; – ExtendScript コード断片。After Effects では各レイヤーに「Shy」フラグを設定するとタイムライン上で非表示にできます。このコードは「Shy フラグが付いたレイヤーを非表示にする」処理を示しており、プラグインが Shy フラグを利用してレイヤーの折りたたみを実現している 可能性が高いです。
•	AEGP Layer Suite, AEGP Layer Mask Suite, AEGP Text Layer Suite – After Effects SDK の AEGP スイート名が含まれており、プラグインがレイヤー操作用の API を利用していることを示します。
これらの情報から、GM FoldLayers は内部で以下のような処理を行っていると推測できます。
1.	グループの定義 – ユーザーが複数のレイヤーを選択すると、それらを 1 つの「グループ」として登録し、親レイヤー（Null オブジェクトや制御用レイヤー）を生成します。親には「Fold Group」といった名前が付けられ、ラベルカラーも変更されます。
2.	折りたたみ処理 – グループ内の子レイヤーに Shy フラグを付け、app.project.activeItem.hideShyLayers = true を実行することでタイムライン上から一時的に非表示にします。展開時には Shy フラグを外し、hideShyLayers = false を設定します。
3.	UI メニューの登録 – プラグインは General Plugin としてメニューに「Fold/Unfold」などのコマンドを追加し、選択中のグループを折りたたむ／展開する処理をトリガーします。
Mac 版非対応の理由
プラグインは Windows 用の 64 bit DLL しか含まれておらず、macOS 用の .plugin (Mach‑O) ファイルは含まれていません。また M1 チップ上では Rosetta 2 での互換性がないことが報告されており[3]、Mac 版を自作する場合は After Effects SDK を用いてネイティブの Mach‑O バイナリをビルドする必要があります。
AeSDK を用いた再実装の方針
以下は After Effects SDK（AEGP）で GM FoldLayers を再実装する際の基本設計です。SDK の API 名はバージョンによって異なる可能性があるため最新の SDK ドキュメントを確認してください。
1. General Plugin の作成
1.	EntryPointFunc の実装 – AeSDK では General Plugin のメイン関数 EntryPointFunc で各種イベントに対応します。この関数内で AEGP_RegisterMenuCommand を用いて「Create Fold Group」「Fold Unfold」「Delete Group」などのコマンドをメニューに登録します。
2.	プラグインデータの保持 – グループごとに子レイヤー ID を記録する必要があります。プラグイン読み込み時に AEGP_RegisterPersistentData や AEGP_SetLayerUserData を利用し、親レイヤーに子レイヤーのリストを紐付けます。あるいは Composition のメタデータとして保持することもできます。
3.	イベントハンドラ – メニュー選択時に呼び出されるコールバック関数を登録し、レイヤー操作を行います。
2. グループ作成ロジック
1.	ユーザーが複数レイヤーを選択した状態で「Create Fold Group」コマンドを実行すると、AEGP_GetCompFromLayer などの API を用いて現在アクティブなコンポジションと選択レイヤーリストを取得します。
2.	新しい Null レイヤーを作成 (AEGP_NewLayer または AEGP_LayerSuite の関数) し、名前を「Fold Group」などに設定します。ラベルカラーを変更する場合は AEGP_SetLayerLabel を利用します。
3.	選択レイヤーの順序を保存し、各レイヤーに Shy フラグを付けます (AEGP_SetLayerFlag で AEGP_LayerFlag_SHY を設定)。また、これらレイヤーを Null レイヤーの親に設定します (AEGP_SetLayerParent)。
4.	親レイヤーのユーザーデータまたは XMP メタデータに子レイヤーの ID を保存します。これにより折りたたみ処理時にどのレイヤーを対象とするか識別できます。
3. 折りたたみ／展開処理
1.	グループレイヤー（親）を選択して「Fold/Unfold」コマンドを実行すると、親レイヤーのユーザーデータから子レイヤーのリストを読み出し、各レイヤーの Shy フラグをトグルします。
2.	AEGP_GetCompShyFlag および AEGP_SetCompShyFlag を使用してコンポジションの hideShyLayers 設定を変更します。折りたたむ際は hideShyLayers = true、展開時は false に設定します。この挙動は前述のバイナリ内部文字列 app.project.activeItem.hideShyLayers = true; と一致します。
3.	展開時はグループレイヤーと子レイヤーのラベルカラーやソロ状態を復元するようにします。
4. グループ削除など追加機能
•	グループ解除 – 親レイヤーに紐づく子レイヤーをすべて選択し Shy フラグを解除、親レイヤーを削除することでグループ解除機能を実装できます。
•	カラーラベル管理 – AEGP_LayerLabelID 列挙体を用いてグループごとに異なる色を割り当てると視認性が向上します。
•	マルチプラットフォーム – Windows だけでなく macOS 用にもビルドするには、Xcode 用のプラグインプロジェクトを用意し、Mach‑O バイナリ（.plugin）を生成します。AeSDK 付属のサンプルプロジェクト（/Examples/Project_FolderPlugIn 等）を参考にすると良いでしょう。
5. テストとデバッグのポイント
•	After Effects はバージョンにより API の挙動が変わるため、CS5〜最新バージョンまで検証が必要です。特にマルチフレームレンダリングやプロパティキャッシュに関する制約に注意してください。
•	プラグインはレイヤーの削除や再配置によって内部の子レイヤーリストが無効になりやすいので、レイヤー ID が変化した場合の追跡処理やエラーハンドリングを実装します。
•	セッション中に誤って Shy フラグが残らないよう、展開時に hideShyLayers の初期状態を記憶して復元するなど、ユーザー操作の邪魔にならない工夫が必要です。
まとめと次のステップ
GM FoldLayers はコンポジション内でレイヤーを折りたたみ可能なグループに整理するシンプルなプラグインであり、プリコンポーズを使用せずタイムラインを整頓できる点が評価されました。公開されていた .aex を解析した結果、Shy フラグの活用と AEGP スイートによるレイヤー操作 が主な仕組みであることがわかりました。また、Fold Group や Fold/Unfold といったメニュー名や説明文が含まれており、プラグインの UI と操作フローを推測できました。
After Effects SDK を用いた再実装は可能であり、以下のステップが推奨されます。
1.	General Plugin テンプレートのセットアップ – SDK のサンプルを基に EntryPointFunc を実装し、コマンド登録処理を整える。
2.	グループ管理ロジックの実装 – 選択レイヤーを Null レイヤーにまとめ、Shy フラグとコンポジションの hideShyLayers 設定で表示/非表示を切り替える。
3.	メタデータ保持とエラーチェック – 親レイヤーにユーザーデータを保存し、レイヤー変更後も安全に追跡できるようにする。
4.	マルチプラットフォーム対応 – macOS 用にもビルドし、M1 環境での動作を検証する。
以上の手がかりを参考に、独自の GM FoldLayers クローンプラグインを開発してみてください。現行の After Effects にはサードパーティ製スクリプトやプラグイン（Layer Groups、Workflower など）が存在し、機能や UI の参考になりますが、自作することで自分のワークフローに合わせたカスタマイズが可能です。[1][2]
________________________________________
[1] Goodmotion releases GM FoldLayers for After Effects | CG Channel
https://www.cgchannel.com/2012/08/goodmotion-releases-gm-foldlayers-for-after-effects/
[2] [4] After Effects GM FoldLayers Resurfaces After Hiatus - Lesterbanks
https://lesterbanks.com/2015/03/after-effects-gm-foldlayers-resurfaces-after-hiatus/
[3] Rearrange AeFolder 1.1 (please test GM_FoldLayers before buiyng it)
https://mysteropodes.gumroad.com/l/RARFOLDER

```

このレポートでは、GM FoldLayers プラグインの目的や歴史、バイナリ解析から得られた文字列、Shy フラグを使った折りたたみ処理の推測、AeSDK を利用した再現方法などを詳しく解説しています。マルチプラットフォーム対応やテストのポイントも含めていますので、プラグイン開発の参考にしていただければと思います。



AeSDKを渡すのでこれを使って、もっと具体的な開発方法を教えて下さい(どのテンプレートを書き換えて始めるなど、)

あと、AeSDKのドキュメントから参考になる部分のページを探してURLをして提示してください

After Effects SDK に含まれている「Skeleton」テンプレートや AEGP サンプルを利用すると、プラグインの骨組みやビルド手順がすべて揃っているので GM FoldLayers のような機能を比較的短時間で再実装できます。以下では、提供いただいた SDK とドキュメントを基にした開発の具体的な進め方を紹介します。

## 1. SDK の準備とテンプレート選択

1. **SDKを展開** – `AfterEffectsSDK_25.6_61_win.zip` を解凍すると `ae25.6_61.64bit.AfterEffectsSDK` ディレクトリがあり、その中の `Examples/Template/Skeleton` がエフェクト用の雛形、 `Examples/AEGP` フォルダに Projector・Easy_Cheese・IO・Persisto などのサンプル AEGP が含まれています。Windows 用プロジェクト (*.sln, *.vcxproj) と macOS 用 Xcode プロジェクトが同梱されており、環境に合わせて開いてください。
2. **エフェクトか AEGP かを決める** – GM FoldLayers はタイムライン操作（レイヤーの Shy フラグの ON/OFF や「Shy を隠す」設定のトグル）を行うツールなので、画像処理エフェクトではなく **AEGP** プラグインとして実装するのが適しています。AEGP は After Effects の UI に独自メニューを追加し、現在のコンポジションや選択レイヤーを操作するための API を提供します。

## 2. テンプレートのカスタマイズ方法

### Skeleton テンプレートの利用 (エフェクト用)

ドキュメントでは、効果プラグインを新規作成する場合は Skeleton テンプレートから始めることが推奨されています。サンプルをそのままコピーして名称を置き換えることで、ビルド済みのプロジェクトが手に入るという説明があります。以下の手順でカスタマイズできます。

1. `Examples/Template/Skeleton` フォルダを丸ごとコピーし、例えば `FoldLayers` にリネームします。
2. テキストエディタで新フォルダ内を検索し、「Skeleton」「SKELETON」という文字列を新しいプラグイン名 (例: `FoldLayers` / `FOLDLAYERS`) に置き換えます。ここにはソースファイル名・プロジェクト名・PiPL リソース定義が含まれます。
3. ソースコードでは、`ENTRYPOINT_PROC` や `PLUGIN_NAME` などの識別子を自分のものに変更します。Skeleton には 8bit/16bit 対応や AEGP_SuiteHandler の利用方法などが既に組み込まれているため、これを土台に処理を書き換えます。
4. Windows では Visual Studio を、macOS では Xcode を使い、プロジェクトファイルを開いてビルドします。After Effects の「Plug-ins」フォルダに生成した `.aex`/`.plugin` ファイルをコピーし、After Effects を再起動すると読み込まれます。

### AEGP プラグイン用サンプルの利用

GM FoldLayers のように After Effects のメニューにコマンドを追加してレイヤーを操作する場合は Skeleton ではなく AEGP 用のサンプルをベースにします。公式ドキュメントの「How to start creating plug‑ins」では、AEGP 開発者は **Projector** (プロジェクト操作)、**Easy Cheese** (キーフレーム補助)、**IO** (メディア読み書き)、**Persisto** (シンプルなメニュー追加と環境設定) などのサンプルから始めるのが良いと説明されています。Persisto にはメニューコマンドを登録し、選択中の項目に対して処理を行う基本構造が含まれているため、これをベースにするのが適しています。

1. `Examples/AEGP/Persisto` をコピーして新しいフォルダ名に変更します。
2. ソースコード中のプラグイン名・コマンド名を変更し、メニュー登録コードを自分の用途に置き換えます。
3. 主要な変更点は、 **命令ハンドラ (command hook)** 内で実際のレイヤー操作を行う部分と、**メニュー更新ハンドラ** でメニュー項目の有効/無効を制御する部分です。

## 3. メニューコマンドを追加する方法

AEGP ではメニュー項目を追加し、選択時にコマンドハンドラが呼び出されます。ドキュメントの実装ページには、「AEGP_GetUniqueCommand() で After Effects からコマンド ID を取得し、AEGP_InsertMenuCommand() でメニューに挿入する。AEGP_RegisterCommandHook() でコマンドハンドラを登録し、AEGP_RegisterUpdateMenuHook() でメニューの有効/無効を更新するフックを登録する」という手順が示されています。また、複数のメニューを追加してもハンドラは 1 つにまとめ、引数の `command` に応じて処理を切り替えます。

具体例:

```cpp
// エントリポイント内
AEGP_Command shyToggleCmd;
AEGP_Command showAllShyCmd;

AEGP_RegisterSuite5()->AEGP_GetUniqueCommand(&shyToggleCmd);
AEGP_RegisterSuite5()->AEGP_GetUniqueCommand(&showAllShyCmd);

// 「Layer」メニューの下の「GM Tools」グループにコマンドを追加
AEGP_CommandSuite1()->AEGP_InsertMenuCommand(
    shyToggleCmd, "Toggle Shy on Selected Layers",
    AEGP_Menu_LAYER, AEGP_MENU_INSERT_AT_BOTTOM);
AEGP_CommandSuite1()->AEGP_InsertMenuCommand(
    showAllShyCmd, "Show/Hide Shy Layers",
    AEGP_Menu_LAYER, AEGP_MENU_INSERT_AT_BOTTOM);

// コマンドハンドラとメニュー更新ハンドラを登録
AEGP_RegisterSuite5()->AEGP_RegisterCommandHook(
    plugin_id, AEGP_HP_BeforeAE, shyToggleCmd, CommandHook, nullptr);
AEGP_RegisterSuite5()->AEGP_RegisterCommandHook(
    plugin_id, AEGP_HP_BeforeAE, showAllShyCmd, CommandHook, nullptr);
AEGP_RegisterSuite5()->AEGP_RegisterUpdateMenuHook(
    plugin_id, UpdateMenuHook, nullptr);
```

## 4. レイヤーの shy フラグを操作する

AEGP_LayerSuite はコンポジション内のレイヤー情報の取得やフラグ操作を提供します。ドキュメントでは `AEGP_GetLayerFlags()` でレイヤーのフラグを取得し、`AEGP_SetLayerFlag()` で個別のフラグをオン／オフできることが説明されています。shy フラグは `AEGP_LayerFlag_SHY` で表されます。

レイヤーを畳む処理の例:

```cpp
// 選択レイヤー集合を取得 (AEGP_CollectionSuite2 を使用)
// For simplicity, this pseudocode uses one active layer:
AEGP_LayerH layerH;
AEGP_LayerFlags flags;

// 今の shy 状態を取得
AEGP_LayerSuite9()->AEGP_GetLayerFlags(layerH, &flags);

// フラグを反転 (true で ON, false で OFF)
A_Boolean setShy = !(flags & AEGP_LayerFlag_SHY);

// Undo グループ開始
AEGP_UtilitySuite6()->AEGP_BeginUndoGroup("Toggle Shy Flags");

// shy を変更
AEGP_LayerSuite9()->AEGP_SetLayerFlag(layerH, AEGP_LayerFlag_SHY, setShy);

// Undo グループ終了
AEGP_UtilitySuite6()->AEGP_EndUndoGroup();
```

コンポジションの「Shy を隠す」スイッチをトグルしたい場合は、AEGP_CompSuite の `AEGP_GetCompFlags()` と `AEGP_SetCompFlags()` (ドキュメントでは `AEGP_CompFlag_SHOW_ALL_SHY` が定義されている) を使って `AEGP_CompFlag_SHOW_ALL_SHY` ビットを反転します。

## 5. 参考になる SDK ドキュメントページ

プラグイン開発の具体的なポイントを調べる際に役立つページをリストアップします。

| 内容                             | ページ名 & 説明                                                                                                            | URL                                                                                                                                                                          |
| ------------------------------ | -------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Skeleton テンプレートの利用方法**       | 「How To Start Creating Plug‑ins」ページの中で、Skeleton テンプレートをコピーして名前を置き換えることで効果プラグインの雛形を作成できることが説明されています。                  | [https://ae-plugins.docsforadobe.dev/intro/how-to-start-creating-plug-ins/#hack](https://ae-plugins.docsforadobe.dev/intro/how-to-start-creating-plug-ins/#hack)             |
| **AEGP 開発の入口**                 | 同ページの末尾では、AEGP 開発者は Projector・Easy Cheese・IO・Persisto などのサンプルから始めるべきであると助言しています。                                     | [https://ae-plugins.docsforadobe.dev/intro/how-to-start-creating-plug-ins/](https://ae-plugins.docsforadobe.dev/intro/how-to-start-creating-plug-ins/)                       |
| **AEGP のエントリポイントとフック登録**       | AEGP 実装ページではエントリポイントの関数プロトタイプやメニュー追加に必要なコマンド／更新ハンドラ登録の流れが解説されています。                                                   | [https://ae-plugins.docsforadobe.dev/aegps/implementation/#example-adding-a-menu-item](https://ae-plugins.docsforadobe.dev/aegps/implementation/#example-adding-a-menu-item) |
| **AEGP_CommandSuite1**         | CommandSuite のセクションでは `AEGP_GetUniqueCommand`, `AEGP_InsertMenuCommand` などメニュー追加 API の詳細が説明されています。                   | [https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#aegp_commandsuite1](https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#aegp_commandsuite1)                       |
| **AEGP_LayerSuite9**           | LayerSuite のセクションには `AEGP_GetLayerFlags`, `AEGP_SetLayerFlag` などレイヤーフラグ操作関数が一覧化されており、shy や collapse フラグの扱いも記載されています。 | [https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#aegp_layersuite9](https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#aegp_layersuite9)                           |
| **AEGP_CompFlag_SHOW_ALL_SHY** | Composition のフラグとして `AEGP_CompFlag_SHOW_ALL_SHY` が定義されており、コンポジションの「Shy を隠す」状態を取得・変更する方法が示されています。                     | [https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#manage-compositions](https://ae-plugins.docsforadobe.dev/aegps/aegp-suites/#manage-compositions)                     |

## 6. まとめと次のステップ

* Skeleton や Persisto などのサンプルを出発点に、メニューの登録処理や AEGP_LayerSuite の使い方を理解すると効率的に開発できます。
* コマンドハンドラ内でレイヤーの Shy フラグを取得・反転し、必要に応じてコンポジションの `AEGP_CompFlag_SHOW_ALL_SHY` を切り替えることで GM FoldLayers の機能を再現できます。
* プラグインは After Effects のメジャーアップデートごとに API が微妙に変わることがあるため、`AEGP_GetUniqueCommand()` で取得したコマンド ID を保存しない、`AEGP_CompFlag_SHOW_ALL_SHY` 等の定義があるバージョンを確認するなど、API バージョン分岐に注意してください。

以上の方法を参考に、提供された SDK を使って GM FoldLayers のようなレイヤー折りたたみツールをクロスプラットフォーム対応で構築してみてください。
