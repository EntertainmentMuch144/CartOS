#!/bin/sh
# Optional OnionOS startup integration. It queues CartOS at boot when no game
# is already queued and adds a marked, reversible GameSwitcher hook.

SDCARD=/mnt/SDCARD
SYSDIR="$SDCARD/.tmp_update"
RUNTIME="$SYSDIR/runtime.sh"
APP="$SDCARD/App/CartOS/launch.sh"
SKIP_ANIMATION_MARKER="$SDCARD/App/CartOS/.skip-startup-animation-once"
BACKUP_DIR="$SYSDIR/CartOS-return-backup"
BACKUP="$BACKUP_DIR/runtime.sh.before"
TEMP="$SYSDIR/.runtime.sh.cartos.tmp"
COMMAND_TEMP="$SYSDIR/.cmd_to_run.sh.cartos.tmp"
LOGDIR="$SYSDIR/logs"
LOG="$LOGDIR/cartos-runtime-hook.log"

log() {
    mkdir -p "$LOGDIR" 2>/dev/null || return 0
    printf '%s CartOS startup: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

queue_cartos() {
    if [ -e "$SYSDIR/cmd_to_run.sh" ]; then
        # If CartOS is already queued, its return is already guaranteed. Clear
        # the durable game-session marker only for that CartOS command; retain
        # it when OnionOS has another game or app pending.
        if [ "${2-}" = "clear-game-marker" ] &&
           grep -qF '/mnt/SDCARD/App/CartOS/launch.sh' "$SYSDIR/cmd_to_run.sh"; then
            rm -f "$SYSDIR/cartos_return_to_launcher"
            sync
        fi
        log "left existing OnionOS launch queued"
        return 0
    fi
    cartos_command='LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so sh "/mnt/SDCARD/App/CartOS/launch.sh"'
    if [ "${1-}" = "skip-animation" ]; then
        : > "$SKIP_ANIMATION_MARKER" || return 1
        cartos_command="CARTOS_SKIP_STARTUP_ANIMATION=1 $cartos_command"
    fi
    printf '%s\n' "$cartos_command" > "$COMMAND_TEMP" || {
        rm -f "$COMMAND_TEMP"
        [ "${1-}" = "skip-animation" ] && rm -f "$SKIP_ANIMATION_MARKER"
        return 1
    }
    chmod 755 "$COMMAND_TEMP" || {
        rm -f "$COMMAND_TEMP"
        [ "${1-}" = "skip-animation" ] && rm -f "$SKIP_ANIMATION_MARKER"
        return 1
    }
    mv -f "$COMMAND_TEMP" "$SYSDIR/cmd_to_run.sh" || {
        rm -f "$COMMAND_TEMP"
        [ "${1-}" = "skip-animation" ] && rm -f "$SKIP_ANIMATION_MARKER"
        return 1
    }
    sync
    if [ "${2-}" = "clear-game-marker" ]; then
        rm -f "$SYSDIR/cartos_return_to_launcher"
        sync
    fi
    log "queued CartOS"
}

# OnionOS calls the same startup script after GameSwitcher exits.
if [ "${1-}" = "return" ]; then
    if [ ! -f "$APP" ]; then
        log "CartOS launch script missing; return skipped"
        exit 0
    fi
    queue_cartos skip-animation clear-game-marker || log "could not queue CartOS after GameSwitcher"
    exit 0
fi

if [ ! -f "$RUNTIME" ] || [ ! -f "$APP" ]; then
    log "OnionOS runtime or CartOS app missing; no changes made"
    exit 0
fi

# Migrate the earlier return-only hook in place when its saved pre-hook runtime
# exists. Keep that original backup so the restore script can still undo it.
if awk '
    /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
    in_function && /CartOS GameSwitcher return hook/ { old_hook=1 }
    in_function && /startup\/GameSwitcher-Return\.sh.*return/ { old_callback=1 }
    in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
    END { exit !(old_hook && old_callback) }
' "$RUNTIME"; then
    if [ ! -f "$BACKUP" ]; then
        log "older return hook found without its runtime backup; no changes made"
        exit 0
    fi
    if ! awk '
        /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
        in_function && /CartOS GameSwitcher return hook/ {
            indent=$0; sub(/[^[:space:]].*/, "", indent)
            print indent "# CartOS startup and GameSwitcher return hook"
            changed=1
            next
        }
        in_function && /startup\/GameSwitcher-Return\.sh.*return/ {
            indent=$0; sub(/[^[:space:]].*/, "", indent)
            print indent "sh \"$sysdir/startup/CartOS.sh\" return"
            changed=1
            next
        }
        { print }
        in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
        END { if (!changed) exit 3 }
    ' "$RUNTIME" > "$TEMP"; then
        rm -f "$TEMP"
        log "could not update older return hook; runtime left unchanged"
        exit 0
    fi
    chmod 755 "$TEMP" && mv -f "$TEMP" "$RUNTIME" || {
        rm -f "$TEMP"
        log "could not migrate older return hook; runtime left unchanged"
        exit 0
    }
    sync
    log "updated earlier return hook to use CartOS startup script"
fi

# Keep an existing current hook up to date. Previous CartOS releases checked
# only /tmp, which loses the return marker if the device powers off in a game.
if awk '
    /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
    in_function && /CartOS startup and GameSwitcher return hook/ { installed=1 }
    in_function && /\/tmp\/cartos_return_to_launcher.*\|\|/ { volatile_marker=1 }
    in_function && /\$sysdir\/cartos_return_to_launcher/ { persistent_marker=1 }
    in_function && /startup\/CartOS\.sh.*return/ { callback=1 }
    in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
    END { exit !(installed && volatile_marker && persistent_marker && callback) }
' "$RUNTIME"; then
    log "runtime return hook already installed"
elif awk '
    /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
    in_function && /CartOS startup and GameSwitcher return hook/ { installed=1 }
    in_function && /startup\/CartOS\.sh.*return/ { callback=1 }
    in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
    END { exit !(installed && callback) }
' "$RUNTIME"; then
    # Replace only CartOS's marked block, preserving the rest of the OnionOS
    # runtime and its original return-to-MainUI branch.
    if ! awk '
        /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
        in_function && /# CartOS startup and GameSwitcher return hook/ {
            indent=$0; sub(/#.*/, "", indent)
            print indent "# CartOS startup and GameSwitcher return hook"
            print indent "if [ -f /tmp/cartos_return_to_launcher ] || [ -f \"$sysdir/cartos_return_to_launcher\" ]; then"
            print indent "    rm -f /tmp/cartos_return_to_launcher"
            print indent "    sh \"$sysdir/startup/CartOS.sh\" return"
            upgraded=1
            replacing=1
            next
        }
        replacing && /elif[[:space:]]+\[[[:space:]]+-f[[:space:]]+\/tmp\/run_advmenu[[:space:]]*\];[[:space:]]*then/ {
            print
            replacing=0
            anchor=1
            next
        }
        replacing { next }
        { print }
        in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
        END { if (!upgraded || !anchor) exit 3 }
    ' "$RUNTIME" > "$TEMP"; then
        rm -f "$TEMP"
        log "could not upgrade marked runtime hook; runtime left unchanged"
        exit 0
    fi
    if ! chmod 755 "$TEMP" || ! mv -f "$TEMP" "$RUNTIME"; then
        rm -f "$TEMP"
        log "could not install updated runtime hook; original left unchanged"
        exit 0
    fi
    sync
    log "upgraded runtime hook for power-off game resume"
else
    # Prepare a candidate first. Unknown OnionOS runtime layouts are left alone.
    if ! awk '
        /^[[:space:]]*check_main_ui[[:space:]]*\(\)[[:space:]]*\{/ { in_function=1 }
        in_function && /^[[:space:]]*if[[:space:]]+\[[[:space:]]+-f[[:space:]]+\/tmp\/run_advmenu[[:space:]]*\][[:space:]]*;[[:space:]]*then[[:space:]]*$/ {
            indent=$0
            sub(/if.*/, "", indent)
            print indent "# CartOS startup and GameSwitcher return hook"
            print indent "if [ -f /tmp/cartos_return_to_launcher ] || [ -f \"$sysdir/cartos_return_to_launcher\" ]; then"
            print indent "    rm -f /tmp/cartos_return_to_launcher"
            print indent "    sh \"$sysdir/startup/CartOS.sh\" return"
            print indent "elif [ -f /tmp/run_advmenu ]; then"
            found=1
            next
        }
        { print }
        in_function && /^[[:space:]]*}[[:space:]]*$/ { in_function=0 }
        END { if (!found) exit 3 }
    ' "$RUNTIME" > "$TEMP"; then
        rm -f "$TEMP"
        log "expected runtime hook point not found; no changes made"
        exit 0
    fi

    chmod 755 "$TEMP" || {
        rm -f "$TEMP"
        log "could not prepare runtime; original left unchanged"
        exit 0
    }
    if ! mv -f "$TEMP" "$RUNTIME"; then
        rm -f "$TEMP"
        log "could not install runtime hook; original left unchanged"
        exit 0
    fi
    sync
    log "installed marked GameSwitcher return hook"
fi

# The runtime's ordinary boot path launches this queued command after all
# startup scripts finish. Leave a pending game/app command untouched.
if [ ! -e "$SYSDIR/cmd_to_run.sh" ]; then
    queue_cartos || log "could not queue CartOS for boot"
else
    log "left existing OnionOS launch queued"
fi
exit 0
