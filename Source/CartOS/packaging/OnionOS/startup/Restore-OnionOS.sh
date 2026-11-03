#!/bin/sh
# Remove CartOS's marked runtime hook and stop its boot launch.

SDCARD=/mnt/SDCARD
SYSDIR="$SDCARD/.tmp_update"
RUNTIME="$SYSDIR/runtime.sh"
TEMP="$SYSDIR/.runtime.sh.restore.tmp"
LOGDIR="$SYSDIR/logs"
LOG="$LOGDIR/cartos-runtime-hook.log"

log() {
    mkdir -p "$LOGDIR" 2>/dev/null || return 0
    printf '%s CartOS restore: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

if [ ! -f "$RUNTIME" ]; then
    log "OnionOS runtime not found; no changes made"
    exit 0
fi

# Support the earlier return-only release, which kept a runtime copy on card.
if awk '
    /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
    in_function && /CartOS GameSwitcher return hook/ { legacy=1 }
    in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
    END { exit !legacy }
' "$RUNTIME"; then
    LEGACY_BACKUP="$SYSDIR/CartOS-return-backup/runtime.sh.before"
    if [ ! -f "$LEGACY_BACKUP" ]; then
        log "older return hook found but its saved runtime is missing; no changes made"
        exit 0
    fi
    if ! cp -p "$LEGACY_BACKUP" "$TEMP" || ! chmod 755 "$TEMP" || ! mv -f "$TEMP" "$RUNTIME"; then
        rm -f "$TEMP"
        log "could not restore the earlier runtime; startup files left unchanged"
        exit 0
    fi
else
    # Remove only the exact block added by the current CartOS startup script.
    if ! awk '
        /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
        in_function && /# CartOS startup and GameSwitcher return hook/ { state=1; next }
        state==1 && /if[[:space:]]+\[.*\/tmp\/cartos_return_to_launcher.*\];[[:space:]]*then/ { state=2; next }
        state==2 && /rm -f \/tmp\/cartos_return_to_launcher/ { state=3; next }
        state==3 && /sh "\$sysdir\/startup\/CartOS\.sh" return/ { state=4; next }
        state==4 && /elif \[ -f \/tmp\/run_advmenu \]; then/ {
            indent=$0; sub(/elif.*/, "", indent)
            print indent "if [ -f /tmp/run_advmenu ]; then"
            restored=1
            state=0
            next
        }
        { print }
        in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
        END { if (!restored) exit 3 }
    ' "$RUNTIME" > "$TEMP"; then
        rm -f "$TEMP"
        log "CartOS runtime marker not found; runtime left unchanged"
        exit 0
    fi

    if ! chmod 755 "$TEMP" || ! mv -f "$TEMP" "$RUNTIME"; then
        rm -f "$TEMP"
        log "could not remove the CartOS runtime hook; runtime left unchanged"
        exit 0
    fi
fi

# Remove only CartOS's own queued launch; keep a queued game or other app.
if [ -f "$SYSDIR/cmd_to_run.sh" ] && grep -qF '/mnt/SDCARD/App/CartOS/launch.sh' "$SYSDIR/cmd_to_run.sh"; then
    rm -f "$SYSDIR/cmd_to_run.sh"
fi
rm -f /tmp/cartos_return_to_launcher
rm -f "$SYSDIR/cartos_return_to_launcher"
rm -f "$SYSDIR/startup/CartOS.sh" "$SYSDIR/startup/GameSwitcher-Return.sh" "$0"
sync
log "removed CartOS runtime hook and startup launch"
exit 0
