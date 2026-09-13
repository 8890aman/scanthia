#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview/build/src/app
gdb -batch -ex run -ex "bt 25" --args ./Scanthia.exe --open "/c/Users/Administrator/Desktop/ANONYMOUS ANONYMOUS/ANON00099 HRCT Chest" 2>&1 | tail -40
