#!/usr/bin/env bash
export PATH=/usr/bin:/bin:/ucrt64/bin:$PATH
BASE="/c/Users/Administrator/Desktop/ANONYMOUS ANONYMOUS"

for dir in "CT Angio 5MM" "CT Lungs 510mm"; do
    files=$(find "$BASE" -ipath "*$dir*" -name "*.dcm" | sort | head -1)
    filel=$(find "$BASE" -ipath "*$dir*" -name "*.dcm" | sort | tail -1)
    echo "=== $dir"
    echo "first $files"
    dcmdump "$files" 2>/dev/null | grep "0020,0032"
    echo "last $filel"
    dcmdump "$filel" 2>/dev/null | grep "0020,0032"
done
