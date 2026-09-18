#!/bin/bash
# get_shareware.sh -- fetches id's shareware Quake 1.06 into a folder and unpacks it.
# Idempotent: does nothing if it's already there.
#
#   tools/get_shareware.sh [dir]     (default: ./data)
#
# Leaves <dir>/q106/id1/pak0.pak and id's slicnse.txt / licinfo.txt beside it, which tools/mkzip.py packs into the
# archive the door sends. The episode is inside quake106.zip as resource.1, an LHA archive, so lhasa (or lha, or 7z)
# is needed: apt install lhasa.
set -euo pipefail

DEST="${1:-$(dirname "$0")/../data}"
URLS=(
    "https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake/quake106.zip"
    "https://www.quaddicted.com/files/idgames/idstuff/quake/quake106.zip"
)
PAK_MD5=5906e5998fc3d896ddaf5e6a62e03abb      # the 1.06 shareware pak0.pak

if [ -f "$DEST/q106/id1/pak0.pak" ]; then
    echo "OK: shareware Quake already in $DEST/q106"
    exit 0
fi
mkdir -p "$DEST"
if [ ! -f "$DEST/quake106.zip" ]; then
    for url in "${URLS[@]}"; do
        echo "Downloading shareware Quake 1.06 (about 9 MB) from $url ..."
        curl -fL --progress-bar -o "$DEST/quake106.zip.part" "$url" && mv "$DEST/quake106.zip.part" "$DEST/quake106.zip" && break
    done
fi
[ -f "$DEST/quake106.zip" ] || { echo "ERROR: couldn't download quake106.zip" >&2; exit 1; }

rm -rf "$DEST/q106"
unzip -q -o "$DEST/quake106.zip" -d "$DEST/q106"
cd "$DEST/q106"
if command -v lhasa >/dev/null; then lhasa xq resource.1
elif command -v lha >/dev/null; then lha xq resource.1
elif command -v 7z >/dev/null; then 7z x -y resource.1 >/dev/null
else echo "ERROR: need lhasa, lha or 7z to unpack resource.1 (apt install lhasa)" >&2; exit 1; fi

[ -f id1/pak0.pak ] || { echo "ERROR: resource.1 held no id1/pak0.pak" >&2; exit 1; }
if command -v md5sum >/dev/null && [ "$(md5sum id1/pak0.pak | cut -c1-32)" != "$PAK_MD5" ]; then
    echo "ERROR: id1/pak0.pak isn't the 1.06 shareware pak" >&2; exit 1
fi
echo "OK: shareware Quake in $DEST/q106"
