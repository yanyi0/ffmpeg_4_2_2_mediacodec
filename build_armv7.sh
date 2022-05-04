#!/bin/bash

echo ">>>>>>>>> 编译ffmpeg <<<<<<<<"

#NDK路径.
export NDK=/Users/cloud/Library/android-ndk-r20b
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/darwin-x86_64

#如果只需要单独的ffmpeg，不需要依赖x264，去掉$ADD_H264_FEATURE这句就可以了；
#如果你需要的是动态库，--enable-static 改为 --disable-static，--disable-shared 改为 --enable-shared

function build_android
{

echo "开始编译 $CPU"

./configure \
--prefix=$PREFIX \
--enable-neon  \
--enable-mediacodec \
--enable-hlmediacodec \
--enable-hwaccels  \
--enable-decoder=h264_mediacodec \
--enable-encoder=h264_mediacodec \
--enable-decoder=hevc_mediacodec \
--enable-decoder=mpeg4_mediacodec \
--enable-encoder=mpeg4_mediacodec \
--enable-hwaccel=h264_mediacodec \
--enable-encoder=h264_hlmediacodec \
--enable-gpl   \
--enable-postproc \
--enable-avresample \
--enable-avdevice \
--enable-pic \
--disable-shared \
--enable-debug \
--disable-yasm \
--enable-zlib \
--disable-bzlib \
--disable-iconv \
--disable-optimizations \
--disable-stripping \
--enable-small \
--enable-jni \
--enable-static \
--disable-doc \
--enable-ffmpeg \
--enable-ffplay \
--enable-ffprobe \
--disable-doc \
--disable-symver \
--cross-prefix=$CROSS_PREFIX \
--target-os=android \
--arch=$ARCH \
--cpu=$CPU \
--cc=$CC \
--cxx=$CXX \
--enable-cross-compile \
--sysroot=$SYSROOT \
--extra-cflags="-O0 -fpic $OPTIMIZE_CFLAGS" \
--extra-ldflags="$ADDI_LDFLAGS" \
$ADD_H264_FEATURE \
$ADD_FDK_AAC_FEATURE \
$ADD_MEDIA_NDK_SO


make clean
make -j8
make install

echo "编译完成 $CPU"

}

#x264库所在的位置，ffmpeg 需要链接 x264
X264_LIB_DIR=/Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/x264-snapshot-20191217-2245-stable/android/armeabi-v7a;
FDK_AAC_LIB_DIR=/Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/fdk-aac-2.0.2/android/armv7-a;

#x264的头文件地址
X264_INC="$X264_LIB_DIR/include"
FDK_AAC_INC="$FDK_AAC_LIB_DIR/include"

#x264的静态库地址
X264_LIB="$X264_LIB_DIR/lib"
FDK_AAC_LIB="$FDK_AAC_LIB_DIR/lib"

#libmediandk.so路径
MEDIA_NDK_LIB=$TOOLCHAIN/sysroot/usr/lib/arm-linux-androideabi/21

ADD_H264_FEATURE="--enable-gpl \
    --enable-libx264 \
    --enable-encoder=libx264 \
    --extra-cflags=-I$X264_INC $OPTIMIZE_CFLAGS \
    --extra-ldflags=-L$X264_LIB $ADDI_LDFLAGS "
    
ADD_FDK_AAC_FEATURE="--enable-libfdk-aac \
    --enable-nonfree \
    --extra-cflags=-I$FDK_AAC_INC $OPTIMIZE_CFLAGS \
    --extra-ldflags=-L$FDK_AAC_LIB $ADDI_LDFLAGS "
   
#ADD_MEDIA_NDK_SO="--extra-ldflags= -lmediandk \
#        -L$MEDIA_NDK_LIB $ADDI_LDFLAGS "
ADD_MEDIA_NDK_SO="--extra-ldflags=-L$MEDIA_NDK_LIB \
--extra-libs=-lmediandk "

#APP_PLATFORM := android-21
#MEDIA_NDK_LIB    += -lmediandk
#MEDIA_NDK_LIB += libmediandk
#armv7-a
ARCH=arm
CPU=armv7-a
API=21
CC=$TOOLCHAIN/bin/armv7a-linux-androideabi$API-clang
CXX=$TOOLCHAIN/bin/armv7a-linux-androideabi$API-clang++
SYSROOT=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/sysroot
CROSS_PREFIX=$TOOLCHAIN/bin/arm-linux-androideabi-
PREFIX=$(pwd)/android/$CPU
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfp -marm -march=$CPU "

build_android

