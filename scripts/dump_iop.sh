#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
BASE="/c/Users/Administrator/Desktop/ANONYMOUS ANONYMOUS"

for dir in "CT Angio 5MM" "Lungs" "Plain"; do
    f=$(find "$BASE" -ipath "*$dir*" -name "*.dcm" | head -1)
    [ -z "$f" ] && continue
    echo "=== $dir : $f"
    dcmdump "$f" 2>/dev/null | grep -E "0020,0037|0020,0032|0018,5100"
done
