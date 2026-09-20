# urusi-emacs

A WinUI 3 application that owns the window and takes its contents from Emacs, so that the interface can be built with native controls and described in Elisp.

Emacs runs inside this process. The application loads `libemacs.dll` — Emacs built as a DLL, from the [`urusi` branch of the fork](https://github.com/i999rri/emacs/tree/urusi) — and calls its exported `w32_emacs_init` on a thread with the 8 MB stack Emacs expects. Nothing is serialised between processes and no socket is listening; the only thing crossing the boundary is a C call.

## Building

The host needs Visual Studio with the Windows App SDK workload, and builds from `urusi-emacs.slnx`.

Emacs is built separately, in the MSYS2 mingw64 shell, from the fork:

```sh
make -C src libemacs.dll
```

then dumped with that DLL, which writes `libemacs.pdmp` beside it. Emacs finds both its dump and its Lisp from the path of the DLL, so the application only has to say where the DLL is.

## Running

Two environment variables say where things are, which is what a development build wants:

- `URUSI_EMACS_DLL` — the path of `libemacs.dll`. Without it the application looks for `emacs\libemacs.dll` next to itself.
- `URUSI_LISP_DIR` — the directory holding `urusi.el`. Without it, `lisp` next to the application.

Emacs's standard output and error are shown in the window, which is where a failure to start appears.

<details>
<summary>日本語</summary>

ウインドウを持つのは WinUI 3 のアプリで、その中身を Emacs が決める。ネイティブのコントロールで見た目を作って、それを Elisp で書けるようにするのが目的。

Emacs はこのプロセスの中で動く。アプリが `libemacs.dll`（[fork の `urusi` ブランチ](https://github.com/i999rri/emacs/tree/urusi)で DLL としてビルドした Emacs）を読み込んで、export されている `w32_emacs_init` を 8 MB スタックのスレッドで呼ぶ。プロセス間のやり取りはないし、ソケットも開かない。境界を越えるのは C の関数呼び出しだけ。

### ビルド

ホストは Visual Studio（Windows App SDK ワークロード）で `urusi-emacs.slnx` からビルドする。

Emacs は fork から MSYS2 の mingw64 シェルで別にビルドする。

```sh
make -C src libemacs.dll
```

そのあと、その DLL でダンプすると隣に `libemacs.pdmp` ができる。Emacs は dump も Lisp も DLL の場所から見つけるので、アプリが指定するのは DLL の場所だけでいい。

### 実行

開発中の指定は環境変数 2 つ。

- `URUSI_EMACS_DLL` — `libemacs.dll` のパス。なければアプリの隣の `emacs\libemacs.dll` を見る。
- `URUSI_LISP_DIR` — `urusi.el` があるディレクトリ。なければアプリの隣の `lisp`。

Emacs の標準出力とエラーはウインドウに出る。起動に失敗したときもそこに出る。

</details>
