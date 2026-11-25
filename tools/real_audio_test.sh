
#!/usr/bin/env bash

set -u
export LC_NUMERIC=C

DURATION=60
OUT="benchmark_results/real_audio_test.wav"
LOG="benchmark_results/real_audio_test.log"

mkdir -p benchmark_results

echo "======================================"
echo " USB INDEPENDENT - REAL AUDIO TEST"
echo "======================================"

# 1. Periksa JACK
if ! jack_lsp >/dev/null 2>&1; then
    echo "ERROR: JACK tidak aktif"
    exit 1
fi

# 2. Periksa USB Independent
PORT1="usb_independent:CM108_INPUT_01"
PORT2="usb_independent:CM108_INPUT_02"

if ! jack_lsp | grep -Fxq "$PORT1"; then
    echo "ERROR: $PORT1 tidak ditemukan"
    exit 1
fi

if ! jack_lsp | grep -Fxq "$PORT2"; then
    echo "ERROR: $PORT2 tidak ditemukan"
    exit 1
fi

echo "USB Independent: kedua output ditemukan"

# 3. Jalankan FFmpeg
echo
echo "Memulai FFmpeg..."

ffmpeg -hide_banner -loglevel warning \
    -f jack -channels 2 -i real_audio_test \
    -t "$DURATION" -y "$OUT" \
    >"$LOG" 2>&1 &

FFPID=$!

cleanup() {
    if kill -0 "$FFPID" 2>/dev/null; then
        kill "$FFPID" 2>/dev/null || true
        wait "$FFPID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# 4. Tunggu port FFmpeg
FFPORT1=""
FFPORT2=""

for i in $(seq 1 50); do
    PORTS=$(jack_lsp 2>/dev/null || true)

    FFPORT1=$(printf '%s\n' "$PORTS" |
        grep -E '^real_audio_test[^:]*:input_1$' | head -n1 || true)

    FFPORT2=$(printf '%s\n' "$PORTS" |
        grep -E '^real_audio_test[^:]*:input_2$' | head -n1 || true)

    if [[ -n "$FFPORT1" && -n "$FFPORT2" ]]; then
        break
    fi

    if ! kill -0 "$FFPID" 2>/dev/null; then
        echo "ERROR: FFmpeg berhenti sebelum port tersedia"
        cat "$LOG"
        exit 1
    fi

    sleep 0.1
done

if [[ -z "$FFPORT1" || -z "$FFPORT2" ]]; then
    echo "ERROR: Port input FFmpeg tidak ditemukan"
    exit 1
fi

echo "Port FFmpeg ditemukan:"
echo "  $FFPORT1"
echo "  $FFPORT2"

# 5. Hubungkan kedua input
echo
echo "Menghubungkan output USB..."

jack_connect "$PORT1" "$FFPORT1" || exit 1
jack_connect "$PORT2" "$FFPORT2" || exit 1

echo "Routing:"
jack_lsp -c

# 6. Tunggu rekaman selesai
echo
echo "Merekam selama ${DURATION} detik..."
wait "$FFPID"
STATUS=$?

trap - EXIT INT TERM

if [[ "$STATUS" -ne 0 ]]; then
    echo "ERROR: FFmpeg gagal"
    cat "$LOG"
    exit 1
fi

# 7. Analisis hasil
echo
echo "======================================"
echo " ANALISIS AUDIO"
echo "======================================"

ffmpeg -hide_banner -i "$OUT" \
    -af "astats=metadata=0:reset=0" \
    -f null - 2>&1 |
    grep -E 'Channel:|Peak level dB:|RMS level dB:|Max level:|Number of samples:'

echo
echo "======================================"
echo " TEST SELESAI"
echo "WAV : $OUT"
echo "LOG : $LOG"
echo "======================================"
