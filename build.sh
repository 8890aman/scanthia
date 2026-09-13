#!/usr/bin/env bash
export PATH=/usr/bin:/ucrt64/bin:$PATH
cd /c/Users/Administrator/Desktop/medaview
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /tmp/cfg.log 2>&1 || { tail -30 /tmp/cfg.log; exit 1; }
cmake --build build -j8 > /tmp/build.log 2>&1
grep -n "error:" /tmp/build.log | head -40
tail -4 /tmp/build.log
