>[音视频文章汇总](https://www.jianshu.com/p/167b605add32)
##接到需求，做一个iOS和Android两端的编码测试工具，可选编码器，分辨率，帧率，码率控制ABR或CBR，GOP进行转码,查看软编码libx264和硬编码MediaCodec的编码效率和画质以及查看是否少帧，具体如下:

![0](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501163338.jpg)
![1.gif](https://upload-images.jianshu.io/upload_images/4193251-60f1c53402815759.gif?imageMogr2/auto-orient/strip)

Android效果图
![ffmepg_transcode](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501104013.jpg)
iOS效果图
![ios_ffmpeg_transcode](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501162021.jpg)
可以用ffmpeg自带的ffmpeg.c中的main函数来执行上面的所选参数，iOS端，ffmpeg是支持VideoToolBox硬编码h264和h265,直接传入所选参数即可执行，问题是Android端ffmpeg并不支持MediaCodec硬编码
###1.Android端，通过查看ffmpeg官网发现，ffmpeg只支持mediacodec硬解码，并不支持mediacodec硬编码，但目前Android手机是支持硬编码的，必须自己修改ffmpeg源码将MediaCodec硬编码添加到ffmpeg源码中，如何给ffmpeg添加codec呢？
![ffmpeg_mediacodec_wiki](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501101655.jpg)
查看[官网](https://wiki.multimedia.cx/index.php/FFmpeg_codec_HOWTO),大致分为五步
####A.查看libavcodec/avcodec.h中AVCodec结构体，知道我们新加的MediaCodec编码器有哪些属性,name,type,id,pix_fmts等
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501114213.jpg)
####B.编写自己的编码器MediaCodec,通过宏定义，取名h264_hlmediacodec,hevc_hlmediacodec分别代表h264和h265的编码器名称，根据此name可以找到编码器进行编码
```
// receive_packet modify to encode2
#define DECLARE_HLMEDIACODEC_ENC(short_name, full_name, codec_id, codec_type)                           \
    DECLARE_HLMEDIACODEC_VCLASS(short_name)                                                             \
    AVCodec ff_##short_name##_hlmediacodec_encoder = {                                                  \
        .name = #short_name "_hlmediacodec",                                                            \
        .long_name = full_name " (Ffmpeg MediaCodec NDK)",                                              \
        .type = codec_type,                                                                             \
        .id = codec_id,                                                                                 \
        .priv_class = &ff_##short_name##_hlmediacodec_enc_class,                                        \
        .priv_data_size = sizeof(HLMediaCodecEncContext),                                               \
        .init = hlmediacodec_encode_init,                                                               \
        .encode2 = hlmediacodec_encode_receive_packet,                                                  \
        .close = hlmediacodec_encode_close,                                                             \
        .capabilities = AV_CODEC_CAP_DELAY,                                                             \
        .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,                      \
        .pix_fmts = (const enum AVPixelFormat[]){AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE}, \
    };
#ifdef CONFIG_H264_HLMEDIACODEC_ENCODER
DECLARE_HLMEDIACODEC_ENC(h264, "H.264", AV_CODEC_ID_H264, AVMEDIA_TYPE_VIDEO)
#endif
#ifdef CONFIG_HEVC_HLMEDIACODEC_ENCODER
DECLARE_HLMEDIACODEC_ENC(hevc, "H.265", AV_CODEC_ID_HEVC, AVMEDIA_TYPE_VIDEO)
#endif
```
####C.libavcodec/avcodec.h中要有自己的编码器的id，上面传入的AV_CODEC_ID_H264，AV_CODEC_ID_HEVC在avcodec.h中本来就有
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501120516.jpg)
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501120552.jpg)
####D.libavcodec/allcodecs.c中导出新添加的编码器ff_h264_hlmediacodec_encoder,ff_hevc_hlmediacodec_encoder,这样获取所有的编码器能输出ff_h264_hlmediacodec_encoder和ff_hevc_hlmediacodec_encoder
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501121337.jpg)
####E.libavcodec/Makefile中添加新加的文件,编译到ffmpeg库中,编译的时候才会将这些新增的文件添加到ffmpeg库中
```
OBJS-$(CONFIG_HLMEDIACODEC)            += hlmediacodec.o hlmediacodec_codec.o
OBJS-$(CONFIG_AAC_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_MP3_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_H264_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_H264_HLMEDIACODEC_ENCODER) += hlmediacodec_enc.o
OBJS-$(CONFIG_HEVC_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_HEVC_HLMEDIACODEC_ENCODER) += hlmediacodec_enc.o
OBJS-$(CONFIG_MPEG4_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_VP8_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
OBJS-$(CONFIG_VP9_HLMEDIACODEC_DECODER) += hlmediacodec_dec.o
SKIPHEADERS-$(CONFIG_HLMEDIACODEC)     += hlmediacodec.h hlmediacodec_codec.h
```
###2.编译的时候可以直接执行原始脚本编译嘛？答案是不是能的，需要修改脚本，我们需要在configure中打开硬件加速和新增的MediaCodec编码器,并且在链接外部库中新增链接libmediandk.so，如果不添加，则会编译报错，找不到MediaCodec的库，
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
最后链接ndk中的libmediandk.so库文件，通过指定libmediandk.so库路径，这一步的实质是就是编译的时候再Mac环境下模拟出Android MediaCodec的硬编码环境#libmediandk.so路径
MEDIA_NDK_LIB=$TOOLCHAIN/sysroot/usr/lib/aarch64-linux-android/21
Android的模拟环境都在ndk路径下android-ndk-r20b，armv7和arm64分别对应不同的路径，这个涉及到Android脚本编译，后面再写，只有真正编译过一次才知道其对应关系
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220502164707.jpg)
```
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
--extra-cflags="-Os -fpic $OPTIMIZE_CFLAGS" \
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
X264_LIB_DIR=/Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/x264-snapshot-20191217-2245-stable/android/arm64-v8a;
FDK_AAC_LIB_DIR=/Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/fdk-aac-2.0.2/android/armv8-a;

#x264的头文件地址
X264_INC="$X264_LIB_DIR/include"
FDK_AAC_INC="$FDK_AAC_LIB_DIR/include"

#x264的静态库地址
X264_LIB="$X264_LIB_DIR/lib"
FDK_AAC_LIB="$FDK_AAC_LIB_DIR/lib"

#libmediandk.so路径
MEDIA_NDK_LIB=$TOOLCHAIN/sysroot/usr/lib/aarch64-linux-android/21

ADD_H264_FEATURE="--enable-gpl \
    --enable-libx264 \
    --enable-encoder=libx264 \
    --extra-cflags=-I$X264_INC $OPTIMIZE_CFLAGS \
    --extra-ldflags=-L$X264_LIB $ADDI_LDFLAGS "
    
ADD_FDK_AAC_FEATURE="--enable-libfdk-aac \
    --enable-nonfree \
    --extra-cflags=-I$FDK_AAC_INC $OPTIMIZE_CFLAGS \
    --extra-ldflags=-L$FDK_AAC_LIB $ADDI_LDFLAGS "
    
ADD_MEDIA_NDK_SO="--extra-ldflags=-L$MEDIA_NDK_LIB \
--extra-libs=-lmediandk "

#ADD_H264_FDK_AAC_FEATURE="--enable-encoder=aac \
#    --enable-decoder=aac \
#    --enable-gpl \
#    --enable-encoder=libx264 \
#    --enable-libx264 \
#    --enable-libfdk-aac \
#    --enable-encoder=libfdk-aac \
#    --enable-nonfree \
#    --extra-cflags=-I$X264_INC -I$FDK_AAC_INC \
#    --extra-ldflags=-lm -L$X264_LIB -L$FDK_AAC_LIB $ADDI_LDFLAGS "
#armv8-a
ARCH=aarch64
CPU=armv8-a
API=21
CC=$TOOLCHAIN/bin/aarch64-linux-android$API-clang
CXX=$TOOLCHAIN/bin/aarch64-linux-android$API-clang++
SYSROOT=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/sysroot
CROSS_PREFIX=$TOOLCHAIN/bin/aarch64-linux-android-
PREFIX=$(pwd)/android/$CPU
#OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfp -marm -march=$CPU "
build_android
```
执行脚本命令同时输出log文件方便排错```sh build_arm64.sh > /Users/cloud/Desktop/0.log```，编译成功生成.a静态库，我这儿是将armv7和arm64分开执行的，也分开合并成.so文件
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501202814.jpg)
执行合并.so的脚本union_ffmpeg_so_armv8.sh，将libx264,fdk-aac和ffmpeg中的.a合并为libffmpeg.so文件
```
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
```
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501203653.jpg)
同理再生成armv7架构的so，拖入到Andriod工程中，就可以执行通过ffmpeg执行Android硬编码了,比如具体命令
ffmpeg -i MyHeart.mp4 -c:a aac -c:v h264_hlmediacodec output.mp4
ffmpeg -i MyHeart.mp4 -c:a aac -c:v hevc_hlmediacodec output.mp4
ffmpeg -i MyHeart.mp4 -c:a aac -c:v libx264 output.mp4
当然后面可以添加更改分辨率，帧率，码率，gop,ABR和CBR的参数配置，不同的参数输出的结果不一致
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501204137.jpg)
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220501204533.jpg)
先将我编好的工程传到github上面，[Demo地址](https://github.com/yanyi0/FFmpegTranscoding/tree/master)
iOS的则简单些，直接打开开关编译ffmpeg即可,进行脚本编译，生成.a静态库,我编译的脚本是联合了libx264和fdk-aac
--enable-videotoolbox --enable-encoder=h264_videotoolbox --enable-encoder=hevc_videotoolbox
编译脚本如下
```
#!/bin/sh

# directories
FF_VERSION="4.2.2"
#FF_VERSION="snapshot-git"
if [[ $FFMPEG_VERSION != "" ]]; then
  FF_VERSION=$FFMPEG_VERSION
fi
SOURCE="ffmpeg-$FF_VERSION"
FAT="FFmpeg-iOS"

SCRATCH="scratch"
# must be an absolute path
THIN=`pwd`/"thin"

# absolute path to x264 library
X264=`pwd`/X264/x264-iOS

#FDK_AAC=`pwd`/../fdk-aac-build-script-for-iOS/fdk-aac-ios
FDK_AAC=`pwd`/FDK-AAC/fdk-aac-ios

CONFIGURE_FLAGS="--enable-cross-compile --enable-debug --disable-programs --disable-optimizations --disable-stripping \
                 --disable-doc --enable-pic --disable-asm --disable-yasm --enable-avresample \
                 --enable-videotoolbox --enable-encoder=h264_videotoolbox \
                 --enable-nonfree"

if [ "$X264" ]
then
	CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-gpl --enable-libx264"
fi

if [ "$FDK_AAC" ]
then
	CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-libfdk-aac --enable-nonfree"
fi

# avresample
#CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-avresample"

ARCHS="arm64 armv7"

COMPILE="y"
LIPO="y"

DEPLOYMENT_TARGET="8.0"

if [ "$*" ]
then
	if [ "$*" = "lipo" ]
	then
		# skip compile
		COMPILE=
	else
		ARCHS="$*"
		if [ $# -eq 1 ]
		then
			# skip lipo
			LIPO=
		fi
	fi
fi

if [ "$COMPILE" ]
then
	if [ ! `which yasm` ]
	then
		echo 'Yasm not found'
		if [ ! `which brew` ]
		then
			echo 'Homebrew not found. Trying to install...'
                        ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)" \
				|| exit 1
		fi
		echo 'Trying to install Yasm...'
		brew install yasm || exit 1
	fi
	if [ ! `which gas-preprocessor.pl` ]
	then
		echo 'gas-preprocessor.pl not found. Trying to install...'
		(curl -L https://github.com/libav/gas-preprocessor/raw/master/gas-preprocessor.pl \
			-o /usr/local/bin/gas-preprocessor.pl \
			&& chmod +x /usr/local/bin/gas-preprocessor.pl) \
			|| exit 1
	fi

	if [ ! -r $SOURCE ]
	then
		echo 'FFmpeg source not found. Trying to download...'
		curl http://www.ffmpeg.org/releases/$SOURCE.tar.bz2 | tar xj \
			|| exit 1
	fi

	CWD=`pwd`
	for ARCH in $ARCHS
	do
		echo "building $ARCH..."
		mkdir -p "$SCRATCH/$ARCH"
		cd "$SCRATCH/$ARCH"

		CFLAGS="-arch $ARCH"
		if [ "$ARCH" = "i386" -o "$ARCH" = "x86_64" ]
		then
		    PLATFORM="iPhoneSimulator"
		    CFLAGS="$CFLAGS -mios-simulator-version-min=$DEPLOYMENT_TARGET"
		else
		    PLATFORM="iPhoneOS"
		    CFLAGS="$CFLAGS -mios-version-min=$DEPLOYMENT_TARGET -fembed-bitcode"
		    if [ "$ARCH" = "arm64" ]
		    then
		        EXPORT="GASPP_FIX_XCODE5=1"
		    fi
		fi

		XCRUN_SDK=`echo $PLATFORM | tr '[:upper:]' '[:lower:]'`
		CC="xcrun -sdk $XCRUN_SDK clang"

		# force "configure" to use "gas-preprocessor.pl" (FFmpeg 3.3)
		if [ "$ARCH" = "arm64" ]
		then
		    AS="gas-preprocessor.pl -arch aarch64 -- $CC"
		else
		    AS="gas-preprocessor.pl -- $CC"
		fi

		CXXFLAGS="$CFLAGS"
		LDFLAGS="$CFLAGS"
		if [ "$X264" ]
		then
			CFLAGS="$CFLAGS -I$X264/include"
			LDFLAGS="$LDFLAGS -L$X264/lib"
		fi
		if [ "$FDK_AAC" ]
		then
			CFLAGS="$CFLAGS -I$FDK_AAC/include"
			LDFLAGS="$LDFLAGS -L$FDK_AAC/lib"
		fi

		TMPDIR=${TMPDIR/%\/} $CWD/$SOURCE/configure \
		    --target-os=darwin \
		    --arch=$ARCH \
		    --cc="$CC" \
		    --as="$AS" \
		    $CONFIGURE_FLAGS \
		    --extra-cflags="$CFLAGS" \
		    --extra-ldflags="$LDFLAGS" \
		    --prefix="$THIN/$ARCH" \
		|| exit 1

		make -j3 install $EXPORT || exit 1
		cd $CWD
	done
fi

if [ "$LIPO" ]
then
	echo "building fat binaries..."
	mkdir -p $FAT/lib
	set - $ARCHS
	CWD=`pwd`
	cd $THIN/$1/lib
	for LIB in *.a
	do
		cd $CWD
		echo lipo -create `find $THIN -name $LIB` -output $FAT/lib/$LIB 1>&2
		lipo -create `find $THIN -name $LIB` -output $FAT/lib/$LIB || exit 1
	done

	cd $CWD
	cp -rf $THIN/$1/include $FAT
fi

echo Done
```
[iOS的Demo地址](https://github.com/yanyi0/FFmpegTranscoding_iOS.git)
###3.我们如何知道我们新添加的编码器h264_hlmediacodec,hevc_hlmediacodec是否在ffmpeg中生效了呢？我们通过jni调用打印所有的编码器，看是否有Android mediacodec硬编码器,控制台会将所有的编码器打印出来，存在新增的编码器h264_hlmediacodec,hevc_hlmediacodec。
```
JNIEXPORT jstring JNICALL
Java_com_fish_ffmpegtranscoding_MainActivity_ffmpegInfo(JNIEnv *env, jobject  /* this */) {
    av_log_set_callback(log_callback_test2);
    char info[40000] = {0};
    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->decode != NULL) {
            sprintf(info, "%sdecode:", info);
        } else {
            sprintf(info, "%sencode:", info);
        }
        switch (c_temp->type) {
            case AVMEDIA_TYPE_VIDEO:
                sprintf(info, "%s(video):", info);
                break;
            case AVMEDIA_TYPE_AUDIO:
                sprintf(info, "%s(audio):", info);
                break;
            default:
                sprintf(info, "%s(other):", info);
                break;
        }
        if (strcmp(c_temp->name,"h264_hlmediacodec") == 0){
            sprintf(info, "%s[%s]\n", info, c_temp->name);
        }
        sprintf(info, "%s[%s]\n", info, c_temp->name);
        c_temp = c_temp->next;
    }
//    AVCodec *codec =avcodec_find_encoder_by_name("h264_hlmediacodec") ;
    return env->NewStringUTF(info);
    }
```
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220505112033.jpg)

###4.新加的Android编码器是如何在ffmpeg.c中生效的呢？我们执行ffmpeg -i in.mp4 -c:a aac -c:v h264_hlmediacodec -y output.mp4,ffmpeg是如何发现h264_hlmediacodec编码器的，先去查看iOS的硬编码VideoToolBox是如何工作的，三个函数init,encode2,close
> 1.编码器初始化init函数,为编码器硬编码做准备
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220502171706.jpg)

```
static av_cold int vtenc_init(AVCodecContext *avctx)
{
    VTEncContext    *vtctx = avctx->priv_data;
    CFBooleanRef    has_b_frames_cfbool;
    int             status;

    pthread_once(&once_ctrl, loadVTEncSymbols);

    pthread_mutex_init(&vtctx->lock, NULL);
    pthread_cond_init(&vtctx->cv_sample_sent, NULL);

    vtctx->session = NULL;
    status = vtenc_configure_encoder(avctx);
    if (status) return status;

    status = VTSessionCopyProperty(vtctx->session,
                                   kVTCompressionPropertyKey_AllowFrameReordering,
                                   kCFAllocatorDefault,
                                   &has_b_frames_cfbool);

    if (!status && has_b_frames_cfbool) {
        //Some devices don't output B-frames for main profile, even if requested.
        vtctx->has_b_frames = CFBooleanGetValue(has_b_frames_cfbool);
        CFRelease(has_b_frames_cfbool);
    }
    avctx->has_b_frames = vtctx->has_b_frames;
    return 0;
}
```
> 2.encode2函数,有4个参数,AVCodecContext *avctx表示当前编码器上下文，AVPacket *pkt表示一帧纯YUV数据编码后用pkt来接受H264文件,int *got_packet表示编码成功后将got_packet置为1，返回给发送方，发送下一帧YUV数据，若编码失败got_packet置为0，返回给发送方，编码失败停止发送
通过阅读源码查看这部分逻辑,同理，新增的videotoolboxenc.c也是一样的逻辑，传入纯YUV数据后，编码成功得到AVPacket
```
static av_cold int vtenc_frame(
    AVCodecContext *avctx,
    AVPacket       *pkt,
    const AVFrame  *frame,
    int            *got_packet)
```
![mediacodec](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220502185653.jpg)
编码成功后的AVPacket是如何回传到ffmpeg.c中的do_video_out方法的呢？继续往下看
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220502200206.jpg)
如下图,传入的frame和pkt的引用计数内部不用去管,外部ffmpeg自行去释放，pkt为栈变量，函数结束就释放了，frame则每有新一帧的时候去覆盖掉前一帧，前一帧的引用计数减一被释放
![mediacodec](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220504110029.jpg)

> 3.hlmediacodec_encode_close，编码完成，已经重新将编码后的数据写入容器，释放编码器上下文,内部的frame若有数据，则清空

```
static av_cold int hlmediacodec_encode_close(AVCodecContext *avctx)
{
    hi_logi(avctx, "hlmediacodec_encode_close %s %d", __FUNCTION__, __LINE__);

    HLMediaCodecEncContext *ctx = avctx->priv_data;
    ctx->stats.uint_stamp = av_gettime_relative();

    hlmediacodec_show_stats(avctx, ctx->stats);

    if (ctx->mediacodec)
    {
        AMediaCodec_stop(ctx->mediacodec);
        AMediaCodec_delete(ctx->mediacodec);
        ctx->mediacodec = NULL;
    }

    if (ctx->mediaformat)
    {
        AMediaFormat_delete(ctx->mediaformat);
        ctx->mediaformat = NULL;
    }

    if (ctx->frame)
    {
        av_frame_free(&ctx->frame);
        ctx->frame = NULL;
    }
    return 0;
}
```
若要添加编码参数，则在options中添加，不如码率模式CQ,VBR,CBR
```
static const AVOption ff_hlmediacodec_enc_options[] = {
    {"rc-mode", "The bitrate mode to use", OFFSET(rc_mode), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_BITRATE_MODE_VBR}, HLMEDIACODEC_BITRATE_MODE_CQ, HLMEDIACODEC_BITRATE_MODE_CBR, VE, "rc_mode"},
    {"cq", "Constant quality", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_CQ}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"vbr", "Variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_VBR}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"cbr", "Constant bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = HLMEDIACODEC_BITRATE_MODE_CBR}, INT_MIN, INT_MAX, VE, "rc_mode"},
    {"in_timeout", "in buff timeout", OFFSET(in_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"ou_timeout", "ou buff timeout", OFFSET(ou_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_OU_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"eof_timeout", "eof buff timeout", OFFSET(eof_timeout), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_EOF_SET_TIMEOUT_USEC}, HLMEDIACODEC_MIN_TIMEOUT_USEC, HLMEDIACODEC_MAX_TIMEOUT_USEC, VE},
    {"in_timeout_times", "in buff timeout times", OFFSET(in_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_IN_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VE},
    {"ou_timeout_times", "ou buff timeout times", OFFSET(ou_timeout_times), AV_OPT_TYPE_INT, {.i64 = HLMEDIACODEC_ENC_OU_SET_TIMEOUT_TIMES}, HLMEDIACODEC_MIN_TIMEOUT_TIMES, HLMEDIACODEC_MAX_TIMEOUT_TIMES, VE},
    {NULL},
};
```
修改后的添加了MediaCodec硬编码后的[ffmpeg_4.2.2版本源码下载地址](https://github.com/yanyi0/ffmpeg_4_2_2_mediacodec.git)
分别执行里面的build_arm64.sh脚本在android/armv8-a目录下生成arm64架构的.a静态库,通过执行合并脚本union_ffmpeg_so_armv8.sh,得到.so,这个.so文件就是最终我们可以用来在Android工程中去跑的ffmpeg命令行比如```ffmpeg -i MyHeartWillGoOn.mp4 -c:a aac -c:v h264_hlmediacodec output.mp4```,最后得到的mp4文件为使用mediacodec硬编码后的output.mp4文件
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220504155538.jpg)
```
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
```
同理执行build_armv7.sh脚本后会生成armv7架构的.a静态库，执行union_ffmpeg_so_armv7.sh合并生成.so,两种架构对应的ndk环境不一致
![](https://raw.githubusercontent.com/yanyi0/MWeb-Images/master/20220504163618.jpg)
```
echo "开始编译ffmpeg so"

#NDK路径.
export NDK=/Users/cloud/Library/android-ndk-r20b

PLATFORM=$NDK/platforms/android-21/arch-arm
TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64
TOOL=$NDK/toolchains/llvm/prebuilt/darwin-x86_64

PREFIX=$(pwd)

#如果不需要依赖x264，去掉/usr/x264/x264-master/android/armeabi-v7a/lib/libx264.a \就可以了

$TOOLCHAIN/bin/arm-linux-androideabi-ld \
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
    /Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/x264-snapshot-20191217-2245-stable/android/armeabi-v7a/lib/libx264.a \
    /Users/cloud/Documents/iOS/ego/FFmpeg/Android_sh/fdk-aac-2.0.2/android/armv7-a/lib/libfdk-aac.a \
    -lc -lm -lz -ldl -llog --dynamic-linker=/system/bin/linker \
    $TOOLCHAIN/lib/gcc/arm-linux-androideabi/4.9.x/libgcc.a \
    $TOOL/sysroot/usr/lib/arm-linux-androideabi/21/libmediandk.so \

echo "完成编译ffmpeg so"
```



