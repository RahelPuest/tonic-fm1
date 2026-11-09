#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Every picture of the documentation, from the host simulator: docs/img (the manual), docs/img/themes.png and the
# README's ../docs/screens.png.
#   tools/doc_images.sh            (after python3 ../base/tools/build.py . --host; needs Pillow)
cd "$(dirname "$0")/.."
H=build/host/tonic_host
fail=0
run() {                                         # run() SCRIPT OUTDIR: a host script, its expectations checked
    mkdir -p "$2"
    if $H "tests/host/$1.txt" "$2" 2>&1 | grep -iE 'fail|mismatch'; then fail=1; fi
}
run manual_shots docs/img
rm -f docs/img/*.txt                            # (the simulator's MIDI log, not a picture)
# the pictures that come from the feature tests' own set-ups: published name, script, shot
while read -r name script shot; do
    run "$script" "build/shots/$script"
    cp "build/shots/$script/$shot" "docs/img/$name"
done <<'LIST'
chord_page.png euc08 chord_page.png
euc_acc.png euc08 euc_acc.png
euc_kit.png euc08 euc_kit.png
comp.png new06 comp.png
insert.png new012 insert.png
menv.png new012 menv.png
seq_track.png new012 seq_track.png
va_shape.png va va_shape.png
pfx.png pfx pfx.png
pfxb.png pfxb023 pfxb.png
presets.png presets presets_list.png
theme_paper.png ui ui_paper.png
engines.png hold018 engines.png
LIST
run doc_themes build/shots/doc_themes
python3 - <<'PY'
from PIL import Image, ImageDraw
th = Image.new('RGB', (1200, 480))
for i in range(10):
    th.paste(Image.open('build/shots/doc_themes/th%d.png' % i).convert('RGB'), ((i % 5) * 240, (i // 5) * 240))
th.save('docs/img/themes.png')
# the README's six screens, twice the size, rounded
names = ['home', 'engines', 'drum_kit', 'roll', 'pfxb', 'lfo']
im = Image.new('RGB', (32 + 3 * 512, 32 + 2 * 512), (24, 24, 28))
mask = Image.new('L', (480, 480), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, 479, 479), 14, fill=255)
for i, n in enumerate(names):
    s = Image.open('docs/img/%s.png' % n).convert('RGB').resize((480, 480), Image.NEAREST)
    im.paste(s, (32 + (i % 3) * 512, 32 + (i // 3) * 512), mask)
im.save('../docs/screens.png', optimize=True)
PY
ls docs/img/*.png | wc -l | xargs echo "docs/img:"
[ "$fail" = 0 ] && echo "doc images: ok"
