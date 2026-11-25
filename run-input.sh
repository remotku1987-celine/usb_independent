#!/usr/bin/env bash
set -Eeuo pipefail

# ==========================================
# USB INDEPENDENT - PROCESS MANAGER
# ==========================================

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
RUNTIME="$BUILD_DIR/usb_independent_runtime"

# JACK configuration
JACK_DEVICE="${JACK_DEVICE:-hw:0,0}"
JACK_RATE=48000
JACK_PERIOD=128
JACK_PERIODS=3
CPU_CORE=3

JACK_PRIORITY=95
RUNTIME_PRIORITY=88

# Runtime state and logs
STATE_DIR="${XDG_RUNTIME_DIR:-/tmp}/usb-independent-$UID"

JACK_PID_FILE="$STATE_DIR/jack.pid"
RUNTIME_PID_FILE="$STATE_DIR/runtime.pid"

JACK_LOG="$STATE_DIR/jack.log"
RUNTIME_LOG="$STATE_DIR/runtime.log"

mkdir -p "$STATE_DIR"
chmod 700 "$STATE_DIR"

# ==========================================
# UTILITIES
# ==========================================

require_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: Command tidak ditemukan: $1"
        exit 1
    }
}

get_pid() {
    [[ -f "$1" ]] && cat "$1" || true
}

pid_alive() {
    local pid="$1"
    [[ "$pid" =~ ^[0-9]+$ ]] && kill -0 "$pid" 2>/dev/null
}

pid_matches() {
    local pid="$1"
    local type="$2"
    local cmd

    pid_alive "$pid" || return 1
    [[ -r "/proc/$pid/cmdline" ]] || return 1

    cmd="$(tr '\0' ' ' < "/proc/$pid/cmdline")"

    case "$type" in
        jack)
            [[ "$cmd" == *jackd* ]]
            ;;
        runtime)
            [[ "$cmd" == *"$RUNTIME"* ]]
            ;;
        *)
            return 1
            ;;
    esac
}

# ==========================================
# JACK CHECK
# ==========================================

jack_running() {
    jack_lsp >/dev/null 2>&1
}

# ==========================================
# WAIT FOR JACK
# ==========================================

wait_for_jack() {
    local i

    for ((i=0; i<50; i++)); do
        if jack_running; then
            return 0
        fi

        sleep 0.2
    done

    return 1
}

# ==========================================
# WAIT FOR USB INDEPENDENT PORTS
# ==========================================

wait_for_runtime_ports() {
    local i

    for ((i=0; i<100; i++)); do

        if ! pid_alive "$(get_pid "$RUNTIME_PID_FILE")"; then
            return 1
        fi

        if jack_lsp 2>/dev/null |
            grep -Fxq "usb_independent:CM108_INPUT_01" &&
           jack_lsp 2>/dev/null |
            grep -Fxq "usb_independent:CM108_INPUT_02"; then
            return 0
        fi

        sleep 0.2
    done

    return 1
}

# ==========================================
# CONNECT JACK PORTS
# ==========================================

connect_port() {
    local source="$1"
    local destination="$2"

    if jack_lsp -c "$source" 2>/dev/null |
        grep -Fq "$destination"; then
        echo "Already connected: $source -> $destination"
        return 0
    fi

    jack_connect "$source" "$destination"

    echo "Connected: $source -> $destination"
}

connect_audio() {
    echo
    echo "===== CONNECT AUDIO ====="

    connect_port \
        "usb_independent:CM108_INPUT_01" \
        "system:playback_1"

    connect_port \
        "usb_independent:CM108_INPUT_01" \
        "system:playback_2"

    connect_port \
        "usb_independent:CM108_INPUT_02" \
        "system:playback_1"

    connect_port \
        "usb_independent:CM108_INPUT_02" \
        "system:playback_2"
}

# ==========================================
# STOP PROCESS
# ==========================================

stop_process() {
    local name="$1"
    local pid_file="$2"
    local type="$3"

    local pid
    pid="$(get_pid "$pid_file")"

    if [[ -z "$pid" ]]; then
        echo "$name: PID tidak ditemukan."
        return 0
    fi

    if ! pid_matches "$pid" "$type"; then
        echo "$name: proses tidak aktif atau PID tidak sesuai."
        rm -f "$pid_file"
        return 0
    fi

    echo "Stopping $name (PID $pid)..."

    kill -INT "$pid" 2>/dev/null || true

    for ((i=0; i<80; i++)); do
        if ! pid_alive "$pid"; then
            break
        fi
        sleep 0.1
    done

    if pid_alive "$pid"; then
        echo "$name belum berhenti, mengirim SIGTERM..."
        kill -TERM "$pid" 2>/dev/null || true
    fi

    for ((i=0; i<30; i++)); do
        if ! pid_alive "$pid"; then
            break
        fi
        sleep 0.1
    done

    if pid_alive "$pid"; then
        echo "WARNING: $name tidak berhenti normal, SIGKILL dikirim."
        kill -KILL "$pid" 2>/dev/null || true
    fi

    rm -f "$pid_file"

    echo "$name stopped."
}

# ==========================================
# STOP ALL
# ==========================================

stop_all() {
    echo
    echo "===== STOP USB INDEPENDENT ====="

    # Runtime harus berhenti dahulu.
    stop_process "USB Independent" "$RUNTIME_PID_FILE" runtime

    # Setelah runtime berhenti, JACK dapat dihentikan.
    stop_process "JACK2" "$JACK_PID_FILE" jack

    echo
    echo "USB Independent stopped."
}

# ==========================================
# START
# ==========================================

start_all() {
    require_command jackd
    require_command jack_lsp
    require_command jack_connect
    require_command cmake
    require_command taskset
    require_command chrt

    echo "========================================"
    echo "       USB INDEPENDENT MANAGER"
    echo "========================================"

    # Hindari menjalankan dua JACK server.
    if jack_running; then
        echo "ERROR: JACK server sudah berjalan."
        echo "Hentikan JACK yang sedang berjalan terlebih dahulu."
        exit 1
    fi

    echo
    echo "===== BUILD RUNTIME ====="

    cmake -S "$ROOT_DIR" -B "$BUILD_DIR"

    cmake --build "$BUILD_DIR" \
        --target usb_independent_runtime \
        -j"$(nproc)"

    [[ -x "$RUNTIME" ]] || {
        echo "ERROR: Runtime executable tidak ditemukan."
        exit 1
    }

    # --------------------------------------
    # START JACK
    # --------------------------------------

    echo
    echo "===== START JACK2 ====="

    echo "CPU       : $CPU_CORE"
    echo "Priority  : FIFO $JACK_PRIORITY"
    echo "Device    : $JACK_DEVICE"
    echo "SampleRate: $JACK_RATE"
    echo "Buffer    : $JACK_PERIOD frames"

    taskset -c "$CPU_CORE" \
        chrt -f "$JACK_PRIORITY" \
        jackd -R -P "$JACK_PRIORITY" \
        -d alsa \
        -d "$JACK_DEVICE" \
        -r "$JACK_RATE" \
        -p "$JACK_PERIOD" \
        -n "$JACK_PERIODS" \
        > "$JACK_LOG" 2>&1 &

    echo "$!" > "$JACK_PID_FILE"

    if ! wait_for_jack; then
        echo "ERROR: JACK gagal berjalan."
        cat "$JACK_LOG"
        stop_process "JACK2" "$JACK_PID_FILE" jack
        exit 1
    fi

    echo "JACK ready."

    # --------------------------------------
    # START USB INDEPENDENT
    # --------------------------------------

    echo
    echo "===== START USB INDEPENDENT ====="

    echo "CPU      : $CPU_CORE"
    echo "Priority : FIFO $RUNTIME_PRIORITY"

    taskset -c "$CPU_CORE" \
        chrt -f "$RUNTIME_PRIORITY" \
        "$RUNTIME" \
        > "$RUNTIME_LOG" 2>&1 &

    echo "$!" > "$RUNTIME_PID_FILE"

    if ! wait_for_runtime_ports; then
        echo "ERROR: Runtime gagal menyediakan kedua port."
        cat "$RUNTIME_LOG"
        stop_all
        exit 1
    fi

    echo "Runtime ports ready."

    # --------------------------------------
    # CONNECT AUDIO
    # --------------------------------------

    if ! connect_audio; then
        echo "ERROR: JACK routing gagal."
        stop_all
        exit 1
    fi

    echo
    echo "========================================"
    echo " USB INDEPENDENT STARTED SUCCESSFULLY"
    echo "========================================"

    echo "JACK PID    : $(get_pid "$JACK_PID_FILE")"
    echo "Runtime PID : $(get_pid "$RUNTIME_PID_FILE")"

    echo
    jack_bufsize

    echo
    echo "Gunakan:"
    echo "./usb-independent.sh status"
    echo "./usb-independent.sh stop"
}

# ==========================================
# STATUS
# ==========================================

show_status() {
    echo "========================================"
    echo "       USB INDEPENDENT STATUS"
    echo "========================================"

    local jack_pid runtime_pid

    jack_pid="$(get_pid "$JACK_PID_FILE")"
    runtime_pid="$(get_pid "$RUNTIME_PID_FILE")"

    echo
    echo "JACK2:"

    if pid_matches "$jack_pid" jack; then
        echo "  PID      : $jack_pid"
        echo "  Status   : RUNNING"
    else
        echo "  Status   : STOPPED / NOT MANAGED"
    fi

    echo
    echo "USB Independent:"

    if pid_matches "$runtime_pid" runtime; then
        echo "  PID      : $runtime_pid"
        echo "  Status   : RUNNING"
    else
        echo "  Status   : STOPPED"
    fi

    echo
    echo "JACK SERVER:"

    if jack_running; then
        echo "  Status   : ACTIVE"
        echo "  Buffer   : $(jack_bufsize) frames"

        echo
        echo "JACK PORT CONNECTIONS:"
        jack_lsp -c
    else
        echo "  Status   : INACTIVE"
    fi

    echo
    echo "LOG FILES:"
    echo "  JACK    : $JACK_LOG"
    echo "  Runtime : $RUNTIME_LOG"
}

# ==========================================
# COMMAND
# ==========================================

case "${1:-}" in
    start)
        start_all
        ;;
    status)
        show_status
        ;;
    stop)
        stop_all
        ;;
    *)
        echo "Usage:"
        echo "  $0 start"
        echo "  $0 status"
        echo "  $0 stop"
        exit 1
        ;;
esac