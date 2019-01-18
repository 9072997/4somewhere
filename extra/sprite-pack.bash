#!/bin/bash
if [ -z "$1" ] ; then
	spriteName='mySprite'
else
	spriteName="$1"
fi

echo "const PROGMEM unsigned char $spriteName[] {"
(
	bstr=""
	while read i ; do
		bstr+="$(echo -n "$i" | rev)"
	done
	bstr+="0000000"
	len=${#bstr}
	for i in $(seq 0 8 $len) ; do
		echo ",${bstr:$i:8}B  " | rev
	done | head -n -1
) | sed '$ s/.$//' | column -x | tr -d '\t'
echo '};'
