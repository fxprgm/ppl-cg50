#!/bin/bash
# prime_ask.sh: ask HP's Virtual Calculator (macOS app "HP Prime") how it evaluates things.
#
#   tools/prime_ask.sh [-o OUTDIR] [-n NAME] LINE...      type each LINE + Enter in the Home view
#   tools/prime_ask.sh [-o OUTDIR] [-n NAME] -f FILE      the same, one line per line of FILE
#
# After every line the calculator's screen (the history) is saved as OUTDIR/NAME-NN.png
# (default OUTDIR = build/prime, NAME = ask); the last one is also OUTDIR/NAME-last.png. Read the
# answers off those images. The history shows the last ~7 entries, so one PNG per line keeps
# every answer visible.
#
# Needs: the HP Prime app running in its Home view; the terminal that runs this needs macOS
# Accessibility (to type) and Screen Recording (to capture) permissions. It types only while
# HP Prime is the frontmost app and stops otherwise, so keystrokes never land elsewhere.
#
# Typing notes: lines go through AppleScript `keystroke`, after two Esc presses (they close
# an error box and clear the entry line). Use Entry:=1 (Algebraic): in Textbook entry "/"
# opens a fraction template and the rest of the line lands in its denominator. "E" types the EEX exponent
# (1+5E-12); ":=" works; "i" is the variable i, use (a,b) for complex numbers. A leading "-"
# is read as "Ans -" (use (-1)*… or put it after an operand). Multi-line programs: type them
# in the Program editor yourself, then call the function from here.
set -u
OUT=build/prime NAME=ask FILE=
while getopts "o:n:f:" o; do
    case $o in
        o) OUT=$OPTARG ;;
        n) NAME=$OPTARG ;;
        f) FILE=$OPTARG ;;
        *) sed -n '2,20p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
lines=()
if [ -n "$FILE" ]; then
    while IFS= read -r l || [ -n "$l" ]; do [ -n "$l" ] && lines+=("$l"); done <"$FILE"
fi
lines+=("$@")
[ ${#lines[@]} -gt 0 ] || { sed -n '2,20p' "$0"; exit 2; }
mkdir -p "$OUT"

pgrep -xq "HP Prime" || { echo "prime_ask: HP Prime is not running" >&2; exit 1; }

front() { osascript -e 'tell application "System Events" to get name of first process whose frontmost is true'; }

capture() {
    local xy x y
    xy=$(osascript -e 'tell application "System Events" to tell process "HP Prime" to get position of window 1') || return 1
    x=${xy%%,*}; y=${xy##*, }
    # the screen area of the default skin: the status bar and the history, below the title bar
    screencapture -x -R"$x,$((y + 20)),327,280" "$1"
}

k=0
for l in "${lines[@]}"; do
    k=$((k + 1))
    osascript -e 'tell application "HP Prime" to activate' -e 'delay 0.4' >/dev/null
    f=$(front)
    if [ "$f" != "HP Prime" ]; then
        echo "prime_ask: HP Prime is not the frontmost app ($f): stopped before line $k" >&2
        exit 1
    fi
    # escape backslashes and quotes for AppleScript
    esc=${l//\\/\\\\}; esc=${esc//\"/\\\"}
    # Esc twice first: closes an error box left by the previous line and clears the entry line
    osascript -e 'tell application "System Events" to key code 53' -e 'delay 0.25' \
              -e 'tell application "System Events" to key code 53' -e 'delay 0.25' >/dev/null
    osascript -e "tell application \"System Events\" to keystroke \"$esc\"" -e 'delay 0.3' \
              -e 'tell application "System Events" to key code 36' -e 'delay 0.7' >/dev/null
    png=$(printf '%s/%s-%02d.png' "$OUT" "$NAME" "$k")
    capture "$png" && cp "$png" "$OUT/$NAME-last.png"
    printf '%02d  %s  ->  %s\n' "$k" "$l" "$png"
done
