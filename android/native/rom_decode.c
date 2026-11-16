/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-layout decoder matching upstream BootROM.swift. No Apple bytes bundled. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lzfse.h"
#include "lzvn_decode_base.h"
#define ROM_BYTES 214904
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int naru_rom_decode(const uint8_t *input,size_t count,uint8_t *rom) {
    if(count!=859848)return -1;
    uint8_t *block=malloc(1048576);
    if(!block)return -1;
    int result=-1;
    if(lzfse_decode_buffer(block,1048576,input,count,NULL)!=1048576)goto done;
    const uint8_t *fork=block+876544;
    uint32_t table=word(fork);size_t used=0;
    if(table!=20)goto done; /* exactly four 64 KiB pieces, last one short */
    for(unsigned i=0;i<4;i++) {
        uint32_t start=word(fork+4*i),end=word(fork+4*(i+1));
        size_t want=ROM_BYTES-used;if(want>65536)want=65536;
        if(start<table||start>=end||end>116335)goto done;
        if(fork[start]==6) {
            if(end-start-1!=want)goto done;
            memcpy(rom+used,fork+start+1,want);
        } else {
            lzvn_decoder_state state={0};
            state.src=fork+start;state.src_end=fork+end;
            state.dst=state.dst_begin=rom+used;state.dst_end=rom+used+want;
            lzvn_decode(&state);
            if(state.dst!=state.dst_end)goto done;
        }
        used+=want;
    }
    result=used==ROM_BYTES?0:-1;
done:
    free(block);return result;
}
#ifdef __ANDROID__
#include <jni.h>
JNIEXPORT jbyteArray JNICALL Java_dev_henge_research_BootRom_decode(JNIEnv *env,jclass klass,jbyteArray bytes) {
    if((*env)->GetArrayLength(env,bytes)!=859848)return NULL;
    jbyte *input=(*env)->GetByteArrayElements(env,bytes,NULL);
    if(!input)return NULL;
    uint8_t *out=malloc(ROM_BYTES);jbyteArray result=NULL;
    if(out&&!naru_rom_decode((uint8_t *)input,859848,out)) {
        result=(*env)->NewByteArray(env,ROM_BYTES);
        if(result)(*env)->SetByteArrayRegion(env,result,0,ROM_BYTES,(jbyte *)out);
    }
    free(out);(*env)->ReleaseByteArrayElements(env,bytes,input,JNI_ABORT);return result;
}
#endif
