# BonDriver_SRT

SRTで送られたMPEG-TSをBonDriver対応アプリケーションへ渡すWindows用受信DLLです。SRTプロトコルのプロジェクトで証明書を用意せずに利用できます。

受信側はSRT listenerとして動作し、送信側からのcaller接続を受け付けます。パスフレーズを設定すると暗号化されます。

## ビルドに必要なもの

- Windows 10以降の64bit開発環境
- PowerShell 5.1または7
- MSVC x86/x64コンパイラとWindows SDK（C++ Build Tools 2019以降）
- CMake 3.20以降とNinja
- 初回の依存ソース取得に使うインターネット接続

Visual StudioのIDE、ソリューションファイル、MSBuildは使用しません。コマンドライン用の[Microsoft C++ Build Tools](https://learn.microsoft.com/cpp/build/building-on-the-command-line)で構築できます。Build Toolsをインストールするときは、C++開発用ワークロードとWindows SDKを選択してください。CMakeとNinjaはC++ CMake toolsコンポーネントのものを優先し、なければPATH上から検出します。単体で導入する場合はWindowsネイティブ版を使用してください。

BonDriverホストとのC++バイナリ互換性を保つため、コンパイラはMSVCを使用します。MinGW用のビルドは提供していません。

## PowerShellからビルドする

このディレクトリで実行します。Developer PowerShellの事前起動は不要です。

```powershell
# 64bit・Release
.\build.ps1

# 32bitホスト向け
.\build.ps1 -Architecture x86

# テストを実行してから配布用ファイルを生成
.\build.ps1 -Test

# Debug、再ビルド、並列数の指定
.\build.ps1 -Configuration Debug -Rebuild -Jobs 4
```

生成先は以下のとおりです。

| 内容 | 64bit Releaseの場所 |
| --- | --- |
| 作業用ビルド | `build/x64/Release/` |
| 配置・配布するファイル | `dist/x64/Release/` |
| 依存ライブラリのソースと取得済みアーカイブ | `.deps/` |

32bitでは`x64`が`x86`に、Debugでは`Release`が`Debug`に変わります。配布用フォルダにはDLL、設定例、この説明書とライセンス文書が入ります。SRTと暗号化ライブラリはDLLに静的リンクされるため、それぞれの実行用DLLを探す必要はありません。実行するPCには、ホストと同じビット数のMicrosoft Visual C++ 2015–2022以降の対応ランタイムが必要です。

依存ソースのバージョン、取得先、SHA-256は`dependencies.json`で固定しています。取得したアーカイブのハッシュを確認してから展開します。Mbed TLSの展開にはCMake内蔵のアーカイブ機能を使用し、PATH上の`tar.exe`や外部の`bzip2`には依存しません。アーカイブまたは復元済みソースが`.deps/`にあれば、次のコマンドでネットワークを使わずに再ビルドできます。

```powershell
.\build.ps1 -Offline -Test
```

ネット接続のあるPCから移す場合は、このプロジェクトと`.deps/`内の2つのアーカイブをコピーしてください。開発用コンパイラ、SDK、CMake、Ninjaは移行先にも必要です。

## CIとリリース

`TvRockOnTVTest`のCI構成を参考に、GitHub Actions（`.github/workflows/ci.yml`）とGitLab CI（`.gitlab-ci.yml`）を用意しています。

- GitHub Actionsはブランチpush時にx64・x86のReleaseビルドと全テストを実行します。タグpush時はテストを再実行せず、配布用ビルドとZIP作成後に`BonDriver_SRT.zip`をReleaseへ公開します。ブランチpushでテスト済みのコミットにタグを付けてください。自動実行はpushのみです。
- GitLab CIは従来どおりブランチpush、Merge Request、手動実行でビルド・テスト・ZIP作成を行い、タグpush時は同じ処理の成功後にReleaseへ公開します。
- ZIPには`BonDriver_SRT/x64/`と`BonDriver_SRT/x86/`が入り、それぞれDLL、`BonDriver_SRT.ini`、設定例、README、ライセンス文書を含みます。配布用INIは`ListenAddress=0.0.0.0`に設定し、全IPv4インターフェイスで待ち受けます。他の設定は設定例と同じです。
- 既存のReleaseを再実行した場合は同名の配布ファイルを更新し、既存のリリースノートは保持します。

GitHubでは`windows-2022`を使用します。公開用ジョブだけに`contents: write`を付与し、組み込みの`GITHUB_TOKEN`で公開します。追加のシークレットは不要です。

GitLabでは`saas-windows-medium-amd64`タグのWindows runnerを使用します。セルフホストの場合は`.gitlab-ci.yml`の`default.tags`を変更し、上記のビルドツールを用意してください。プロジェクトのPackage Registryを有効にしておく必要があります。組み込みの`CI_JOB_TOKEN`を使い、ZIPをGeneric Package RegistryへアップロードしてReleaseのダウンロードリンクを作ります。保存先のバージョンにはジョブIDを使用するため、タグ名に`/`があっても利用でき、再実行時にも衝突しません。ジョブartifactの保存期間は7日ですが、Release用ZIPはPackage Registryに残ります。パッケージの削除やクリーンアップを行う場合はReleaseからの参照に注意してください。

例えば、公開先リモートが`origin`の場合は次のようにリリースします。両サービスへ公開する場合は、それぞれのリモートへコミットとタグをpushしてください。

```powershell
git push origin HEAD
git tag v1.0.0
git push origin v1.0.0
```

ローカルでもCIと同じテスト・ZIP作成を実行できます。

```powershell
.\build-artifact.ps1
# 依存ソースを取得済みの場合
.\build-artifact.ps1 -Offline
```

生成先は`dist/BonDriver_SRT.zip`です。通常はテストが失敗すると新しいZIPを作成しません。GitHubのタグpushでは `build-artifact.ps1 -SkipTests` を使用し、テスト用ターゲットを含めずに配布物をビルドします。

## 受信を開始する

1. ホストアプリと同じビット数の`BonDriver_SRT.dll`をBonDriver配置先へコピーします。
2. リリースZIPの`BonDriver_SRT.ini`を同じ場所へコピーします（全IPv4インターフェイスで待受）。設定例から作る場合は`BonDriver_SRT.ini.example`をコピーし、名前を`BonDriver_SRT.ini`にします。
3. ホスト側でDLLとチャンネルを選択します。
4. 送信側をcallerモードで受信PCへ接続します。

設定ファイルがない場合も、ローカル接続・暗号化なしで動作します。既定のチャンネルは`SRT 0`から`SRT 9`で、待受ポートは3230から3239です。

```ini
[SRT]
ListenAddress=127.0.0.1
BasePort=3230
LatencyMs=120
Passphrase=
```

| 設定 | 指定内容 |
| --- | --- |
| `ListenAddress` | 待受するIPv4アドレス。LAN用にはPCのアドレス、全IPv4インターフェイスでは`0.0.0.0` |
| `BasePort` | チャンネル0のポート。1〜65526。チャンネル番号を加算して使用 |
| `LatencyMs` | 受信遅延。0〜60000ミリ秒。送信側の設定ともネゴシエートされる |
| `Passphrase` | 空欄で暗号化なし。暗号化には双方で同じ10〜79文字の印字可能ASCIIを設定 |

設定はチャンネル選択時に読み込みます。ファイル名を変えてDLLを配置した場合は、INIも同じベース名にしてください。LANで受信するときは、使用する待受ポートへのUDP通信をWindowsファイアウォールで許可します。

## FFmpegから送る

SRTを有効にしてビルドされたFFmpegを使用します。以下は同じPCの`SRT 0`へファイルを再生速度で送る例です。

```powershell
ffmpeg -re -i sample.ts -map 0:v:0 -map 0:a:0 -map "0:s?" -c copy -f mpegts "srt://127.0.0.1:3230?mode=caller&pkt_size=1316&latency=120000"
```

最初の映像と音声、およびすべての字幕（ARIB字幕を含む）を再エンコードせずに送ります。`-map "0:s?"`は字幕がないファイルでも続行する指定です。EPG・データ放送・未知のストリームは送信対象に含めません。放送TSで`-map 0`を使うと、未知のストリームが原因で`unsupported type`エラーになることがあります。字幕の表示には、受信アプリ側のARIB字幕対応と表示設定も必要です。

暗号化する場合は、INIの`Passphrase`と送信側を一致させます。

```powershell
ffmpeg -re -i sample.ts -map 0:v:0 -map 0:a:0 -map "0:s?" -c copy -f mpegts "srt://127.0.0.1:3230?mode=caller&pkt_size=1316&latency=120000&passphrase=replace-this-secret&pbkeylen=32"
```

`replace-this-secret`は自分のパスフレーズに変更してください。URL予約文字の指定方法は送信ソフトに従います。FFmpegの`latency`はマイクロ秒、INIの`LatencyMs`はミリ秒です。詳しくは[FFmpegのSRTオプション](https://ffmpeg.org/ffmpeg-protocols.html#srt)を参照してください。

### H.264へ変換して送る

`libx264`を有効にしてビルドされたFFmpegでは、映像をH.264へ変換し、音声と字幕をコピーしてSRT送信できます。次の例は、放送TSのインターレースを解除して送ります。

```powershell
ffmpeg -re -i sample.ts -map 0:v:0 -map 0:a:0 -map "0:s?" -vf "bwdif=mode=send_frame:parity=auto:deint=interlaced" -c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p -c:a copy -c:s copy -f mpegts "srt://127.0.0.1:3230?mode=caller&pkt_size=1316&latency=120000"
```

- `-c:v libx264`は映像をH.264へ変換します。`-preset veryfast`は変換速度を優先する設定です。
- `-crf 23`は画質の指定です。値を小さくすると高画質になり、通常は通信量も増えます。固定ビットレートにはなりません。
- `bwdif=mode=send_frame:parity=auto:deint=interlaced`は、インターレースとして識別されたフレームを解除します。29.97fpsのインターレース入力では、約29.97fpsのプログレッシブ映像になります。
- `-c:a copy -c:s copy`は音声と字幕を再エンコードせずに保持します。

実時間で送るには、変換中のログの`speed`が概ね`1.0x`以上を維持できる処理性能が必要です。受信アプリ側にもH.264のデコード対応が必要です。暗号化する場合は、上の暗号化送信例と同じ`passphrase`と`pbkeylen`をSRT URLへ追加してください。

## 動作範囲

- IPv4、SRT live/messageモードに対応し、各受信インスタンスは1送信元から受信します。受信側のcaller/rendezvousモードはありません。
- 各メッセージは188バイトTSパケットの整数個で構成してください。1316バイト（7パケット）を推奨します。RTPヘッダー付きデータ、192/204バイトTSは対象外です。
- 同じDLLを複数のTVTestから利用し、別々のポートで同時受信できます。例えば `/d BonDriver_SRT.dll /chspace 0 /chi 0` と `/d BonDriver_SRT.dll /chspace 0 /chi 1` は、既定の設定なら3230・3231で受信します。DLLの複製は不要です。
- 待受アドレス・ポートが同じ受信は重複起動できません。全IPv4待受 `0.0.0.0` と特定アドレスで同じポートを使う場合も競合します。別の特定アドレス同士は同じポートを使用できます。
- 各受信の接続・FIFO・終了処理は独立しています。1つを終了しても、他の受信は継続します。切断後にはそれぞれ再接続できます。`CreateBonDriver()` ごとに独立したインスタンスを返すため、各インスタンスに対して `Release()` を呼んでください。
- 受信FIFOは88ブロック、4,235,264バイトです。満杯または不正なTSを検出した場合はその接続を切ります。受信途中のメッセージを部分的に書き込むことはありません。
- 新しい接続、チャンネル変更、パージによってFIFOをクリアします。取得したデータのポインターは次のポインター取得まで有効で、受信スレッドが上書きしません。
- SRT liveは設定した遅延内に回復できない損失を許容する方式です。回線に応じて受信遅延を調整してください。
- ホストはDLLのアンロード前に`Release()`を呼ぶ必要があります。
- 設定や通信のエラーは`OutputDebugString`へ出力します。パスフレーズの値は診断に出力しません。

## テストとソース構成

`build.ps1 -Test`は、TSバッファ、設定値検証、実際のSRT接続を使うDLL統合テストを実行します。統合テストでは平文とAES-256でのバイト一致、両方のストリーム取得API、キーの未指定・不一致拒否、再接続、接続中の終了、受信待ちの解除、再オープンを確認します。テストはビルド先の専用ディレクトリで設定を生成し、利用者のINIには触れません。多重受信テストでは、平文・AES-256での別ポート同時受信とデータ分離、別プロセスとの競合、ワイルドカードの重複拒否、終了とポート再利用を確認します。既存の統合テストは41320、多重受信テストは42320〜42322を使用します。これらのテスト用ポートは他のアプリで使用しないでください。

実際のTVTest、FFmpegとの接続、別PC間通信、意図的なパケット損失を加えた環境については、実際に使用する構成で確認してください。

| ディレクトリ | 役割 |
| --- | --- |
| `include/` | BonDriverホストとのバイナリインターフェイス |
| `src/` | DLL窓口、設定読み込み、受信ワーカー、固定長TSバッファ |
| `tests/` | バッファ・設定・通信の検証 |
| `cmake/` | 外部ライブラリのビルド設定 |
| `tools/` | ハッシュ確認付き依存ソース復元 |
| `config/` | 設定ファイルのひな形 |

C++の整形規則は`.clang-format`、文字コードと改行規則は`.editorconfig`に定義しています。

## ライセンス

プロジェクトのソースは`LICENSE`に従います。依存ライブラリのバージョン、取得先、ライセンスは`THIRD_PARTY.md`に記載しています。配布用フォルダの`licenses/`も同梱してください。
