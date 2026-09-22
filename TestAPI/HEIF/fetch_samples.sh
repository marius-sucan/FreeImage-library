#!/bin/sh
# download the third-party samples into samples/ and check their MD5 (curl, md5sum)
cd "$(dirname "$0")" || exit 1

NOKIA=https://nokiatech.github.io/heif/content
MPEG=https://media.githubusercontent.com/media/MPEGGroup/FileFormatConformance/main/data/file_features/published/heif

# md5 local-path url
LIST="
52821a5a9102213b2a71403890857c2e nokia/bird_burst.heic $NOKIA/image_sequences/bird_burst.heic
55a1e730cbff6a7dd37ebfebbd643eec nokia/rally_burst.heic $NOKIA/image_sequences/rally_burst.heic
6d68d8963d5bebc6606fb221d3853512 nokia/sea1_animation.heic $NOKIA/image_sequences/sea1_animation.heic
9ce9c29fda8d1137a6019cef204519e2 nokia/starfield_animation.heic $NOKIA/image_sequences/starfield_animation.heic
0359e432b6b748d8f3f0df762b622243 nokia/random_collection_1440x960.heic $NOKIA/images/random_collection_1440x960.heic
37582f6c31b45dbfc6debe50bb8e203b nokia/stereo_1200x800.heic $NOKIA/heifv2/stereo_1200x800.heic
491a9f70ec2a9fa0bb7ab382022f3c46 mpeg/C001.heic $MPEG/C001.heic
9c480ac1575aff16fd15a1d56c94524b mpeg/C026.heic $MPEG/C026.heic
3f8fafa597b2217dde3b86361ad006c4 mpeg/C027.heic $MPEG/C027.heic
ea65e127e15705757b264a0d214efb57 mpeg/C028.heic $MPEG/C028.heic
5787b5baae9add7381f95adcb2fcd698 mpeg/C029.heic $MPEG/C029.heic
627bb95fea634e550901dcbefbdc071f mpeg/C030.heic $MPEG/C030.heic
0cee19e4c7f1e564e067ab8b1ffc6c7e mpeg/C031.heic $MPEG/C031.heic
311491dbef2db7e8a7189aa96f88862f mpeg/C032.heic $MPEG/C032.heic
403f912c8aab1a6b57be37de94286c63 mpeg/C036.heic $MPEG/C036.heic
f7f154b2e983671f490a86e41fe6fa4b mpeg/C037.heic $MPEG/C037.heic
0ff80786c96ad83c5088419ce373c746 mpeg/C038.heic $MPEG/C038.heic
5ae9f234cab8ab2a96e9062c0f1723a3 mpeg/C041.heic $MPEG/C041.heic
"

mkdir -p samples/nokia samples/mpeg
echo "$LIST" | while read -r sum file url; do
	[ -z "$sum" ] && continue
	if [ -f "samples/$file" ] && [ "$(md5sum < "samples/$file" | cut -c1-32)" = "$sum" ]; then
		continue
	fi
	echo "fetching $file"
	if ! curl -sS -L --max-time 300 -o "samples/$file.part" "$url"; then
		echo "  FAILED to download $url"
		rm -f "samples/$file.part"
		continue
	fi
	if [ "$(md5sum < "samples/$file.part" | cut -c1-32)" != "$sum" ]; then
		echo "  $file does not have the expected MD5 sum: the source changed, or the download is incomplete"
		rm -f "samples/$file.part"
		continue
	fi
	mv "samples/$file.part" "samples/$file"
done
echo "$LIST" | while read -r sum file url; do
	[ -z "$sum" ] && continue
	[ -f "samples/$file" ] || echo "missing: samples/$file"
done
