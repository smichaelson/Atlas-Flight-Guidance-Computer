/** @file test_telemetry.c @brief Real telemetry transport under byte/packet faults.
 * Major function: main verifies erasure repair, CRC, ordering, expiry, restart,
 * pacing, counter definitions and fixed memory bounds without opening hardware. */
#include "atlas_telemetry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint32_t one[3]={1,2,3},two[3]={4,5,6},boot[2]={11,12};
static uint8_t packets[10][100];
static unsigned sent;
static AtlasStatus outcome=ATLAS_OK;
/** @brief Retain emitted packets. @param context Unused. @param b Frame. @param n Length. @return Status. */
static AtlasStatus capture(void *context,const uint8_t *b,size_t n)
{ (void)context;assert(n==100&&sent<10);memcpy(packets[sent++],b,n);return outcome; }
/** @brief Produce a deterministic checked batch. @param tx State. @param now Start time. */
static void batch(AtlasTelemetry *tx,uint32_t now)
{
    uint32_t words[128];for(unsigned i=0;i<128;++i)words[i]=i*UINT32_C(1234567);
    words[0]=ATLAS_TM_MAGIC;words[1]=now;
    assert(AtlasTelemetry_Begin(tx,words,now));words[10]=0; /* Verify copied ownership. */
    sent=0;for(unsigned i=0;i<10;++i)AtlasTelemetry_Service(tx,now+i*35U,capture,NULL);
    assert(sent==10);
}
/** @brief Feed a frame in arbitrary chunks. @param rx State. @param f Frame. @param chunk Fragment size. @param now Time. */
static void fragmented(AtlasTelemetry *rx,const uint8_t *f,unsigned chunk,uint32_t now)
{ for(unsigned pos=0;pos<100U;pos+=chunk)AtlasTelemetry_Feed(rx,f+pos,chunk<100U-pos?chunk:100U-pos,now); }
/** @brief Run all supported loss pairs and fragment boundaries. */
static void erasures(void)
{
    AtlasTelemetry tx,rx;AtlasTelemetry_Init(&tx,one,boot,0);batch(&tx,1000);
    for(int a=-1;a<10;++a)for(int b=a;b<10;++b)for(unsigned chunk=1;chunk<=100;chunk+=11){
        AtlasTelemetry_Init(&rx,two,boot,0);
        for(int i=9;i>=0;--i)if(i!=a&&i!=b)fragmented(&rx,packets[i],chunk,1500+(unsigned)(9-i));
        assert(rx.view.available&&rx.view.stats.good_batches==1);
        assert(!memcmp(rx.view.data,tx.tx,512));
        AtlasTelemetry_Service(&rx,3100,NULL,NULL);
        assert(rx.view.stats.expected_packets==10);
        assert(rx.view.stats.missing_packets==(unsigned)((a>=0)+(b>=0&&b!=a)));
    }
    for(unsigned cut=0;cut<=100;++cut){
        AtlasTelemetry_Init(&rx,two,boot,0);
        AtlasTelemetry_Feed(&rx,packets[0],cut,1100);
        AtlasTelemetry_Feed(&rx,packets[0]+cut,100-cut,1100);
        for(unsigned i=1;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,1200+i);
        assert(rx.view.available&&!memcmp(rx.view.data,tx.tx,512));
    }
}
/** @brief Check every damaged byte, every truncated prefix, and unrelated command text. */
static void corruption(void)
{
    AtlasTelemetry tx,rx;AtlasTelemetry_Init(&tx,one,boot,0);batch(&tx,1000);
    for(unsigned byte=0;byte<100;++byte)for(unsigned bit=0;bit<8;++bit){
        AtlasTelemetry_Init(&rx,two,boot,0);uint8_t damaged[100];memcpy(damaged,packets[2],100);damaged[byte]^=(uint8_t)(1U<<bit);
        AtlasTelemetry_Feed(&rx,damaged,100,1100);
        for(unsigned i=0;i<10;++i)if(i!=2U&&i!=5U)AtlasTelemetry_Feed(&rx,packets[i],100,1200+i);
        assert(rx.view.available&&!memcmp(rx.view.data,tx.tx,512));
        assert(rx.view.stats.recovered_packets==2);
    }
    for(unsigned prefix=0;prefix<100;++prefix){
        AtlasTelemetry_Init(&rx,two,boot,0);AtlasTelemetry_Feed(&rx,packets[0],prefix,1100);
        const uint8_t command[]="999 gpio 1\nradio ping\nstabilize on\nATLT noise";
        AtlasTelemetry_Feed(&rx,command,sizeof(command),1110);
        for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,1200+i);
        assert(rx.view.available&&!memcmp(rx.view.data,tx.tx,512));
    }
    AtlasTelemetry_Init(&rx,two,boot,0);
    for(unsigned i=0;i<7;++i)AtlasTelemetry_Feed(&rx,packets[i],100,1100+i);
    assert(!rx.view.available);AtlasTelemetry_Service(&rx,2600,NULL,NULL);
    assert(rx.view.stats.lost_batches==1&&rx.view.stats.missing_packets==3);
    for(unsigned i=7;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,2601);
    assert(!rx.view.available); /* A late final shard cannot resurrect an expired batch. */
    /* Passing the outer CRC still cannot bypass the end-to-end batch checksum. */
    AtlasTelemetry_Init(&rx,two,boot,0);
    packets[0][40]^=1U;
    uint32_t crc=AtlasTelemetry_Crc(packets[0],96U);
    for(unsigned i=0;i<4U;++i)packets[0][96U+i]=(uint8_t)(crc>>(8U*i));
    for(unsigned i=0;i<10U;++i)AtlasTelemetry_Feed(&rx,packets[i],100U,1100U+i);
    assert(!rx.view.available&&rx.view.stats.batch_crc_errors==1U&&rx.view.stats.lost_batches==1U);
    batch(&tx,2000U);AtlasTelemetry_Init(&rx,two,boot,0);
    packets[4][4]=2U;crc=AtlasTelemetry_Crc(packets[4],96U);
    for(unsigned i=0;i<4U;++i)packets[4][96U+i]=(uint8_t)(crc>>(8U*i));
    for(unsigned i=0;i<10U;++i)AtlasTelemetry_Feed(&rx,packets[i],100U,2100U+i);
    assert(rx.view.available&&rx.view.stats.header_errors==1U&&rx.view.stats.recovered_packets==1U);
    AtlasTelemetry_Init(&rx,one,boot,0);
    for(unsigned i=0;i<10U;++i)AtlasTelemetry_Feed(&rx,packets[i],100U,2100U+i);
    assert(!rx.view.available&&!rx.peer_known); /* Local echo never becomes a remote board. */
}
/** @brief Verify counters, wrap, session fencing, pacing, failure and staleness. */
static void lifecycle(void)
{
    AtlasTelemetry tx,rx;AtlasTelemetry_Init(&tx,one,boot,0);AtlasTelemetry_Init(&rx,two,boot,0);
    batch(&tx,1000);uint8_t old[10][100];memcpy(old,packets,sizeof(old));
    for(unsigned i=0;i<10;++i){AtlasTelemetry_Feed(&rx,packets[i],100,1100+i);AtlasTelemetry_Feed(&rx,packets[i],100,1120+i);}
    assert(rx.view.stats.duplicates==10&&rx.view.stats.expected_packets==10&&rx.view.stats.missing_packets==0);
    AtlasTelemetry_Service(&rx,2000,NULL,NULL);assert(rx.view.stats.rx_bps==1000&&rx.view.stats.payload_bps==256);
    tx.tx_sequence=3;batch(&tx,3000);
    for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,3200+i);
    assert(rx.view.stats.lost_batches==2&&rx.view.stats.missing_packets==20&&rx.view.stats.expected_packets==40);
    AtlasTelemetry_Service(&rx,7000,NULL,NULL);assert(!rx.view.available&&rx.view.stats.rx_bps>0);
    AtlasTelemetry_Service(&rx,8000,NULL,NULL);assert(rx.view.stats.rx_bps==0);
    const uint32_t reboot[2]={99,100};AtlasTelemetry_Init(&tx,one,reboot,0);batch(&tx,1000);
    for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,8100+i);
    assert(rx.view.available&&rx.view.stats.sessions==2);
    for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,old[i],100,8200+i);
    assert(rx.view.boot[0]==99&&rx.view.stats.old_packets==10);
    AtlasTelemetry_Init(&tx,one,boot,UINT32_MAX-1200U);AtlasTelemetry_Init(&rx,two,boot,UINT32_MAX-1200U);
    tx.tx_sequence=UINT32_MAX-1U;batch(&tx,UINT32_MAX-100U);
    for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,(uint32_t)(UINT32_MAX-10U+i*2U));
    batch(&tx,1000);for(unsigned i=0;i<10;++i)AtlasTelemetry_Feed(&rx,packets[i],100,1100+i);
    assert(rx.view.sequence==0&&rx.view.stats.missing_packets==0);
    outcome=ATLAS_ERROR_IO;batch(&tx,2000);assert(tx.view.stats.tx_errors==10);outcome=ATLAS_OK;
    uint32_t words[128]={ATLAS_TM_MAGIC};assert(AtlasTelemetry_Begin(&tx,words,3000));sent=0;
    AtlasTelemetry_Service(&tx,3000,capture,NULL);AtlasTelemetry_Service(&tx,3001,capture,NULL);assert(sent==1);
    AtlasTelemetry_Service(&tx,5000,capture,NULL);assert(sent==1&&tx.view.stats.tx_aborted==1);
    const uint32_t invalid[2]={0,0};AtlasTelemetry_Init(&tx,one,invalid,0);assert(!AtlasTelemetry_Due(&tx,9999));
    AtlasTelemetry_Init(&tx,one,boot,0);AtlasTelemetry_SetStreaming(&tx,false);
    assert(!AtlasTelemetry_Due(&tx,2000));AtlasTelemetry_SetStreaming(&tx,true);assert(AtlasTelemetry_Due(&tx,2000));
}
int main(void)
{
    assert(sizeof(AtlasTelemetry)<2500U);
    assert(AtlasTelemetry_Crc((const uint8_t *)"123456789",9)==UINT32_C(0xcbf43926));
    erasures();corruption();lifecycle();
    puts("PASS telemetry: all two-shard erasures, bit damage, fragmentation/truncation, expiry, reboot, wrap, loss/rates, pacing and bounded state");
    return 0;
}
