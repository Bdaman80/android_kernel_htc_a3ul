#!/bin/bash
rm output/zip/system/lib/modules/*.ko
rm output/zip/*.zip
rm -rf output/working/extracted
rm output/zip/boot.img

make clean
make mrproper
make a3ul_defconfig
time make -j16
FILE=arch/arm/boot/zImage

if [ -f $FILE ];
then
mkdir output
mkdir output/working
mkdir output/zip
mkdir output/zip/system
mkdir output/zip/system/lib
mkdir output/zip/system/lib/modules
scripts/mkboot output/working/stockboot.img output/working/extracted
cp arch/arm/boot/zImage output/working/extracted/zImage
scripts/dtbTool -s 2048 -d "htc,project-id = <" -o output/working/extracted/dt.img -p scripts/dtc/ arch/arm/boot/
scripts/mkboot output/working/extracted output/zip/boot.img
find . -path ./output -prune -o -name '*.ko' -exec cp {} output/zip/system/lib/modules/  \;
mv output/zip/system/lib/modules/exfat.ko output/zip/system/lib/modules/texfat.ko
git log --oneline --decorate > output/zip/META-INF/com/google/android/aroma/changelogs.txt
sed -i 's/(HEAD, master)/" "/g' output/zip/META-INF/com/google/android/aroma/changelogs.txt
sed -i 's/\(origin\/master, origin\/HEAD\)/" "/g' output/zip/META-INF/com/google/android/aroma/changelogs.txt
cd output/zip
NOW=$(date +"%m-%d-%y")
zip -r -q --exclude=*zip* JmzA3_Kernel-"$NOW".zip * && echo "Success" || echo "Failure"
else
echo "Something went wrong"
fi
