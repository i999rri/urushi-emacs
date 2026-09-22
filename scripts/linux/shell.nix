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
  ];

  # Fonts for the host build of Emacs to measure text with: it reads
  # font files itself, from the XDG data directories among others,
  # and NixOS in WSL has none installed.
  shellHook = ''
    export XDG_DATA_DIRS=${pkgs.dejavu_fonts}/share''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}
  '';
}
