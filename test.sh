#!/bin/zsh

# Encode PNG → BGX
for f in ./source/*.png; do
    name="${f:t:r}"   # filename without path and extension
    ./bgx e "$f" "./sample/$name.bgx"
done

# Decode BGX → PNG
for f in ./sample/*.bgx; do
    name="${f:t:r}"
    ./bgx d "$f" "./output/$name.png"
done