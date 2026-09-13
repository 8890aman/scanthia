#!/bin/bash
# Record a short demo clip of the Scanthia window.
# Usage: ./record_demo.sh [seconds]   (default 20)
# 1. This launches Scanthia with the test CT loaded.
# 2. ffmpeg records ONLY the "Scanthia" window via gdigrab.
# 3. Interact during the countdown — scroll slices, drag W/L, etc.
# Output: scanthia_demo.mp4 in the repo root.

cd "$(dirname "$0")"
DUR=${1:-20}

# Launch the app with the test study loaded (background).
./build/src/app/Scanthia.exe --open tests/data/ct_series &
APP_PID=$!
sleep 4   # let the window open + series load

echo "Recording ${DUR}s of the Scanthia window — interact now!"
# No microphone: silent audio track so players don't complain about
# missing audio. Drop `-f lavfi -i anullsrc=...` if you want zero audio
# stream entirely.
ffmpeg -y -f gdigrab -framerate 30 -t "$DUR" -i title=Scanthia \
       -f lavfi -i anullsrc=cl=stereo:r=44100 \
       -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
       -c:v libx264 -pix_fmt yuv420p -preset fast -crf 20 \
       -c:a aac -b:a 128k -shortest \
       scanthia_demo.mp4 2>/dev/null

kill $APP_PID 2>/dev/null
echo "Saved: scanthia_demo.mp4"
