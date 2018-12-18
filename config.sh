#!/bin/sh

INSTALL=$PWD/install

ffmpeg_build_func(){

mkdir -p ${INSTALL}

export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$PWD/fdk_aac/install/lib/pkgconfig:/opt/intel/mediasdk/pkgconfig

echo "test1"

CONFIG="
./configure \
  --enable-version3 \
  --prefix="${INSTALL}" \
  --pkg-config-flags="--static" \
  --extra-cflags='-I$PWD/fdk_aac/install/include -I/opt/intel/mediasdk/include' \
  --extra-ldflags='-L$PWD/fdk_aac/install/lib -L/opt/intel/mediasdk/lib/lin_x64' \
  --extra-libs='-lpthread -lm -lmfx' \
  --bindir='${INSTALL}/bin' \
  --enable-gpl \
  --enable-libmfx \
  --enable-libfdk_aac \
  --disable-libfreetype \
  --enable-nonfree
";

echo $CONFIG
eval$CONFIG

echo "test2"
make -j 8

echo "test3"
make install

}

ffmpeg_clean_func(){

make clean
make distclean

}

if test $# -eq 0
then
        echo 'ffmpeg  config & build & install ... '
        ffmpeg_build_func
else
        if [ $1 == 'clean' ]
        then
                echo 'ffmpeg clean !!!'
                ffmpeg_clean_func
        fi
fi

