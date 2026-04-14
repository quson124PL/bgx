#!/bin/zsh


# Encode PNG → BGX
for f in ./test/source/*.png; do
    name="${f:t:r}"   # filename without path and extension
    ./bgx e "$f" "./test/sample/$name.bgx"
done

for f in ./test/source/*.png; do
    name="${f:t:r}"   # filename without path and extension
    ./bgx e "$f" "./test/sample/rle$name.bgx" rle
done

# Decode BGX → PNG
for f in ./test/sample/*.bgx; do
    name="${f:t:r}"
    ./bgx d "$f" "./test/output/$name.png"
done
