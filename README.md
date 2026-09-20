# urusi-emacs

A WinUI 3 application that owns the window and takes its contents from Emacs, so that the interface can be built with native controls and described in Elisp.

Emacs runs inside this process. The application loads `libemacs.dll` — Emacs built as a DLL, from the [`urusi` branch of the fork](https://github.com/i999rri/emacs/tree/urusi) — and calls its exported `w32_emacs_init` on a thread with the 8 MB stack Emacs expects. Nothing is serialised between processes and no socket is listening; the only thing crossing the boundary is a C call.

## Building

Emacs is built first, in the MSYS2 mingw64 shell, from the fork:

```sh
make -C src libemacs.dll
```

and then dumped with that DLL, which writes `libemacs.pdmp` beside it.

`scripts/stage-emacs.sh` installs that Emacs into `emacs/` here, in the layout Emacs expects: the DLL and its dump in `bin`, the Lisp and data under `share`. Emacs finds all of it from the path of the DLL, and the application finds the DLL next to itself, so nothing has to be told where anything is.

```sh
scripts/stage-emacs.sh ~/source/repos/emacs-build
```

That is 134 MB, of which 110 MB is Emacs's own Lisp. The DLL is stripped down to 4 MB on the way; pass `--debug` to keep its debug information.

The host then builds from `urusi-emacs.slnx`, in Visual Studio with the Windows App SDK workload, and takes `emacs/` into the package.

## Running

Emacs's standard output and error are shown in the window, which is where a failure to start appears.

Two environment variables override where things are, for running against a build that is not staged:

- `URUSI_EMACS_DLL` — the path of `libemacs.dll`.
- `URUSI_LISP_DIR` — the directory holding `urusi.el`.

<details>
<summary>日本語</summary>

ウインドウを持つのは WinUI 3 のアプリで、その中身を Emacs が決める。ネイティブのコントロールで見た目を作って、それを Elisp で書けるようにするのが目的。

Emacs はこのプロセスの中で動く。アプリが `libemacs.dll`（[fork の `urusi` ブランチ](https://github.com/i999rri/emacs/tree/urusi)で DLL としてビルドした Emacs）を読み込んで、export されている `w32_emacs_init` を 8 MB スタックのスレッドで呼ぶ。プロセス間のやり取りはないし、ソケットも開かない。境界を越えるのは C の関数呼び出しだけ。

### ビルド

先に Emacs を、MSYS2 の mingw64 シェルで fork からビルドする。

```sh
make -C src libemacs.dll
```

そのあと、その DLL でダンプすると隣に `libemacs.pdmp` ができる。

`scripts/stage-emacs.sh` で、その Emacs を `emacs/` に Emacs が期待する配置でインストールする。DLL と dump が `bin`、Lisp とデータが `share` の下。Emacs は全部 DLL の場所から見つけるし、アプリは DLL を自分の隣から見つけるので、場所を教える設定はどこにも要らない。

```sh
scripts/stage-emacs.sh ~/source/repos/emacs-build
```

これで 134 MB。うち 110 MB は Emacs の Lisp。DLL は途中で strip して 4 MB にする。デバッグ情報を残すなら `--debug`。

ホストはそのあと Visual Studio（Windows App SDK ワークロード）で `urusi-emacs.slnx` からビルドすると、`emacs/` ごとパッケージに入る。

### 実行

Emacs の標準出力とエラーはウインドウに出る。起動に失敗したときもそこに出る。

ステージングしていないビルドで動かしたいときは環境変数で上書きできる。

- `URUSI_EMACS_DLL` — `libemacs.dll` のパス。
- `URUSI_LISP_DIR` — `urusi.el` があるディレクトリ。

</details>
