/**
 * @file atlas_telemetry.c
 * @brief Fixed 100-byte packets, per-packet and batch CRC, systematic (10,8) erasure code.
 * Major functions: Feed resynchronizes and validates before assembly; Service paces
 * packets; repair recovers up to two lost/corrupted shards over GF(256), polynomial 0x11d.
 * CRC detects accidental corruption; identity and boot IDs are not authentication.
 */
#include "atlas_telemetry.h"
#include <string.h>

static const uint8_t tm_magic[4]={'A','T','L','T'};
/** @brief Decode little endian. @param p Four bytes. @return Word. */
static uint32_t rd(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U); }
/** @brief Encode little endian. @param p Four bytes. @param n Word. */
static void wr(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4U;++i)p[i]=(uint8_t)(n>>(8U*i)); }
/** @brief Saturate health counters rather than wrapping. @param p Counter. @param n Addition. */
static void add(uint32_t *p,uint64_t n) { *p=n>UINT32_MAX-*p?UINT32_MAX:*p+(uint32_t)n; }
/** @brief Count received shard bits. @param n Bitmap. @return Count. */
static unsigned bits(uint16_t n) { unsigned count=0;while(n){count+=n&1U;n>>=1U;}return count; }
/** @brief GF(256) multiplication. @param a Operand. @param b Operand. @return Product. */
static uint8_t mul(uint8_t a,uint8_t b)
{
    uint8_t result=0;
    while(b){if(b&1U)result^=a;b>>=1U;a=(uint8_t)((a<<1U)^((a&128U)?0x1dU:0U));}
    return result;
}
/** @brief GF inverse for nonzero denominator. @param a Element. @return Inverse. */
static uint8_t inv(uint8_t a) { uint8_t r=1U;for(unsigned i=0;i<254U;++i)r=mul(r,a);return r; }
/** @brief Coefficient 2^index. @param index Data shard index. @return Coefficient. */
static uint8_t coefficient(unsigned index) { return (uint8_t)(1U<<index); }
/** @brief Compute CRC. @param data Bytes. @param length Count. @return CRC. */
uint32_t AtlasTelemetry_Crc(const uint8_t *data,size_t length)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<length;++i){c^=data[i];for(unsigned b=0;b<8U;++b)c=(c>>1U)^((c&1U)?UINT32_C(0xedb88320):0U);}
    return ~c;
}
/** @brief Initialize passive parser and paced sender. @param t State. @param uid Identity.
 * @param boot Boot identifier. @param now Local time. */
void AtlasTelemetry_Init(AtlasTelemetry *t,const uint32_t uid[3],const uint32_t boot[2],uint32_t now)
{
    memset(t,0,sizeof(*t));memcpy(t->uid,uid,12U);memcpy(t->boot,boot,8U);
    t->view.tx_ready=(boot[0]|boot[1])!=0U;t->tx_index=ATLAS_TM_SHARDS;t->rate_at=now;
    t->view.streaming=t->view.tx_ready;
    t->tx_began=now;t->tx_at=now;
}
/** @brief Check bounded sender availability. @param t State. @param now Time. @return Due. */
bool AtlasTelemetry_Due(const AtlasTelemetry *t,uint32_t now)
{ return t->view.streaming && t->tx_index==ATLAS_TM_SHARDS && (uint32_t)(now-t->tx_began)>=ATLAS_TM_INTERVAL_MS; }
/** @brief Deliberately pause local transmission. @param t State. @param enabled Broadcast. */
void AtlasTelemetry_SetStreaming(AtlasTelemetry *t,bool enabled)
{
    t->view.streaming=enabled&&t->view.tx_ready;
    if(!t->view.streaming&&t->tx_index<ATLAS_TM_SHARDS){
        t->tx_index=ATLAS_TM_SHARDS;add(&t->view.stats.tx_aborted,1U);
    }
}
/** @brief Start a copied batch. @param t State. @param words Sample. @param now Time. @return Accepted. */
bool AtlasTelemetry_Begin(AtlasTelemetry *t,const uint32_t words[ATLAS_TM_WORDS],uint32_t now)
{
    if(!AtlasTelemetry_Due(t,now) || words[TM_MAGIC]!=ATLAS_TM_MAGIC)return false;
    for(unsigned i=0;i<TM_CRC;++i)wr(t->tx+4U*i,words[i]);
    wr(t->tx+508U,AtlasTelemetry_Crc(t->tx,508U));
    ++t->tx_sequence;t->tx_began=now;t->tx_at=now-35U;t->tx_index=0U;
    add(&t->view.stats.tx_batches,1U);return true;
}
/** @brief Finalize expected-versus-received counts once. @param t State. */
static void close_batch(AtlasTelemetry *t)
{
    if(!t->active || t->closed)return;
    add(&t->view.stats.expected_packets,ATLAS_TM_SHARDS);
    add(&t->view.stats.missing_packets,ATLAS_TM_SHARDS-bits(t->seen));
    if(!t->delivered)add(&t->view.stats.lost_batches,1U);
    t->closed=true;
}
/** @brief Repair up to two missing data shards, then check complete snapshot integrity.
 * @param t State. @param now Time. */
static void repair(AtlasTelemetry *t,uint32_t now)
{
    unsigned missing[2]={0U,0U},count=0;
    for(unsigned i=0;i<ATLAS_TM_DATA_SHARDS;++i)if(!(t->seen&(1U<<i))){if(count==2U)return;missing[count++]=i;}
    const bool p=(t->seen&256U)!=0U,q=(t->seen&512U)!=0U;
    if((count==2U&&(!p||!q))||(count==1U&&!p&&!q))return;
    const uint8_t a=coefficient(missing[0]),c=coefficient(missing[1]);
    const uint8_t inverse=count==0U?1U:count==2U?inv((uint8_t)(a^c)):inv(a);
    for(unsigned b=0;b<ATLAS_TM_SHARD_BYTES&&count;++b){
        uint8_t x=t->parity[0][b],y=t->parity[1][b];
        for(unsigned i=0;i<ATLAS_TM_DATA_SHARDS;++i)if(t->seen&(1U<<i)){
            x^=t->assembly[i*ATLAS_TM_SHARD_BYTES+b];
            y^=mul(coefficient(i),t->assembly[i*ATLAS_TM_SHARD_BYTES+b]);
        }
        if(count==1U)t->assembly[missing[0]*ATLAS_TM_SHARD_BYTES+b]=p?x:mul(y,inverse);
        else {
            const uint8_t first=mul((uint8_t)(y^mul(c,x)),inverse);
            t->assembly[missing[0]*ATLAS_TM_SHARD_BYTES+b]=first;
            t->assembly[missing[1]*ATLAS_TM_SHARD_BYTES+b]=(uint8_t)(first^x);
        }
    }
    if(rd(t->assembly)!=ATLAS_TM_MAGIC || rd(t->assembly+508U)!=AtlasTelemetry_Crc(t->assembly,508U)){
        if(!t->crc_failed)add(&t->view.stats.batch_crc_errors,1U);
        t->crc_failed=true;return;
    }
    memcpy(t->view.data,t->assembly,ATLAS_TM_BYTES);t->view.sequence=t->rx_sequence;
    t->view.received_ms=now;t->view.assembly_ms=(uint32_t)(now-t->rx_began);t->view.available=1U;t->delivered=true;
    add(&t->view.stats.good_batches,1U);add(&t->rate_payload,ATLAS_TM_BYTES);
    if(count){add(&t->view.stats.recovered_batches,1U);add(&t->view.stats.recovered_packets,count);}
}
/** @brief Accept only CRC-checked packets from one nonlocal identity. @param t State. @param now Time. */
static void accept(AtlasTelemetry *t,uint32_t now)
{
    if(t->active&&!t->closed&&(uint32_t)(now-t->rx_began)>=ATLAS_TM_EXPIRY_MS)close_batch(t);
    const uint8_t *f=t->parser;uint32_t uid[3],boot[2];
    for(unsigned i=0;i<3U;++i)uid[i]=rd(f+8U+4U*i);
    for(unsigned i=0;i<2U;++i)boot[i]=rd(f+20U+4U*i);
    if(!(uid[0]|uid[1]|uid[2])||!(boot[0]|boot[1])||!memcmp(uid,t->uid,12U))return;
    if(t->peer_known&&memcmp(uid,t->view.peer,12U)){add(&t->view.stats.foreign_packets,1U);return;}
    if(!t->peer_known||memcmp(boot,t->view.boot,8U)){
        for(unsigned i=0;i<4U;++i)if(!memcmp(boot,t->retired[i],8U)){add(&t->view.stats.old_packets,1U);return;}
        close_batch(t);
        if(t->peer_known){memcpy(t->retired[t->retired_next++%4U],t->view.boot,8U);}
        memcpy(t->view.peer,uid,12U);memcpy(t->view.boot,boot,8U);t->peer_known=true;
        t->active=false;t->view.available=0U;t->view.sequence=0U;
        memset(t->view.data,0,sizeof(t->view.data));add(&t->view.stats.sessions,1U);
    }
    const uint32_t sequence=rd(f+28U);
    if(t->active&&sequence!=t->rx_sequence){
        const uint32_t delta=sequence-t->rx_sequence;
        if(delta>=UINT32_C(0x80000000)){add(&t->view.stats.old_packets,1U);return;}
        close_batch(t);
        add(&t->view.stats.expected_packets,(uint64_t)(delta-1U)*ATLAS_TM_SHARDS);
        add(&t->view.stats.missing_packets,(uint64_t)(delta-1U)*ATLAS_TM_SHARDS);
        add(&t->view.stats.lost_batches,delta-1U);t->active=false;
    }
    if(!t->active){
        t->active=true;t->closed=t->delivered=t->crc_failed=false;t->seen=0;t->rx_sequence=sequence;t->rx_began=now;
        memset(t->assembly,0,sizeof(t->assembly));memset(t->parity,0,sizeof(t->parity));
    }
    const unsigned index=f[6];const uint16_t bit=(uint16_t)(1U<<index);
    if(t->seen&bit){add(&t->view.stats.duplicates,1U);return;}
    if(t->closed){add(&t->view.stats.old_packets,1U);return;}
    t->seen|=bit;
    if(index<ATLAS_TM_DATA_SHARDS)memcpy(t->assembly+index*ATLAS_TM_SHARD_BYTES,f+32U,ATLAS_TM_SHARD_BYTES);
    else memcpy(t->parity[index-ATLAS_TM_DATA_SHARDS],f+32U,ATLAS_TM_SHARD_BYTES);
    if(!t->delivered)repair(t,now);
    if(bits(t->seen)==ATLAS_TM_SHARDS)close_batch(t);
}
/** @brief Sliding framed parser, no dynamic allocation. @param t State. @param bytes Input.
 * @param length Count. @param now Time. */
void AtlasTelemetry_Feed(AtlasTelemetry *t,const uint8_t *bytes,size_t length,uint32_t now)
{
    for(size_t i=0;i<length;++i){
        t->parser[t->used++]=bytes[i];
        while(t->used&&memcmp(t->parser,tm_magic,t->used<4U?t->used:4U)){
            --t->used;memmove(t->parser,t->parser+1U,t->used);
        }
        if(t->used!=ATLAS_TM_FRAME_BYTES)continue;
        const bool crc=rd(t->parser+96U)==AtlasTelemetry_Crc(t->parser,96U);
        const bool header=t->parser[4]==1U&&t->parser[5]==1U&&t->parser[6]<ATLAS_TM_SHARDS&&t->parser[7]==ATLAS_TM_DATA_SHARDS;
        if(crc&&header){
            add(&t->view.stats.rx_packets,1U);add(&t->view.stats.rx_bytes,ATLAS_TM_FRAME_BYTES);
            add(&t->rate_packets,1U);add(&t->rate_bytes,ATLAS_TM_FRAME_BYTES);
            accept(t,now);t->used=0;
        }else{
            if(!crc)add(&t->view.stats.crc_errors,1U);else add(&t->view.stats.header_errors,1U);
            --t->used;memmove(t->parser,t->parser+1U,t->used);
        }
    }
}
/** @brief Deadlines and bounded paced transmission. @param t State. @param now Time.
 * @param send Callback or NULL. @param context Callback argument. */
void AtlasTelemetry_Service(AtlasTelemetry *t,uint32_t now,AtlasRadioSend send,void *context)
{
    if(t->view.available&&(uint32_t)(now-t->view.received_ms)>=ATLAS_TM_FRESH_MS)t->view.available=0U;
    if(t->active&&!t->closed&&(uint32_t)(now-t->rx_began)>=ATLAS_TM_EXPIRY_MS)close_batch(t);
    if(t->tx_index<ATLAS_TM_SHARDS&&(uint32_t)(now-t->tx_began)>=ATLAS_TM_INTERVAL_MS){
        t->tx_index=ATLAS_TM_SHARDS;add(&t->view.stats.tx_aborted,1U);
    }
    const uint32_t elapsed=now-t->rate_at;
    if(elapsed>=1000U){
        t->view.stats.rx_bps=(uint32_t)((uint64_t)t->rate_bytes*1000U/elapsed);
        t->view.stats.payload_bps=(uint32_t)((uint64_t)t->rate_payload*1000U/elapsed);
        t->view.stats.rx_pps=(uint32_t)((uint64_t)t->rate_packets*1000U/elapsed);
        t->rate_at=now;t->rate_bytes=t->rate_packets=t->rate_payload=0U;
    }
    if(!send||t->tx_index>=ATLAS_TM_SHARDS||(uint32_t)(now-t->tx_at)<35U)return;
    uint8_t f[ATLAS_TM_FRAME_BYTES]={0};memcpy(f,tm_magic,4U);
    f[4]=1U;f[5]=1U;f[6]=t->tx_index;f[7]=ATLAS_TM_DATA_SHARDS;
    for(unsigned i=0;i<3U;++i)wr(f+8U+4U*i,t->uid[i]);
    for(unsigned i=0;i<2U;++i)wr(f+20U+4U*i,t->boot[i]);
    wr(f+28U,t->tx_sequence);
    if(t->tx_index<ATLAS_TM_DATA_SHARDS)memcpy(f+32U,t->tx+t->tx_index*ATLAS_TM_SHARD_BYTES,ATLAS_TM_SHARD_BYTES);
    else for(unsigned i=0;i<ATLAS_TM_DATA_SHARDS;++i)for(unsigned b=0;b<ATLAS_TM_SHARD_BYTES;++b)
        f[32U+b]^=t->tx_index==8U?t->tx[i*ATLAS_TM_SHARD_BYTES+b]:mul(coefficient(i),t->tx[i*ATLAS_TM_SHARD_BYTES+b]);
    wr(f+96U,AtlasTelemetry_Crc(f,96U));t->tx_at=now;++t->tx_index;
    if(send(context,f,sizeof(f))==ATLAS_OK){add(&t->view.stats.tx_packets,1U);add(&t->view.stats.tx_bytes,sizeof(f));}
    else add(&t->view.stats.tx_errors,1U);
}
