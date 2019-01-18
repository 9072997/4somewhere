#!/bin/bash
maxSameType=10
for folder in icons/* ; do
	type="$(basename "$folder")"
	echo "// $type"
	echo
	i=0
	for file in "$folder"/* ; do
		echo "// $file"
		php imgToSprite.php "$file" | ./sprite-pack.bash "${type}_${i}"
		echo
		i="$(("$i" + 1 ))" #i++
		if [ "$i" -ge "$maxSameType" ] ; then
			break
		fi
	done

	i="$(("$i" - 1 ))" #i--
	echo "const unsigned char* ${type}[] = {"
	for n in $(seq 0 "$i") ; do
		echo "  ${type}_${n},"
	done | sed '$ s/,$//'
	echo '};'
	echo
	echo
done
