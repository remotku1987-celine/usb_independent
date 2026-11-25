#!/usr/bin/env bash

set -u
export LC_NUMERIC=C

JACK_PID="${1:-3973}"
USB_PID="${2:-4013}"
DURATION="${3:-60}"
INTERVAL=1

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RESULT_DIR="$SCRIPT_DIR/../benchmark_results"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
CSV="$RESULT_DIR/benchmark_$TIMESTAMP.csv"

INTERRUPTED=0
SAMPLES=0

mkdir -p "$RESULT_DIR"

if ! [[ "$JACK_PID" =~ ^[0-9]+$ &&
        "$USB_PID" =~ ^[0-9]+$ &&
        "$DURATION" =~ ^[0-9]+$ ]] || (( DURATION < 1 )); then
    echo "ERROR: PID dan durasi harus berupa angka positif."
    exit 1
fi

check_process() {
    [[ -r "/proc/$1/stat" && -r "/proc/$1/status" ]]
}

for PID in "$JACK_PID" "$USB_PID"; do
    if ! check_process "$PID"; then
        echo "ERROR: PID $PID tidak ditemukan."
        exit 1
    fi
done

trap 'INTERRUPTED=1' INT TERM

read_cpu_ticks() {
    local STAT
    local -a FIELDS

    STAT="$(cat "/proc/$1/stat" 2>/dev/null)" || return 1
    STAT="${STAT##*) }"
    read -r -a FIELDS <<< "$STAT"

    [[ ${#FIELDS[@]} -gt 12 ]] || return 1

    echo $((FIELDS[11] + FIELDS[12]))
}

read_memory() {
    awk '
        /^VmRSS:/  {rss=$2}
        /^VmSize:/ {vsz=$2}
        END {printf "%d %d\n", rss, vsz}
    ' "/proc/$1/status" 2>/dev/null
}

read_threads() {
    local PID="$1"
    local COUNT=0

    for T in "/proc/$PID"/task/*; do
        [[ -d "$T" ]] && ((COUNT+=1))
    done

    echo "$COUNT"
}

CLK_TCK="$(getconf CLK_TCK)"

echo "USB INDEPENDENT EXTERNAL BENCHMARK"
echo "=================================="
echo "JACK PID       : $JACK_PID"
echo "USB Runtime PID: $USB_PID"
echo "Duration       : $DURATION seconds"
echo "Interval       : $INTERVAL second"
echo "Output         : $CSV"
echo

echo "timestamp,elapsed_s,jack_cpu_percent,usb_cpu_percent,jack_rss_kb,usb_rss_kb,jack_vsz_kb,usb_vsz_kb,jack_threads,usb_threads" > "$CSV"

PREV_JACK="$(read_cpu_ticks "$JACK_PID")" || exit 1
PREV_USB="$(read_cpu_ticks "$USB_PID")" || exit 1

START_NS="$(date +%s%N)"
PREV_NS="$START_NS"

while (( SAMPLES < DURATION && INTERRUPTED == 0 )); do
    sleep "$INTERVAL"

    (( INTERRUPTED == 1 )) && break

    if ! check_process "$JACK_PID" || ! check_process "$USB_PID"; then
        echo
        echo "ERROR: Salah satu proses berhenti selama benchmark."
        break
    fi

    CUR_JACK="$(read_cpu_ticks "$JACK_PID")" || break
    CUR_USB="$(read_cpu_ticks "$USB_PID")" || break

    NOW_NS="$(date +%s%N)"
    DELTA_NS=$((NOW_NS - PREV_NS))
    ELAPSED_NS=$((NOW_NS - START_NS))

    if (( DELTA_NS <= 0 )); then
        echo "ERROR: Interval waktu tidak valid."
        break
    fi

    JACK_CPU="$(awk -v d="$((CUR_JACK-PREV_JACK))" -v hz="$CLK_TCK" -v ns="$DELTA_NS" 'BEGIN {printf "%.2f", d*100000000000/(hz*ns)}')"

    USB_CPU="$(awk -v d="$((CUR_USB-PREV_USB))" -v hz="$CLK_TCK" -v ns="$DELTA_NS" 'BEGIN {printf "%.2f", d*100000000000/(hz*ns)}')"

    read -r JACK_RSS JACK_VSZ <<< "$(read_memory "$JACK_PID")"
    read -r USB_RSS USB_VSZ <<< "$(read_memory "$USB_PID")"

    JACK_THREADS="$(read_threads "$JACK_PID")"
    USB_THREADS="$(read_threads "$USB_PID")"

    NOW="$(date '+%Y-%m-%d %H:%M:%S')"
    ELAPSED="$(awk -v ns="$ELAPSED_NS" 'BEGIN {printf "%.2f", ns/1000000000}')"

    echo "$NOW,$ELAPSED,$JACK_CPU,$USB_CPU,$JACK_RSS,$USB_RSS,$JACK_VSZ,$USB_VSZ,$JACK_THREADS,$USB_THREADS" >> "$CSV"

    ((SAMPLES+=1))

    printf "\rElapsed: %6s s | JACK CPU: %6s%% | USB CPU: %6s%%" \
        "$ELAPSED" "$JACK_CPU" "$USB_CPU"

    PREV_JACK="$CUR_JACK"
    PREV_USB="$CUR_USB"
    PREV_NS="$NOW_NS"
done

echo
echo
echo "===== BENCHMARK SUMMARY ====="
echo "Samples recorded: $SAMPLES"

if (( SAMPLES > 0 )); then
    awk -F, '
        NR > 1 {
            jack_sum += $3
            usb_sum += $4
            if ($3 > jack_max) jack_max = $3
            if ($4 > usb_max) usb_max = $4
            count++
        }
        END {
            if (count > 0) {
                printf "JACK CPU average : %.2f%%\n", jack_sum/count
                printf "JACK CPU maximum : %.2f%%\n", jack_max
                printf "USB CPU average  : %.2f%%\n", usb_sum/count
                printf "USB CPU maximum  : %.2f%%\n", usb_max
            }
        }
    ' "$CSV"
fi

if (( INTERRUPTED == 1 )); then
    echo "Status: STOPPED BY USER"
else
    echo "Status: FINISHED"
fi

echo "CSV: $CSV"
