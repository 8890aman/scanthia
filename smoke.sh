#!/usr/bin/env bash
export PATH=/usr/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview/build
./src/app/scanthia.exe &
APPPID=$!
sleep 6
if kill -0 $APPPID 2>/dev/null; then
  echo "APP RUNNING OK"
  kill $APPPID
else
  echo "APP CRASHED"
  wait $APPPID
  echo "exit=$?"
fi
