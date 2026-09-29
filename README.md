# urushi-emacs

A WinUI 3 application that owns the window and takes its contents from Emacs, so that the interface can be built with native controls and described in Elisp.

Emacs runs inside this process. The application loads `libemacs.dll` — Emacs built as a DLL, from the [`urushi` branch of the fork](https://github.com/i999rri/emacs/tree/urushi) — and calls its exported `w32_emacs_init` on a thread with the 8 MB stack Emacs expects.

The two then talk by calling each other. The application exports `host_get_api`, Emacs asks for it as it starts, and from then on Lisp sends a message with `host-post` and takes the ones coming back with `host-take-events`. That interface is `external/emacs/libemacs/src/host.h`, included straight from the fork so the two sides cannot drift. Nothing in it is particular to Windows, so that an application on another system is the same thing to Emacs. No socket is listening and nothing crosses a process boundary.

## Building

The fork is a submodule, so a clone brings everything:

```sh
git clone --recurse-submodules https://github.com/i999rri/urushi-emacs.git
```

Emacs's history is large, and none of it is needed to build. `git clone` then `git submodule update --init --filter=blob:none` fetches only what a checkout uses.

Emacs is built first, in the MSYS2 mingw64 shell. `--deps` installs the mingw64 packages it is built against, which is only needed once on a machine:

```sh
scripts/build-emacs.sh --deps
```

That builds `emacs.exe`, then `libemacs.dll`, then dumps `libemacs.pdmp` with the DLL, into `external/emacs-build`.

`scripts/stage-emacs.sh` installs the result into `emacs/` here, in the layout Emacs expects: the DLL and its dump in `bin`, the Lisp and data under `share`. Emacs finds all of it from the path of the DLL, and the application finds the DLL next to itself, so nothing has to be told where anything is.

```sh
scripts/stage-emacs.sh
```

That is 134 MB, of which 110 MB is Emacs's own Lisp. The DLL is stripped down to 4 MB on the way; pass `--debug` to keep its debug information.

After a change to Emacs's C, or to urushi's Lisp, `scripts/refresh-emacs.sh` does only what that needs: it recompiles what changed, links and dumps the DLL, and copies it and the Lisp into `emacs/` and into the application's build output. That takes seconds on one core, where building and staging everything again takes minutes on several. A change to a header most of `src/` includes, to Emacs's own Lisp or to `configure.ac` still wants the two scripts above.

```sh
scripts/refresh-emacs.sh
```

The host then builds from `urushi-emacs.slnx`, in Visual Studio with the Windows App SDK workload. Deploying `package` in `platforms/windows/` builds the application in `platforms/windows/host/` and packages it with `emacs/` beside the executable, and with the Lisp in `lisp/` as it is, so a change to that needs no staging.

| Directory | What is in it |
| --- | --- |
| `core/` | The code of the host that no platform has a part in: what becomes of keys, the input method, the pointer, frames on the screen, splitters. Each directory is a namespace, `urushi::core::input` for `core/Input/`. |
| `platforms/windows/host/` | The Windows application, in C++ with WinUI 3: what Windows and XAML have a part in. `urushi::windows::input` for `Input/`, and so on. |
| `platforms/windows/package/` | Its package: the manifest, the images, and what it carries besides the application. |
| `tests/` | The host's tests, `core/` and `windows/` as the code, and the traces they play back in `tests/traces/`. |
| `lisp/` | The Lisp urushi brings, and its tests in `lisp/test/`. |
| `docs/` | How to make the screen your own, with recipes. |
| `scripts/` | Building and staging Emacs, and the scripts that check what the application does in `scripts/verify/`. |
| `external/` | The Emacs fork. |

## Running

Emacs's standard output and error are shown in the window, which is where a failure to start appears.

`URUSHI_EMACS_DLL` overrides the path of `libemacs.dll`, for running against a build that is not staged.

<details>
<summary>日本語</summary>

ウインドウを持つのは WinUI 3 のアプリで、その中身を Emacs が決める。ネイティブのコントロールで見た目を作って、それを Elisp で書けるようにするのが目的。

Emacs はこのプロセスの中で動く。アプリが `libemacs.dll`（[fork の `urushi` ブランチ](https://github.com/i999rri/emacs/tree/urushi)で DLL としてビルドした Emacs）を読み込んで、export されている `w32_emacs_init` を 8 MB スタックのスレッドで呼ぶ。

やり取りはお互いを呼ぶだけ。アプリが `host_get_api` を export していて、Emacs が起動時にそれを取りに来る。あとは Lisp から `host-post` で送って、`host-take-events` で受け取る。インターフェースは `external/emacs/libemacs/src/host.h`。fork から直接 include しているので、両側がズレようがない。中身に Windows 固有のものはないので、別のシステムのアプリでも Emacs から見れば同じものになる。ソケットは開かないし、プロセス境界を越えるものもない。

### ビルド

fork は submodule なので、clone すれば一式そろう。

```sh
git clone --recurse-submodules https://github.com/i999rri/urushi-emacs.git
```

Emacs の履歴は大きいが、ビルドには要らない。`git clone` のあと `git submodule update --init --filter=blob:none` にすると、チェックアウトに使うものだけ取ってくる。

先に Emacs を MSYS2 の mingw64 シェルでビルドする。`--deps` は Emacs がリンクする mingw64 のパッケージを入れるもので、マシンごとに1回だけ。

```sh
scripts/build-emacs.sh --deps
```

`emacs.exe` → `libemacs.dll` → その DLL で `libemacs.pdmp` をダンプ、までを `external/emacs-build` の中でやる。

`scripts/stage-emacs.sh` で、できたものを `emacs/` に Emacs が期待する配置でインストールする。DLL と dump が `bin`、Lisp とデータが `share` の下。Emacs は全部 DLL の場所から見つけるし、アプリは DLL を自分の隣から見つけるので、場所を教える設定はどこにも要らない。

```sh
scripts/stage-emacs.sh
```

これで 134 MB。うち 110 MB は Emacs の Lisp。DLL は途中で strip して 4 MB にする。デバッグ情報を残すなら `--debug`。

Emacs の C や urushi の Lisp を直したあとは `scripts/refresh-emacs.sh` で足りる。変わったところだけコンパイルして DLL をリンク・dump し、DLL と Lisp を `emacs/` とアプリのビルド出力にコピーする。1コアで数秒で終わる。全部ビルドしてステージし直すと複数コアで数分かかる。`src/` の大半が include するヘッダや Emacs 本体の Lisp、`configure.ac` を変えたときは、上の2つのスクリプトが要る。

```sh
scripts/refresh-emacs.sh
```

ホストはそのあと Visual Studio（Windows App SDK ワークロード）で `urushi-emacs.slnx` からビルドする。`platforms/windows/` の `package` を配置すると、`platforms/windows/host/` のアプリがビルドされ、exe の隣に `emacs/` を置いた形でパッケージになる。`lisp/` の Lisp はそのまま入るので、直してもステージし直す必要はない。

| ディレクトリ | 中身 |
| --- | --- |
| `core/` | ホストのうち、どのプラットフォームにも依存しないコード。キーの扱い、IME、ポインタ、画面上のフレーム、分割バーなど。ディレクトリがそのまま namespace になる (`core/Input/` は `urushi::core::input`)。 |
| `platforms/windows/host/` | Windows のアプリ (C++ / WinUI 3)。Windows と XAML に依存する部分。`Input/` は `urushi::windows::input` のように対応する。 |
| `platforms/windows/package/` | そのパッケージ。マニフェスト、画像、アプリのほかに詰めるもの。 |
| `tests/` | ホストのテスト。コードと同じく `core/` と `windows/` に分け、再生する記録は `tests/traces/`。 |
| `lisp/` | urushi が同梱する Lisp。テストは `lisp/test/`。 |
| `docs/` | 画面を自分のものにする方法とレシピ。 |
| `scripts/` | Emacs のビルドとステージ。アプリの動作を確かめるスクリプトは `scripts/verify/`。 |
| `external/` | Emacs の fork。 |

### 実行

Emacs の標準出力とエラーはウインドウに出る。起動に失敗したときもそこに出る。

ステージングしていないビルドで動かしたいときは `URUSHI_EMACS_DLL` で `libemacs.dll` のパスを上書きできる。

</details>
