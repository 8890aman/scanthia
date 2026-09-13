#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview/build/src/app
./Scanthia.exe --open "/c/Users/Administrator/Desktop/ANONYMOUS ANONYMOUS/ANON00099 HRCT Chest" 2>&1 &
PID=$!
sleep 20
if kill -0 $PID 2>/dev/null; then
    echo "ALIVE"
    kill $PID
else
    wait $PID
    echo "DIED code=$?"
fi
