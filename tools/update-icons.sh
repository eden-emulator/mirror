#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Updates main icons for eden

which magick || exit
which optipng || exit

VARIATION=${VARIATION:-base}

EDEN_BASE_SVG="dist/icon_variations/${VARIATION}.svg"
EDEN_NAMED_SVG="dist/icon_variations/${VARIATION}_named.svg"
ANDROID_BG_COLOR=$(cat "dist/icon_variations/${VARIATION}_bgcolor")

[ -f "$EDEN_BASE_SVG" ] && [ -f "$EDEN_NAMED_SVG" ] || { echo "Error: missing SVG" >&2; exit; }

# Desktop / Windows / Qt icons

magick -density 256x256 -background transparent "$EDEN_BASE_SVG" -define icon:auto-resize -colors 256 dist/eden.ico || exit
magick -density 256x256 -background transparent "$EDEN_BASE_SVG" -resize 256x256 dist/yuzu.bmp || exit

magick -size 256x256 -background transparent "$EDEN_BASE_SVG" dist/qt_themes/default/icons/256x256/eden.png || exit
magick -size 256x256 -background transparent "$EDEN_NAMED_SVG" dist/qt_themes/default/icons/256x256/eden_named.png || exit

magick dist/qt_themes/default/icons/256x256/eden.png -resize 256x256! dist/qt_themes/default/icons/256x256/eden.png || exit
magick dist/qt_themes/default/icons/256x256/eden_named.png -resize 256x256! dist/qt_themes/default/icons/256x256/eden_named.png || exit

optipng -o7 dist/qt_themes/default/icons/256x256/eden*.png

# Android adaptive icon (API 26+)

ANDROID_RES="src/android/app/src/main/res"
ANDROID_FG="$ANDROID_RES/drawable/ic_launcher_foreground.png"

# Android adaptive icon (API 26+)

echo "<?xml version='1.0' encoding='utf-8'?><resources><color name='ic_launcher_background'>${ANDROID_BG_COLOR}</color></resources>" > "$ANDROID_RES/values/colors.xml"
magick -size 1080x1080 -background transparent "$EDEN_BASE_SVG" -gravity center -resize 660x660 -extent 1080x1080 "${ANDROID_FG}" || exit

# DraVee: 'VARIATION=newyear2025' needs a slight adjutment
#magick mogrify -roll -15-5 ${ANDROID_FG}

optipng -o7 "$ANDROID_FG"

# Android legacy launcher icon (API <= 24)

BASE_LEGACY="$ANDROID_RES/mipmap-xxxhdpi/ic_launcher.png"

magick -size 512x512 xc:${ANDROID_BG_COLOR} "$ANDROID_FG" -gravity center -resize 384x384 -composite "$BASE_LEGACY" || exit

magick "$BASE_LEGACY" -resize 192x192 "$ANDROID_RES/mipmap-xxhdpi/ic_launcher.png"
magick "$BASE_LEGACY" -resize 144x144 "$ANDROID_RES/mipmap-xhdpi/ic_launcher.png"
magick "$BASE_LEGACY" -resize 96x96 "$ANDROID_RES/mipmap-hdpi/ic_launcher.png"
magick "$BASE_LEGACY" -resize 72x72 "$ANDROID_RES/mipmap-mdpi/ic_launcher.png"

optipng -o7 "$ANDROID_RES"/mipmap-*/ic_launcher.png

# macOS

TMP_PNG="dist/eden-tmp.png"

magick -size 1024x1024 -background none "$EDEN_BASE_SVG" "$TMP_PNG" || exit

png2icns dist/eden.icns "$TMP_PNG" || echo 'non fatal'
cp dist/eden.icns dist/yuzu.icns

rm "$TMP_PNG"
