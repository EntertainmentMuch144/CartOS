#!/bin/sh
# Optional OnionOS boot hook. It queues CartOS only when OnionOS has no
# pending command, preserving OnionOS auto-resume of the last running game.
SDCARD=/mnt/SDCARD
APP="$SDCARD/App/CartOS/launch.sh"
SYSDIR="$SDCARD/.tmp_update"
COMMAND="$SYSDIR/cmd_to_run.sh"
TEMP="$COMMAND.cartos.tmp"

if [ -e "$SYSDIR/config/.noCartOSAutostart" ] || [ -e "$COMMAND" ] || [ ! -x "$APP" ]; then
    exit 0
fi

printf '%s\n' 'LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so "/mnt/SDCARD/App/CartOS/launch.sh"' > "$TEMP"
chmod 755 "$TEMP"
mv -f "$TEMP" "$COMMAND"
