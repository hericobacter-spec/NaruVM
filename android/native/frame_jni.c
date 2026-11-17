#include "frame_transport.h"
#include <jni.h>
#include <android/bitmap.h>
#include <stdlib.h>
#include <time.h>
#include "pixel_convert.h"
typedef struct Reader { LabFrames *frames; LabFrameView view; int pending; uint64_t age_ns; } Reader;
#define JNI(name) Java_dev_henge_research_NativeFrames_##name
JNIEXPORT jlong JNICALL JNI(open)(JNIEnv *env, jclass type, jstring path, jlong epoch) {
    if (!path || epoch < 0) return 0;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) return 0;
    LabFrames *frames = lab_frames_open(p, (uint64_t)epoch);
    (*env)->ReleaseStringUTFChars(env, path, p);
    if (!frames) return 0;
    Reader *r = calloc(1, sizeof *r);
    if (!r) { lab_frames_close(frames); return 0; }
    r->frames = frames; return (jlong)(uintptr_t)r;
}
JNIEXPORT jlong JNICALL JNI(size)(JNIEnv *env, jclass type, jlong handle) {
    Reader *r = (Reader *)(uintptr_t)handle;
    if (!r) return 0;
    if (!r->pending) r->pending = lab_frames_take(r->frames, &r->view) == 1;
    return r->pending ? ((jlong)r->view.width << 32) | r->view.height : 0;
}
JNIEXPORT jlong JNICALL JNI(copy)(JNIEnv *env, jclass type, jlong handle, jobject bitmap) {
    Reader *r = (Reader *)(uintptr_t)handle;
    if (!r || !r->pending || !bitmap) return 0;
    AndroidBitmapInfo info;
    void *destination = NULL;
    jlong result = -1;
    if (AndroidBitmap_getInfo(env, bitmap, &info) == ANDROID_BITMAP_RESULT_SUCCESS &&
            info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 && info.width == r->view.width &&
            info.height == r->view.height && info.stride >= info.width * 4 &&
            AndroidBitmap_lockPixels(env, bitmap, &destination) == ANDROID_BITMAP_RESULT_SUCCESS) {
        lab_bgra_to_rgba(destination,info.stride,r->view.bgra,(size_t)info.width*4,info.width,info.height);
        if (AndroidBitmap_unlockPixels(env, bitmap) == ANDROID_BITMAP_RESULT_SUCCESS) {
            result = (jlong)r->view.sequence;
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            r->age_ns = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec - r->view.published_ns;
        }
    }
    lab_frames_release(r->frames, &r->view); r->pending = 0; return result;
}
JNIEXPORT jlong JNICALL JNI(copyBuffer)(JNIEnv *env, jclass type, jlong handle, jobject buffer) {
    Reader *r=(Reader *)(uintptr_t)handle;
    if (!r || !r->pending || !buffer) return 0;
    uint8_t *destination=(*env)->GetDirectBufferAddress(env,buffer);
    jlong capacity=(*env)->GetDirectBufferCapacity(env,buffer), result=-1;
    size_t stride=(size_t)r->view.width*4, bytes=stride*r->view.height;
    if (destination && capacity>=0 && (uint64_t)capacity>=bytes) {
        lab_bgra_to_rgba(destination,stride,r->view.bgra,stride,r->view.width,r->view.height);
        result=(jlong)r->view.sequence;
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        r->age_ns=(uint64_t)now.tv_sec*1000000000+now.tv_nsec-r->view.published_ns;
    }
    lab_frames_release(r->frames,&r->view); r->pending=0; return result;
}
JNIEXPORT jlong JNICALL JNI(ageNs)(JNIEnv *env, jclass type, jlong handle) {
    Reader *r = (Reader *)(uintptr_t)handle; return r ? (jlong)r->age_ns : 0;
}
JNIEXPORT void JNICALL JNI(close)(JNIEnv *env, jclass type, jlong handle) {
    Reader *r = (Reader *)(uintptr_t)handle;
    if (r) { lab_frames_close(r->frames); free(r); }
}
