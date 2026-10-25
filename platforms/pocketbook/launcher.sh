#!/bin/sh
root=/mnt/ext1/system/kobalt
mkdir -p "$root"
if [ -f "$root/app.log" ] && [ "$(wc -c < "$root/app.log")" -gt 65536 ]; then
    mv "$root/app.log" "$root/app.previous.log"
fi
exec "$root/bin/kobalt" "$@" >> "$root/app.log" 2>&1
