#!/bin/bash
# Play a 1 kHz sine (left, then right) through the default PipeWire sink while logging the
# UR44 meters with timestamps.
# Usage: tonetest.sh [scale_percent] [extra speaker-test args...]
#   scale 100 = 0 dBFS, 10 = -20 dBFS (speaker-test's default is 80 = -1.9 dBFS)
# Logs: meters.log and speaker-test.log in a new temporary directory (printed at start and end).

UR44="$(dirname "$(readlink -f "$0")")/../tools/ur44.py"
LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/ur44-tonetest.XXXXXX") || exit 1
echo "Logging to ${LOGDIR}"
scale=${1:-80}
shift

adddate() {
    while IFS= read -r line; do
        printf '%s %s\n' "$(date +'%F %T.%03N')" "$line";
    done
}

"${UR44}" meters | adddate > "${LOGDIR}/meters.log" &
meters_proc=`jobs -p`
echo "Meters run as ${meters_proc}"

( sleep 1; speaker-test -D pipewire -t sine -f 1000 -l 1 -c 2 -S "${scale}" "$@" ) | adddate > "${LOGDIR}/speaker-test.log" &
tone_proc=$!
echo "Tone runs as ${tone_proc} (scale ${scale}%)"

wait ${tone_proc}
sleep 1
# ur44.py turns SIGTERM into a clean exit that stops the device's meter stream
kill -SIGTERM ${meters_proc}
wait
echo "Logs: ${LOGDIR}/meters.log ${LOGDIR}/speaker-test.log"
