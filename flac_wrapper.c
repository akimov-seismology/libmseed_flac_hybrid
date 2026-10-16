/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
/* Memory adapter for unmodified libFLAC 1.5.0. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <FLAC/stream_encoder.h>
#include <FLAC/stream_decoder.h>

typedef struct { unsigned char *data; size_t capacity,position,size; } output_buffer;
static FLAC__StreamEncoderWriteStatus encoder_write(const FLAC__StreamEncoder *e,
    const FLAC__byte data[],size_t bytes,unsigned samples,unsigned frame,void *client) {
    (void)e;(void)samples;(void)frame;
    output_buffer *b=client;
    if (bytes>b->capacity-b->position) return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    memcpy(b->data+b->position,data,bytes);b->position+=bytes;
    if (b->position>b->size)b->size=b->position;
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}
static FLAC__StreamEncoderSeekStatus encoder_seek(const FLAC__StreamEncoder *e,
    FLAC__uint64 offset,void *client) {
    (void)e;output_buffer *b=client;
    if (offset>b->size)return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    b->position=(size_t)offset;return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}
static FLAC__StreamEncoderTellStatus encoder_tell(const FLAC__StreamEncoder *e,
    FLAC__uint64 *offset,void *client) {
    (void)e;*offset=((output_buffer*)client)->position;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static int encode_depth(const int32_t *input,int count,int block_size,int lpc_order,
                      int sample_rate,unsigned char *output,int capacity,unsigned bits,
                      FLAC__bool verify) {
    if(!input || !output || count<1 || block_size<16 || block_size>65535 || count>block_size || capacity<1)return -1;
    FLAC__StreamEncoder *e=FLAC__stream_encoder_new();if(!e)return -2;
    output_buffer b={output,(size_t)capacity,0,0};int result=-3;
    if(!FLAC__stream_encoder_set_compression_level(e,8) ||
       !FLAC__stream_encoder_set_streamable_subset(e,false) ||
       !FLAC__stream_encoder_set_channels(e,1) ||
       !FLAC__stream_encoder_set_bits_per_sample(e,bits) ||
       !FLAC__stream_encoder_set_sample_rate(e,(unsigned)sample_rate) ||
       !FLAC__stream_encoder_set_blocksize(e,(unsigned)block_size) ||
       !FLAC__stream_encoder_set_max_lpc_order(e,(unsigned)lpc_order) ||
       !FLAC__stream_encoder_set_verify(e,verify) ||
       !FLAC__stream_encoder_set_total_samples_estimate(e,(FLAC__uint64)count))goto done;
    if(FLAC__stream_encoder_init_stream(e,encoder_write,encoder_seek,encoder_tell,NULL,&b)
       !=FLAC__STREAM_ENCODER_INIT_STATUS_OK){result=-4;goto done;}
    if(!FLAC__stream_encoder_process_interleaved(e,input,(unsigned)count)){result=-5;goto done;}
    if(!FLAC__stream_encoder_finish(e)){result=-6;goto done;}
    /* libFLAC emits a vendor-only VORBIS_COMMENT even when no comments were
     * requested. Remove that metadata block from these independent streams.
     * Preserve STREAMINFO and any other metadata, and repair the last-block
     * flag. Frame bytes and the STREAMINFO MD5 remain unchanged. */
    {
        size_t readpos=4,writepos=4,last_kept=0;
        int last=0;
        if(b.size<4 || memcmp(output,"fLaC",4))goto done;
        while(!last){
            if(readpos>b.size || b.size-readpos<4)goto done;
            unsigned type=output[readpos]&127;
            size_t length=((size_t)output[readpos+1]<<16)|
                          ((size_t)output[readpos+2]<<8)|output[readpos+3];
            last=output[readpos]&128;
            if(length>b.size-readpos-4)goto done;
            if(type!=4){
                memmove(output+writepos,output+readpos,length+4);
                output[writepos]&=127;
                last_kept=writepos;
                writepos+=length+4;
            }
            readpos+=length+4;
        }
        if(!last_kept)goto done;
        output[last_kept]|=128;
        memmove(output+writepos,output+readpos,b.size-readpos);
        result=(int)(writepos+b.size-readpos);
    }
done:FLAC__stream_encoder_delete(e);return result;
}

typedef struct { const unsigned char *data; size_t length,position;
                 int32_t *output;size_t capacity,samples;int error;unsigned expected_rate,expected_bits;
                 const unsigned char *prefix;size_t prefix_length; } decode_buffer;
static FLAC__StreamDecoderReadStatus decoder_read(const FLAC__StreamDecoder *d,
    FLAC__byte data[],size_t *bytes,void *client) {
    (void)d;decode_buffer *b=client;
    if(!*bytes)return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    size_t remaining=b->length-b->position;
    if(!remaining){*bytes=0;return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;}
    if(*bytes>remaining)*bytes=remaining;
    /* Present synthetic STREAMINFO and the caller's frame as one stream,
     * without allocating and copying the complete padded payload. */
    size_t copied=0;
    if(b->position<b->prefix_length){
        copied=b->prefix_length-b->position;
        if(copied>*bytes)copied=*bytes;
        memcpy(data,b->prefix+b->position,copied);b->position+=copied;
    }
    if(copied<*bytes){
        memcpy(data+copied,b->data+b->position-b->prefix_length,*bytes-copied);
        b->position+=*bytes-copied;
    }
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}
static FLAC__StreamDecoderWriteStatus decoder_write(const FLAC__StreamDecoder *d,
    const FLAC__Frame *frame,const FLAC__int32 *const data[],void *client) {
    (void)d;decode_buffer *b=client;
    if(frame->header.channels!=1 || frame->header.bits_per_sample!=b->expected_bits ||
       (b->expected_rate && frame->header.sample_rate!=b->expected_rate) ||
       frame->header.blocksize>b->capacity-b->samples)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    memcpy(b->output+b->samples,data[0],frame->header.blocksize*sizeof(int32_t));
    b->samples+=frame->header.blocksize;return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
static void decoder_error(const FLAC__StreamDecoder *d,FLAC__StreamDecoderErrorStatus status,void *client) {
    (void)d;((decode_buffer*)client)->error=(int)status+1;
}
static FLAC__StreamDecoderTellStatus decoder_tell(const FLAC__StreamDecoder *d,
    FLAC__uint64 *position,void *client){
    (void)d;*position=((decode_buffer*)client)->position;
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

/* A compact record contains exactly one mono FLAC frame followed by
 * zero padding. Supply synthetic STREAMINFO; zero MD5 means unavailable. */
int flac_decode_frame(const unsigned char *input,int length,int32_t *output,
                      int count,int rate,int *consumed){
    if(!input || !output || !consumed || length<4 || count<1 || count>16384 || rate<1 || rate>655350)return -1;
    *consumed=0;
    /* Width is explicit in the FLAC frame. Code zero uses this
     * profile's implicit 32-bit precision; code three is reserved. */
    static const unsigned widths[8]={32,8,12,0,16,20,24,32};
    unsigned bits=widths[(input[3]>>1)&7];if(!bits)return -1;
    unsigned char stream[42]={0};
    memcpy(stream,"fLaC",4);stream[4]=128;stream[7]=34;
    unsigned block=count<32?32:(unsigned)count;
    stream[8]=stream[10]=(unsigned char)(block>>8);
    stream[9]=stream[11]=(unsigned char)block;
    uint64_t info=((uint64_t)rate<<44)|((uint64_t)(bits-1)<<36)|(unsigned)count;
    for(int i=25;i>=18;i--){stream[i]=(unsigned char)info;info>>=8;}
    FLAC__StreamDecoder *d=FLAC__stream_decoder_new();
    if(!d)return -2;
    decode_buffer b={input,(size_t)length+42,0,output,(size_t)count,0,0,(unsigned)rate,bits,stream,42};
    int result=-3;FLAC__uint64 position=0;
    if(FLAC__stream_decoder_init_stream(d,decoder_read,NULL,decoder_tell,NULL,NULL,
        decoder_write,NULL,decoder_error,&b)!=FLAC__STREAM_DECODER_INIT_STATUS_OK)goto done;
    if(!FLAC__stream_decoder_process_until_end_of_metadata(d))goto done;
    if(!FLAC__stream_decoder_process_single(d) || b.error || b.samples!=(size_t)count)goto done;
    if(!FLAC__stream_decoder_get_decode_position(d,&position) || position<=42 || position>b.length)goto done;
    *consumed=(int)position-42;
    if(!FLAC__stream_decoder_finish(d))goto done;
    result=count;
done:FLAC__stream_decoder_delete(d);return result;
}

int flac_encode_frame(const int32_t *input,int count,int block_size,int order,
                      int rate,unsigned char *output,int capacity){
    if(!input || !output || count<1 || capacity<1)return -1;
    static const unsigned widths[6]={8,12,16,20,24,32};
    unsigned bits=32;
    int32_t minimum=input[0],maximum=input[0];
    for(int i=1;i<count;i++){
        if(input[i]<minimum)minimum=input[i];
        if(input[i]>maximum)maximum=input[i];
    }
    for(unsigned w=0;w<6;w++){
        int64_t bound=(int64_t)1<<(widths[w]-1);
        if((int64_t)minimum>=-bound && (int64_t)maximum<bound){bits=widths[w];break;}
    }
    /* Trial encodings are discarded by the fitter. The selected record is
     * always decoded and compared in msflac_pack_record before writing it. */
    int size=encode_depth(input,count,block_size,order,rate,output,capacity,bits,false);
    if(size<0)return size;
    /* Model selection can change with width. Retain a 32-bit candidate as
     * fallback to compare both predictor/precision choices. */
    /* A constant subframe's stored value cannot become shorter when its
     * precision grows; avoid a redundant 32-bit trial for flat blocks. */
    if(bits<32 && minimum!=maximum){
        unsigned char *trial=malloc((size_t)capacity);if(!trial)return -2;
        int bytes=encode_depth(input,count,block_size,order,rate,trial,capacity,32,false);
        if(bytes<0){free(trial);return bytes;}
        if(bytes<size){memcpy(output,trial,(size_t)bytes);size=bytes;}
        free(trial);
    }
    if(size<42 || memcmp(output,"fLaC",4) || output[4]!=128 || output[7]!=34)return -1;
    memmove(output,output+42,(size_t)size-42);return size-42;
}
