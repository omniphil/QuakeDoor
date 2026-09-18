#!/usr/bin/env python3
"""
mkzip.py -- packs the Quake shareware episode into quake-shareware.zip, the one asset the door sends.

    python3 tools/mkzip.py <folder holding id1/pak0.pak and slicnse.txt> door/quake-shareware.zip

id's shareware licence (slicnse.txt) lets the shareware episode be passed on free of charge, "as a whole", "only in a
compressed format", and with the licence "accompanying the Software at all times". So the pak doesn't travel on its
own: it goes compressed, in one archive with id's licence and information files, unchanged. The game unpacks the pak
in memory on the player's PC (module/src/pak_trace.c).

Deflate, with fixed timestamps and a fixed order, so the same files always make the same archive, and so the same hash:
players who already have it aren't sent it again.
"""
import os
import sys
import zipfile

FILES = ['id1/pak0.pak', 'slicnse.txt', 'licinfo.txt']

def main():
    src, out = sys.argv[1], sys.argv[2]
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in FILES:
            path = os.path.join(src, name)
            if not os.path.exists(path):
                sys.exit(f'{path}: missing')
            info = zipfile.ZipInfo(name, date_time=(1996, 6, 22, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(path, 'rb') as f:
                z.writestr(info, f.read(), compresslevel=9)
    print(f'{out}: {os.path.getsize(out)} bytes')

if __name__ == '__main__':
    main()
