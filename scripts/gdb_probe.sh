#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview
gdb -batch -ex run -ex "bt 20" --args ./build/tests/meda_probe.exe "/c/Users/Administrator/Desktop/ANONYMOUS ANONYMOUS/ANON00099 HRCT Chest" probe_hrct.png 2>&1 | tail -35
