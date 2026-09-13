#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview/build/src/app
./Scanthia.exe &
PID=$!
sleep 6
if kill -0 $PID 2>/dev/null; then
    echo "STILL RUNNING (pid $PID)"
    kill $PID 2>/dev/null
else
    wait $PID
    echo "EXITED code=$?"
fi
