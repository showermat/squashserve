#! /bin/bash

cat << !
Do not execute this script directly!

This file is designed to guide the user through the steps necessary to convert
a Wikipedia ZIM file to a ZSR volume.  It does not check for the presence of
required binaries or the success of commands before continuing.  The user
should read and understand each line, then execute a similar command adjusted
for their system.

To execute this script anyway, comment out the exit line.
!
exit 1

src="http://download.kiwix.org/zim/wikipedia/wikipedia_en_all_2016-05.zim"

wget "$src"
zimdump -D wikipedia "$(basename "$src")"
cd wikipedia
mkdir ./-/j ./-/s I/m I/s _meta
mv A wiki
cd ./-
for file in *%2f*; do mv "./$file" "./${file//%2f//}"; done
cd ../I
for file in *%2f*; do mv "./$file" "./${file//%2f//}"; done
cd ../wiki
for file in *; do mv "./$file" "./${file// /_}.html"; done
cd ../_meta
cat > info.txt << !
title:Wikipedia
description:The free encyclopedia
language:eng
created:$(date "+%Y-%m-%d")
refer:$src
origin:http://en.wikipedia.org;^(.*).html$;\$1
home:wiki/Main_page.html
!
convert -background none "https://upload.wikimedia.org/wikipedia/en/8/80/Wikipedia-logo-v2.svg" -resize 64x64 favicon.png
cd ../..
mkvol wikipedia wikipedia.zsr

