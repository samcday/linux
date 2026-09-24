#!/bin/bash
# ask.sh TAG "Title" "Body text" "choice1|choice2|..."
# Pops a critical notification + zenity dialog on pem120's desktop (Hyprland/swaync).
# Writes the answer to $D/replies/TAG.txt as: epoch_utc|phone_uptime|answer
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DISPLAY=:1 \
       DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
D=/home/ishu/claude-touch; mkdir -p $D/replies
TAG=$1 TITLE=$2 BODY=$3 CHOICES=$4
OUT=$D/replies/$TAG.txt
[ -e "$OUT" ] && { echo "REFUSE: $OUT exists"; exit 1; }
echo "$(date -u +%s)|$(timeout 5 adb -s 124ccc8f shell cat /proc/uptime 2>/dev/null | cut -d' ' -f1)|ASKED" > $OUT
notify-send -u critical -a "Claude (touch test)" "$TITLE" "$BODY" 2>/dev/null
for s in /usr/share/sounds/freedesktop/stereo/bell.oga /usr/share/sounds/freedesktop/stereo/message-new-instant.oga; do
  [ -f $s ] && { paplay $s 2>/dev/null & break; }
done
(
  IFS='|' read -ra C <<< "$CHOICES"
  args=(); first=TRUE
  for c in "${C[@]}"; do args+=("$first" "$c"); first=FALSE; done
  ans=$(zenity --list --radiolist --title="Claude: $TITLE" --width=560 --height=440 \
        --text="$BODY" --column="" --column="Answer" "${args[@]}" 2>/dev/null)
  rc=$?
  echo "$(date -u +%s)|$(timeout 5 adb -s 124ccc8f shell cat /proc/uptime 2>/dev/null | cut -d' ' -f1)|rc=$rc|$ans" >> $OUT
) >/dev/null 2>&1 < /dev/null &
disown
echo "asked $TAG -> $OUT"
