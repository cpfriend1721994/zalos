#!/bin/sh
# Minimal stand-in for file(1) on AppImage build hosts that lack it: recognises PNG, ELF, SVG, desktop entries and shell
# scripts, like the command it replaces when appimagetool only needs a type check.
brief=0
for a in "$@"; do
    case "$a" in -b|--brief) brief=1 ;; esac
done
for arg in "$@"; do
    case "$arg" in -*) continue ;; esac
    desc="data"
    if [ -f "$arg" ]; then
        magic=$(head -c 8 "$arg" 2>/dev/null | od -An -tx1 | tr -d ' \n')
        head=$(head -c 32 "$arg" 2>/dev/null)
        case "$magic" in
            89504e470d0a1a0a*) desc="PNG image data, 256 x 256, 8-bit/color RGBA, non-interlaced" ;;
            7f454c46*) desc="ELF 64-bit LSB executable" ;;
            *)
                case "$head" in
                    *"<svg"*) desc="SVG Scalable Vector Graphics image" ;;
                    *"[Desktop Entry]"*) desc="desktop configuration file" ;;
                    "#!"*) desc="POSIX shell script text executable" ;;
                esac
                case "$arg" in
                    *.svg) desc="SVG Scalable Vector Graphics image" ;;
                    *.desktop) desc="desktop configuration file" ;;
                esac
                ;;
        esac
    fi
    if [ "$brief" -eq 1 ]; then
        echo "$desc"
    else
        echo "$arg: $desc"
    fi
done
