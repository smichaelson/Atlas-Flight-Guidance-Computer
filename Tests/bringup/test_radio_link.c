/** @file test_radio_link.c @brief Exercise real radio framing and state against two inert peers.
 * Major tests cover fragmentation, corruption, echoes, mismatched/late responses,
 * simultaneous challenges, wraparound, timeouts, reconnects and bounded traffic. */
#include "atlas_radio_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[ATLAS_RADIO_FRAME_SIZE]; unsigned calls; AtlasStatus status; } Wire;
static const uint32_t uid_a[3]={3342386U,859001093U,943273521U};
static const uint32_t uid_b[3]={3407904U,859001093U,943273521U};
static AtlasStatus send(void *context,const uint8_t *bytes,size_t length)
{
    Wire *wire=context; assert(length==sizeof(wire->bytes));
    memcpy(wire->bytes,bytes,length); ++wire->calls; return wire->status;
}
static void split(AtlasRadioLink *link,const uint8_t *bytes,uint32_t now,size_t at)
{
    AtlasRadioLink_Feed(link,bytes,at,now);
    AtlasRadioLink_Feed(link,bytes+at,ATLAS_RADIO_FRAME_SIZE-at,now+1U);
}
static AtlasRadioLinkSnapshot snapshot(AtlasRadioLink *link,uint32_t now)
{
    AtlasRadioLinkSnapshot result; AtlasRadioLink_Snapshot(link,now,&result); return result;
}
static void pair(AtlasRadioLink *a,AtlasRadioLink *b,Wire *ab,Wire *ba)
{
    AtlasRadioLink_Init(a,uid_a); AtlasRadioLink_Init(b,uid_b);
    memset(ab,0,sizeof(*ab)); memset(ba,0,sizeof(*ba));
}
static void round_trip(AtlasRadioLink *a,AtlasRadioLink *b,Wire *ab,Wire *ba,uint32_t now)
{
    assert(AtlasRadioLink_Test(a,now,false,send,ab)==ATLAS_OK);
    assert(!snapshot(a,now).connected);
    split(b,ab->bytes,now+10U,13U);
    AtlasRadioLink_Service(b,now+12U,send,ba);
    split(a,ba->bytes,now+30U,39U);
    AtlasRadioLinkSnapshot s=snapshot(a,now+31U);
    assert(s.connected && s.test==ATLAS_RADIO_TEST_ACK && s.rtt_ms==31U);
    assert(!memcmp(s.peer,uid_b,sizeof(uid_b)) && s.acknowledgements==1U);
    assert(!snapshot(b,now+31U).connected); /* Receipt alone proves only the inbound leg. */
}
int main(void)
{
    AtlasRadioLink a,b,c; Wire ab,ba,other;
    pair(&a,&b,&ab,&ba);
    AtlasRadioLink_Service(&a,100000U,send,&ab); assert(ab.calls==0U); /* Passive boot. */
    for(size_t boundary=0U;boundary<=ATLAS_RADIO_FRAME_SIZE;++boundary) {
        pair(&a,&b,&ab,&ba);
        assert(AtlasRadioLink_Test(&a,100U,false,send,&ab)==ATLAS_OK);
        assert(snapshot(&a,100U).test==ATLAS_RADIO_TEST_WAITING);
        split(&b,ab.bytes,110U,boundary); AtlasRadioLink_Service(&b,112U,send,&ba);
        split(&a,ba.bytes,150U,boundary);
        assert(snapshot(&a,152U).connected && ba.calls==1U);
        AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),160U);
        AtlasRadioLink_Service(&a,161U,send,&ab);
        assert(ab.calls==1U && snapshot(&a,161U).acknowledgements==1U); /* No ACK storm/replay. */
    }
    /* Every single-byte corruption and every truncation must recover at the following frame. */
    for(size_t index=0U;index<ATLAS_RADIO_FRAME_SIZE;++index) {
        pair(&a,&b,&ab,&ba); AtlasRadioLink_Test(&a,100U,false,send,&ab);
        uint8_t damaged[ATLAS_RADIO_FRAME_SIZE]; memcpy(damaged,ab.bytes,sizeof(damaged));
        damaged[index]^=0x41U;
        AtlasRadioLink_Feed(&b,damaged,sizeof(damaged),110U);
        AtlasRadioLink_Service(&b,111U,send,&ba); assert(ba.calls==0U);
        split(&b,ab.bytes,120U,index); AtlasRadioLink_Service(&b,122U,send,&ba); assert(ba.calls==1U);
        AtlasRadioLink_Init(&b,uid_b); memset(&ba,0,sizeof(ba));
        AtlasRadioLink_Feed(&b,ab.bytes,index,150U);
        AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),160U);
        AtlasRadioLink_Service(&b,161U,send,&ba); assert(ba.calls==1U);
    }
    pair(&a,&b,&ab,&ba); AtlasRadioLink_Test(&a,0U,false,send,&ab);
    AtlasRadioLink_Feed(&a,ab.bytes,sizeof(ab.bytes),10U); /* Local UART echo is not a peer. */
    AtlasRadioLink_Service(&a,10U,send,&ab); assert(ab.calls==1U && !a.reply_pending);
    AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),11U); AtlasRadioLink_Service(&b,12U,send,&ba);
    AtlasRadioLink_Init(&c,(const uint32_t[3]){11U,22U,33U}); memset(&other,0,sizeof(other));
    AtlasRadioLink_Test(&c,0U,false,send,&other);
    AtlasRadioLink_Feed(&c,ba.bytes,sizeof(ba.bytes),20U); assert(!snapshot(&c,20U).connected); /* Wrong destination. */
    AtlasRadioLink_Service(&a,2000U,send,&ab);
    assert(a.state.test==ATLAS_RADIO_TEST_TIMEOUT && !a.state.waiting && !snapshot(&a,2000U).connected);
    AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),2001U); assert(!snapshot(&a,2001U).connected);
    AtlasRadioLink_Test(&a,2002U,false,send,&ab);
    AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),2003U); assert(!snapshot(&a,2003U).connected); /* Old challenge. */
    AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),2010U); AtlasRadioLink_Service(&b,2011U,send,&ba);
    AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),2030U); assert(snapshot(&a,2030U).connected);
    AtlasRadioLink_Service(&a,8030U,send,&ab); assert(!snapshot(&a,8030U).connected);
    assert(!snapshot(&a,2030U).connected); /* Expired proof cannot revive after a full tick wrap. */
    pair(&a,&b,&ab,&ba); round_trip(&a,&b,&ab,&ba,UINT32_MAX-20U); /* Wrap during response. */
    AtlasRadioLink_Stop(&a); assert(!snapshot(&a,50U).connected && !a.state.monitoring);
    AtlasRadioLink_Test(&a,UINT32_MAX-100U,true,send,&ab);
    AtlasRadioLink_Service(&a,1899U,send,&ab); assert(a.state.test==ATLAS_RADIO_TEST_TIMEOUT);
    AtlasRadioLink_Service(&a,2899U,send,&ab); assert(a.state.waiting && ab.calls==3U);
    assert(a.state.test==ATLAS_RADIO_TEST_TIMEOUT); /* Background results do not overwrite explicit test. */
    AtlasRadioLink_Test(&a,2900U,false,send,&ab); assert(a.state.test==ATLAS_RADIO_TEST_WAITING);
    assert(AtlasRadioLink_Test(&a,2901U,false,send,&ab)==ATLAS_ERROR_BUSY);
    AtlasRadioLink_Stop(&a); assert(a.state.test==ATLAS_RADIO_TEST_CANCELLED);
    ab.status=ATLAS_ERROR_IO; assert(AtlasRadioLink_Test(&a,3000U,false,send,&ab)==ATLAS_ERROR_IO);
    assert(a.state.test==ATLAS_RADIO_TEST_TX_ERROR && !a.state.waiting && a.state.tx_errors==1U);
    /* Both ends can challenge simultaneously and reply without overwriting their own challenge. */
    pair(&a,&b,&ab,&ba);
    AtlasRadioLink_Test(&a,100U,true,send,&ab); AtlasRadioLink_Test(&b,101U,true,send,&ba);
    AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),110U); AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),111U);
    AtlasRadioLink_Service(&a,112U,send,&ab); AtlasRadioLink_Service(&b,113U,send,&ba);
    AtlasRadioLink_Feed(&a,ba.bytes,sizeof(ba.bytes),120U); AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),121U);
    assert(snapshot(&a,122U).connected && snapshot(&b,122U).connected);
    /* Flooding arbitrary console text or valid PINGs cannot invoke commands or grow a queue. */
    pair(&a,&b,&ab,&ba); AtlasRadioLink_Test(&a,100U,false,send,&ab);
    for(unsigned i=0U;i<1000U;++i) {
        const uint8_t junk[]="+++\n1 servo set 7 2000\n2 gpio 127\nATLR";
        AtlasRadioLink_Feed(&b,junk,sizeof(junk),110U);
        AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),110U);
        AtlasRadioLink_Service(&b,110U,send,&ba);
    }
    assert(ba.calls==1U && b.state.received==1000U);
    AtlasRadioLink_Feed(&b,ab.bytes,sizeof(ab.bytes),210U); AtlasRadioLink_Service(&b,210U,send,&ba);
    assert(ba.calls==2U);
    puts("PASS: radio peer proof, all fragment/corruption/truncation boundaries, echo/replay/foreign/late rejection, wrap, expiry, monitoring, simultaneous tests, errors and bounded replies");
    return 0;
}
