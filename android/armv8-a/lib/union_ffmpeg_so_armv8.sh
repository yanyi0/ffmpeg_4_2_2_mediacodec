echo "开始编译ffmpeg so"

#NDK路径.
export NDK=/Users/cloud/Library/android-ndk-r20b

PLATFORM=$NDK/platforms/android-21/arch-arm64
TOOLCHAIN=$NDK/toolchains/aarch64-linux-android-4.9/prebuilt/darwin-x86_64
TOOL=$NDK/toolchains/llvm/prebuilt/darwin-x86_64

PREFIX=$(pwd)

#如果不需要依赖x264，去掉/usr/x264/x264-master/android/armeabi-v7a/lib/libx264.a \就可以了

$TOOLCHAIN/bin/aarch64-linux-android-ld \
-rpath-link=$PLATFORM/usr/lib \
-L$PLATFORM/usr/lib \
-L$PREFIX/lib \
-soname libffmpeg.so -shared -nostdlib -Bsymbolic --whole-archive --no-undefined -o \
$PREFIX/libffmpeg.so \
    libavcodec.a \
    libavfilter.a \
    libswresample.a \
    libavformat.a \
    libavutil.a \
    libpostproc.a \
    libswscale.a \
    libavresample.a \
    libavdevice.a \
    /Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/x264-snapshot-20191217-2245-stable/android/arm64-v8a/lib/libx264.a \
    /Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/fdk-aac-2.0.2/android/armv8-a/lib/libfdk-aac.a \
    -lc -lm -lz -ldl -llog --dynamic-linker=/system/bin/linker \
    $TOOLCHAIN/lib/gcc/aarch64-linux-android/4.9.x/libgcc.a \
    $TOOL/sysroot/usr/lib/aarch64-linux-android/21/libmediandk.so \

echo "完成编译ffmpeg so"

