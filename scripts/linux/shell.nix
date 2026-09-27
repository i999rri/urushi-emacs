# What building Emacs for Linux takes, and running the tests of the
# host against it:
#
#   nix-shell scripts/linux/shell.nix --run scripts/linux/build-emacs.sh
#
# Only what Emacs needs without X: its frames are the host's to draw.
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    autoconf
    automake
    gnumake
    gcc
    pkg-config
    texinfo
    ncurses
    gnutls
    harfbuzz
    python3
    git

    # Image formats, so that the host build reads what an Emacs on a
    # desktop reads: without these image.c compiles only its own XBM and
    # PBM readers, and an image of any other kind is one no test can
    # reach.  The host draws what Emacs decoded, so which formats it
    # knows is decided here and not there.
    libpng
    libjpeg
    giflib
    libtiff
    librsvg
    libwebp
  ];

  # Fonts for the host build of Emacs to measure text with: it reads
  # font files itself, from the XDG data directories among others,
  # and NixOS in WSL has none installed.
  shellHook = ''
    export XDG_DATA_DIRS=${pkgs.dejavu_fonts}/share''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}
  '';
}
