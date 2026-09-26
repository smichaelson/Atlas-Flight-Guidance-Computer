/** @file test_stabilization.c @brief Independent geometry, estimator and settings regression.
 * Major functions: geometry compares against Rodrigues horn kinematics; estimator
 * checks stationary bias, signed rotation, acceleration rejection and tick wrap. */
#include "atlas_stabilization.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846f

static AtlasStabilizationConfig config(void)
{
    AtlasStabilizationConfig c={.upright={0,-1,0}};
    AtlasStabilization_Seal(&c); assert(AtlasStabilization_ConfigValid(&c));return c;
}
/* Forward horn geometry is independent of the production inverse expression.
 * Find the best reachable whole-degree angle by brute force in body coordinates. */
static float down_score(unsigned servo,int pulse_angle,const float body_up[3])
{
    const float az=(45.0f+90.0f*(float)servo)*PI/180.0f;
    const float axis[3]={cosf(az),-sinf(az),0};
    const float neutral[3]={0,0,-1};
    const float angle=-(float)pulse_angle*PI/180.0f;
    const float cross[3]={-axis[1],axis[0],0};
    float score=0;
    for(unsigned k=0;k<3;++k) score-=body_up[k]*(neutral[k]*cosf(angle)+cross[k]*sinf(angle));
    return score;
}
static void geometry(void)
{
    AtlasStabilizationConfig c=config(); uint16_t last[4]={1500,1500,1500,1500},p[4];uint32_t lim,sing;
    for(int pitch=-80;pitch<=80;pitch+=10) for(int roll=-80;roll<=80;roll+=10)
    {
        const float x=(float)pitch*PI/180.0f,y=(float)roll*PI/180.0f;
        const float body_up[3]={sinf(x)*cosf(y),sinf(y),cosf(x)*cosf(y)};
        const float sensor_up[3]={-body_up[1],-body_up[2],body_up[0]};
        assert(AtlasStabilization_Targets(&c,sensor_up,last,p,&lim,&sing));
        for(unsigned i=0;i<4;++i)
        {
            assert(p[i]>=1000 && p[i]<=2000);
            if(sing&(1U<<i))continue;
            const int angle=(int)lroundf(((float)p[i]-1500.0f)/10.0f);
            const float actual=down_score(i,angle,body_up);
            for(int test=-50;test<=50;++test) assert(actual+0.0003f>=down_score(i,test,body_up));
        }
    }
    const float up[3]={0,-1,0};assert(AtlasStabilization_Targets(&c,up,last,p,&lim,&sing));
    for(unsigned i=0;i<4;++i)assert(p[i]==1500);
    const float sideways[3]={0.70710678f,0,0.70710678f};
    assert(AtlasStabilization_Targets(&c,sideways,last,p,&lim,&sing));assert((sing&5U)==5U);
    const float tilt[3]={0,-0.8660254f,0.5f};uint16_t reverse[4];
    assert(AtlasStabilization_Targets(&c,tilt,last,p,&lim,&sing));
    c.reverse_mask=15;AtlasStabilization_Seal(&c);
    assert(AtlasStabilization_Targets(&c,tilt,last,reverse,&lim,&sing));
    for(unsigned i=0;i<4;++i)assert(p[i]+reverse[i]==3000U);
    const float bad[3]={NAN,0,0};assert(!AtlasStabilization_Targets(&c,bad,last,p,&lim,&sing));
    puts("PASS stabilization geometry: 289 orientations x 4 shafts x 101 candidate angles; neutral, reversal, clipping, singularity, NaN");
}
static void estimator(void)
{
    AtlasStabilization s={0};AtlasStabilizationSample sample={{0,-1,0},{3,0,0},UINT32_MAX-1500U,true};
    AtlasStabilizationConfig c;
    for(unsigned i=0;i<120;++i){sample.timestamp_ms+=10U;AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);}
    assert(s.ready);assert(AtlasStabilization_Calibrate(&s,0,&c));assert(fabsf(c.bias_dps[0]-3.0f)<0.001f);
    uint8_t original[64];memcpy(original,&c,64);
    for(unsigned i=0;i<64;++i){((uint8_t*)&c)[i]^=1;assert(!AtlasStabilization_ConfigValid(&c));memcpy(&c,original,64);}
    for(unsigned i=0;i<500;++i){sample.timestamp_ms+=10U;AtlasStabilization_Update(&s,&sample,&c,sample.timestamp_ms);}
    assert(s.ready && s.up[1]<-0.999f); /* Bias-corrected rest survives uint32 wrap. */
    const unsigned count=s.still_count;
    AtlasStabilization_Update(&s,&sample,&c,sample.timestamp_ms);assert(s.still_count==count);
    AtlasStabilization_Update(&s,&sample,&c,sample.timestamp_ms+101U);
    assert(!s.ready && !s.initialized && s.fault==ATLAS_STAB_FAULT_STALE);
    memset(&s,0,sizeof(s));c=config();
    sample=(AtlasStabilizationSample){{0,-1,0},{0,0,0},1000,true};
    for(unsigned i=0;i<40;++i){sample.timestamp_ms+=10;AtlasStabilization_Update(&s,&sample,&c,sample.timestamp_ms);}
    /* +X rotation: a fixed initial -Y up vector rotates toward +Z in sensor axes. */
    for(unsigned i=1;i<=100;++i)
    {
        const float a=(float)i*0.3f*PI/180.0f;
        sample.accel_g[1]=-cosf(a);sample.accel_g[2]=sinf(a);sample.gyro_dps[0]=30;
        sample.timestamp_ms+=10;AtlasStabilization_Update(&s,&sample,&c,sample.timestamp_ms);
    }
    assert(s.ready && fabsf(s.up[2]-0.5f)<0.005f && fabsf(s.up[1]+0.8660254f)<0.005f);
    assert(!AtlasStabilization_Calibrate(&s,0,&c));
    sample.accel_g[0]=NAN;sample.timestamp_ms+=10;AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);
    assert(!s.ready && s.fault==ATLAS_STAB_FAULT_NONFINITE);
    sample=(AtlasStabilizationSample){{0,-1,0},{0,0,0},20000,false};
    AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);assert(s.fault==ATLAS_STAB_FAULT_SAMPLE);
    sample.valid=true;sample.gyro_dps[0]=501;
    AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);assert(s.fault==ATLAS_STAB_FAULT_RATE);
    sample.gyro_dps[0]=0;sample.accel_g[1]=-2.01f;
    AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);assert(s.fault==ATLAS_STAB_FAULT_ACCEL);
    sample.accel_g[1]=-1;
    for(unsigned i=0;i<30;++i){sample.timestamp_ms+=10;AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);}
    assert(s.ready && s.fault==ATLAS_STAB_FAULT_NONE);
    sample.accel_g[1]=-1.3f;
    for(unsigned i=0;i<26;++i){sample.timestamp_ms+=10;AtlasStabilization_Update(&s,&sample,NULL,sample.timestamp_ms);}
    assert(!s.ready && s.fault==ATLAS_STAB_FAULT_CORRECTION);
    puts("PASS stabilization estimator: stationary bias, wrap, duplicate/stale/NaN rejection, signed gyro rotation and moving calibration rejection; all 64 record bytes CRC-covered");
}
static void motion(void)
{
    AtlasStabilizationCommands s;
    uint32_t now=UINT32_MAX-37U;
    AtlasStabilization_CommandsReset(&s,now);
    uint16_t p[4]={1500,1500,1500,1500},t[4];
    /* Fifty seconds of rest noise across tick wrap produces no micro-moves. */
    for(unsigned n=0;n<10000;++n)
    {
        now+=5;
        for(unsigned i=0;i<4;++i)t[i]=(uint16_t)(1495+(n+i*3)%11);
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        for(unsigned i=0;i<4;++i)assert(p[i]==1500);
        assert(!s.tracking_mask);
    }
    /* Full range moves and reversals are direct, with no software speed limit. */
    for(unsigned phase=0;phase<2;++phase)
    {
        now+=5;
        const uint16_t end=phase?1000:2000;
        for(unsigned i=0;i<4;++i)t[i]=end;
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        for(unsigned i=0;i<4;++i)assert(p[i]==end);
    }
    for(unsigned i=0;i<4;++i)p[i]=1500;
    AtlasStabilization_CommandsReset(&s,now);
    /* Once awakened, even single-microsecond target changes pass every 5 ms.
     * Opposite channels must track independently and a stationary one stays put. */
    for(unsigned n=1;n<=500;++n)
    {
        now+=5;t[0]=t[2]=(uint16_t)(1500+n);t[1]=(uint16_t)(1500-n);t[3]=(uint16_t)(1499+n%3);
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        for(unsigned i=0;i<3;++i)assert(p[i]==(n<6?1500:t[i]));
        assert(p[3]==1500);
    }
    /* Follow a continuous small reversal, then settle and ignore +/-1 us noise. */
    for(unsigned n=1;n<=100;++n)
    {
        now+=5;t[0]=t[2]=(uint16_t)(2000-n);t[1]=(uint16_t)(1000+n);
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        for(unsigned i=0;i<3;++i)assert(p[i]==t[i]);
    }
    for(unsigned n=0;n<40;++n)
    { now+=5;assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p)); }
    assert(s.tracking_mask==0);
    uint16_t held[4];memcpy(held,p,sizeof(held));
    for(unsigned n=0;n<200;++n)
    {
        now+=5;for(unsigned i=0;i<4;++i)t[i]=(uint16_t)(held[i]-1+n%3);
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        assert(memcmp(held,p,sizeof(held))==0 && s.tracking_mask==0);
    }
    /* Arbitrarily slow movement accumulates against the last actual output.
     * It may remain inside the rest window, but cannot accumulate larger error. */
    AtlasStabilization_CommandsReset(&s,now);
    for(unsigned i=0;i<4;++i)p[i]=1500;
    for(unsigned n=1;n<=500;++n)
    {
        now+=200;for(unsigned i=0;i<4;++i)t[i]=(uint16_t)(1500+n);
        assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
        for(unsigned i=0;i<4;++i)assert(p[i]<=t[i] && t[i]-p[i]<6);
    }
    /* A sub-window endpoint correction remains reachable even from rest. */
    AtlasStabilization_CommandsReset(&s,now);
    for(unsigned i=0;i<4;++i){p[i]=(i&1)?1981:1023;t[i]=(i&1)?2000:1000;}
    now+=5;assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
    for(unsigned i=0;i<4;++i)assert(p[i]==t[i]);
    AtlasStabilization_CommandsReset(&s,now);p[0]=1002;t[0]=1000;
    now+=5;assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));assert(p[0]==1000);
    /* Singular channels freeze immediately and discard their tracking state. */
    for(unsigned i=0;i<4;++i)t[i]=1000;
    p[0]=1637;
    for(unsigned n=0;n<50;++n)
    {
        now+=5;assert(AtlasStabilization_CommandsStep(&s,t,p,1,now,p));
        assert(p[0]==1637 && !(s.tracking_mask&1));
    }
    /* A late iteration uses only the current target, without a catch-up ramp. */
    now+=1000;t[0]=1800;assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));assert(p[0]==1800);
    /* Reproduce both observed native references with no additional ramp/lag. */
    for(unsigned period=1000;period<=2000;period+=1000)
    {
        AtlasStabilization_CommandsReset(&s,now);
        for(unsigned i=0;i<4;++i)p[i]=1500;
        for(unsigned elapsed=5;elapsed<=period;elapsed+=5)
        {
            now+=5;
            const uint16_t reference=(uint16_t)lroundf(1500.0f+500.0f*sinf(6.28318530718f*(float)elapsed/(float)period));
            for(unsigned i=0;i<4;++i)t[i]=reference;
            assert(AtlasStabilization_CommandsStep(&s,t,p,0,now,p));
            for(unsigned i=0;i<4;++i)assert(p[i]==reference);
        }
    }
    /* Validation failure cannot partly mutate either commands or rest state. */
    AtlasStabilizationCommands before=s;
    uint16_t result[4]={11,22,33,44};t[3]=999;
    assert(!AtlasStabilization_CommandsStep(&s,t,p,0,now,result));
    assert(result[0]==11 && result[3]==44 && memcmp(&s,&before,sizeof(s))==0);
    t[3]=1000;p[2]=0;
    assert(!AtlasStabilization_CommandsStep(&s,t,p,0,now,result));
    assert(result[0]==11 && result[3]==44 && memcmp(&s,&before,sizeof(s))==0);
    assert(!AtlasStabilization_CommandsStep(NULL,t,p,0,now,result));
    puts("PASS stabilization commands: 200Hz direct tracking, rest-noise hold, independent shafts, reversals, bounded slow-tilt error, endpoints, singularity, late/wrap timing and failure atomicity");
}
int main(void){geometry();estimator();motion();return 0;}
