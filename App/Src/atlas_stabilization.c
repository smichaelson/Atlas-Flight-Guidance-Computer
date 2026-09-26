/**
 * @file atlas_stabilization.c
 * @brief Bench-only gravity direction estimator, calibration and bounded horn targets.
 * Major functions: Update, Calibrate, Targets, CommandsStep and CRC-checked SD records.
 * Coordinates: sensor +Z points out of the STM32 face. Calibrated upright defines
 * body +Z; body +X is that face projected normal to +Z. Azimuths are clockwise
 * looking down from USB-C: PCB 7=45, 8=135, 1=225, 2=315 degrees.
 */
#include "atlas_stabilization.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(AtlasStabilizationConfig) == ATLAS_STAB_CONFIG_BYTES, "SD record size");
_Static_assert(offsetof(AtlasStabilizationConfig, crc) == 60U, "SD record layout");
#define STAB_MAGIC UINT32_C(0x41535442)
#define RAD_PER_DEG 0.01745329251994329577f

/** @brief Vector dot product. @param a First. @param b Second. @return Scalar. */
static float stab_dot(const float a[3], const float b[3])
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
/** @brief Vector norm. @param v Input. @return Magnitude. */
static float stab_norm(const float v[3]) { return sqrtf(stab_dot(v, v)); }
/** @brief In-place unit vector with finite checks. @param v Vector. @return Validity. */
static bool stab_unit(float v[3])
{
    const float n = stab_norm(v);
    if (!isfinite(n) || n < 0.001f) return false;
    for (unsigned i=0; i<3; ++i) v[i] /= n;
    return true;
}
/** @brief Right-handed cross product. @param a First. @param b Second. @param out Result. */
static void stab_cross(const float a[3], const float b[3], float out[3])
{
    out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0];
}
/** @brief CRC32/ISO-HDLC, no hardware dependency. @param c Record. @return Checksum. */
static uint32_t stab_crc(const AtlasStabilizationConfig *c)
{
    const uint8_t *bytes=(const uint8_t *)c;
    uint32_t crc=UINT32_MAX;
    for (unsigned i=0; i<60U; ++i)
    {
        crc ^= bytes[i];
        for (unsigned bit=0; bit<8U; ++bit)
            crc=(crc>>1) ^ ((crc&1U) ? UINT32_C(0xEDB88320) : 0U);
    }
    return ~crc;
}
/** @brief Seal an explicitly prepared record. @param c Record. */
void AtlasStabilization_Seal(AtlasStabilizationConfig *c)
{ c->magic=STAB_MAGIC; c->version=1U; memset(c->reserved,0,sizeof(c->reserved)); c->crc=stab_crc(c); }
/** @brief Validate supported settings and upright mounting. @param c Record. @return Validity. */
bool AtlasStabilization_ConfigValid(const AtlasStabilizationConfig *c)
{
    if (!c || c->magic!=STAB_MAGIC || c->version!=1U || c->enabled>1U || c->reverse_mask>15U ||
        c->crc!=stab_crc(c)) return false;
    for (unsigned i=0; i<5U; ++i) if(c->reserved[i]) return false;
    for (unsigned i=0; i<3U; ++i)
        if (!isfinite(c->upright[i]) || !isfinite(c->bias_dps[i]) || fabsf(c->bias_dps[i])>10.0f) return false;
    const float n=stab_norm(c->upright);
    return n>0.99f && n<1.01f && c->upright[1]<-0.85f && fabsf(c->upright[2])<0.35f;
}
/** @brief Fuse one new sample and enforce independent freshness. @param s State.
 * @param sample Sensor input. @param c Optional bias. @param now Current tick. */
void AtlasStabilization_Update(AtlasStabilization *s, const AtlasStabilizationSample *sample,
                               const AtlasStabilizationConfig *c, uint32_t now)
{
    if (!sample->valid || (uint32_t)(now-sample->timestamp_ms)>ATLAS_STAB_SAMPLE_MAX_MS)
    {
        s->fault=sample->valid?ATLAS_STAB_FAULT_STALE:ATLAS_STAB_FAULT_SAMPLE;
        s->initialized=s->ready=false; s->still_count=0U; return;
    }
    const uint32_t elapsed=(uint32_t)(sample->timestamp_ms-s->timestamp_ms);
    if (s->initialized && elapsed==0U) return;
    float a[3],w[3];
    for(unsigned i=0;i<3U;++i)
    {
        a[i]=sample->accel_g[i];
        w[i]=sample->gyro_dps[i]-(c ? c->bias_dps[i] : 0.0f);
    }
    const float an=stab_norm(a), wn=stab_norm(w);
    if (!isfinite(an) || !isfinite(wn) || wn>500.0f || an<0.1f || an>2.0f)
    {
        s->fault=(!isfinite(an)||!isfinite(wn))?ATLAS_STAB_FAULT_NONFINITE:
            (wn>500.0f?ATLAS_STAB_FAULT_RATE:ATLAS_STAB_FAULT_ACCEL);
        s->initialized=s->ready=false; s->still_count=0U; return;
    }
    const bool accel_good=an>=0.85f && an<=1.15f;
    if (!s->initialized || elapsed>ATLAS_STAB_SAMPLE_MAX_MS)
    {
        s->initialized=s->ready=false; s->still_count=0U;
        if (!accel_good) { s->fault=ATLAS_STAB_FAULT_CORRECTION; return; }
        for(unsigned i=0;i<3U;++i) s->up[i]=a[i]/an;
        s->settled_ms=sample->timestamp_ms;
        s->accel_ms=sample->timestamp_ms;
        s->initialized=true;
    }
    else
    {
        const float dt=(float)elapsed*0.001f;
        /* A fixed world vector in rotating sensor coordinates obeys u'=-w x u.
         * Rodrigues avoids an Euler integration norm/angle error at higher rates. */
        const float angle=wn*RAD_PER_DEG*dt;
        if(wn>0.0001f)
        {
            float axis[3],cross[3];
            for(unsigned i=0;i<3U;++i) axis[i]=w[i]/wn;
            stab_cross(axis,s->up,cross);
            const float dot=stab_dot(axis,s->up),co=cosf(angle),si=sinf(angle);
            for(unsigned i=0;i<3U;++i) s->up[i]=co*s->up[i]-si*cross[i]+(1.0f-co)*dot*axis[i];
        }
        if(accel_good)
        {
            const float blend=dt/(0.5f+dt);
            for(unsigned i=0;i<3U;++i) s->up[i]+=(a[i]/an-s->up[i])*blend;
            s->accel_ms=sample->timestamp_ms;
        }
        if(!stab_unit(s->up)) { s->fault=ATLAS_STAB_FAULT_VECTOR; s->initialized=s->ready=false; return; }
    }
    s->timestamp_ms=sample->timestamp_ms;
    s->ready=(uint32_t)(s->timestamp_ms-s->settled_ms)>=250U &&
             (uint32_t)(s->timestamp_ms-s->accel_ms)<=250U;
    s->fault=s->ready?ATLAS_STAB_FAULT_NONE:
        ((uint32_t)(s->timestamp_ms-s->accel_ms)>250U?ATLAS_STAB_FAULT_CORRECTION:ATLAS_STAB_FAULT_SETTLING);
    /* Calibration requires stationary samples for a full second, not one reading.
     * Bias up to 10 dps is allowed for this sensor; motion resets the accumulator. */
    bool still=an>=0.9f && an<=1.1f && stab_norm(sample->gyro_dps)<10.0f;
    for(unsigned i=0;i<3U;++i)
        if(s->still_count && (fabsf(a[i]-s->previous_accel[i])>0.04f ||
            fabsf(a[i]-s->sum_accel[i]/(float)s->still_count)>0.025f)) still=false;
    if(!still) s->still_count=0U;
    else
    {
        if(s->still_count==0U)
        {
            memset(s->sum_accel,0,sizeof(s->sum_accel)); memset(s->sum_gyro,0,sizeof(s->sum_gyro));
            s->still_ms=s->timestamp_ms;
        }
        /* A bounded sliding commissioning window avoids long-run sum overflow. */
        if(s->still_count>=300U)
        {
            for(unsigned i=0;i<3U;++i) { s->sum_accel[i]*=0.5f; s->sum_gyro[i]*=0.5f; }
            s->still_count/=2U;
        }
        for(unsigned i=0;i<3U;++i)
        { s->sum_accel[i]+=a[i]; s->sum_gyro[i]+=sample->gyro_dps[i]; }
        ++s->still_count;
    }
    memcpy(s->previous_accel,a,sizeof(a));
}
/** @brief Prepare a disabled, stationary-upright calibration. @param s State.
 * @param reverse Direction bits. @param c Output. @return Commissioning checks passed. */
bool AtlasStabilization_Calibrate(const AtlasStabilization *s,uint32_t reverse,AtlasStabilizationConfig *c)
{
    if(!s->ready || reverse>15U || s->still_count<50U || (uint32_t)(s->timestamp_ms-s->still_ms)<1000U) return false;
    memset(c,0,sizeof(*c)); c->reverse_mask=reverse;
    for(unsigned i=0;i<3U;++i)
    { c->upright[i]=s->sum_accel[i]/(float)s->still_count; c->bias_dps[i]=s->sum_gyro[i]/(float)s->still_count; }
    if(!stab_unit(c->upright)) return false;
    AtlasStabilization_Seal(c);
    return AtlasStabilization_ConfigValid(c);
}
/** @brief Closest reachable down direction for four radial outward shafts. @param c Calibration.
 * @param up Current up. @param previous Last pulses. @param pulse Targets.
 * @param limited Clipping bits. @param singular Unobservable-angle bits. @return Valid geometry. */
bool AtlasStabilization_Targets(const AtlasStabilizationConfig *c,const float up[3],
                                const uint16_t previous[4],uint16_t pulse[4],uint32_t *limited,uint32_t *singular)
{
    if(!AtlasStabilization_ConfigValid(c)) return false;
    float u[3]={up[0],up[1],up[2]},x[3]={0,0,1},y[3];
    if(!stab_unit(u)) return false;
    const float dot=stab_dot(x,c->upright);
    for(unsigned i=0;i<3U;++i) x[i]-=dot*c->upright[i];
    if(!stab_unit(x)) return false;
    stab_cross(c->upright,x,y);
    const float ux=stab_dot(u,x),uy=stab_dot(u,y),uz=stab_dot(u,c->upright);
    const float co[4]={0.7071067812f,-0.7071067812f,-0.7071067812f,0.7071067812f};
    const float si[4]={0.7071067812f,0.7071067812f,-0.7071067812f,-0.7071067812f};
    *limited=*singular=0U;
    for(unsigned i=0;i<4U;++i)
    {
        /* Outward axis r=(cos(a),-sin(a),0); r x resting_down=(sin(a),cos(a),0).
         * Dot this positive-rotation tangent with world_down=(-ux,-uy,-uz). */
        const float tangent=-si[i]*ux-co[i]*uy;
        if(hypotf(tangent,uz)<0.08f)
        { *singular|=1U<<i; pulse[i]=(previous[i]>=1000U && previous[i]<=2000U)?previous[i]:1500U; continue; }
        float angle=atan2f(tangent,uz)/RAD_PER_DEG;
        /* Increasing PWM is assumed clockwise viewed from outside the shaft.
         * Reverse is an explicit commissioning setting for each actual servo. */
        if(!(c->reverse_mask&(1U<<i))) angle=-angle;
        if(fabsf(angle)>50.0f) *limited|=1U<<i;
        /* At inverted orientations, select the same nearest end across atan2's
         * +/-180 branch cut instead of sweeping through the full travel. */
        if(fabsf(angle)>178.0f && previous[i]!=1500U && previous[i]>=1000U && previous[i]<=2000U)
            angle=previous[i]>1500U ? 50.0f : -50.0f;
        if(angle>50.0f) angle=50.0f;
        if(angle<-50.0f) angle=-50.0f;
        pulse[i]=(uint16_t)lroundf(1500.0f+10.0f*angle);
    }
    return true;
}

/** @brief Reset rest detection at the actual neutral start. @param s State. @param now Tick. */
void AtlasStabilization_CommandsReset(AtlasStabilizationCommands *s,uint32_t now)
{
    memset(s,0,sizeof(*s));
    for(unsigned i=0;i<ATLAS_STAB_CHANNELS;++i) { s->anchor_us[i]=1500U; s->quiet_ms[i]=now; }
}
/** @brief Stream current destinations during motion, then freeze small rest noise.
 * @param s Rest state. @param target Raw targets. @param previous Current pulses.
 * @param singular Hold masks. @param now Tick. @param pulse Outputs. @return Input validity. */
bool AtlasStabilization_CommandsStep(AtlasStabilizationCommands *s,const uint16_t target[4],
                                     const uint16_t previous[4],uint32_t singular,uint32_t now,uint16_t pulse[4])
{
    if(!s || !target || !previous || !pulse || singular>15U || s->tracking_mask>15U) return false;
    AtlasStabilizationCommands next=*s;
    uint16_t result[ATLAS_STAB_CHANNELS];
    for(unsigned i=0;i<ATLAS_STAB_CHANNELS;++i)
    {
        if(target[i]<1000U || target[i]>2000U || previous[i]<1000U || previous[i]>2000U ||
           next.anchor_us[i]<1000U || next.anchor_us[i]>2000U) return false;
        const uint32_t bit=1U<<i;
        result[i]=previous[i];
        if(singular&bit)
        {
            next.tracking_mask&=~bit; next.anchor_us[i]=previous[i]; next.quiet_ms[i]=now;
            continue;
        }
        const int change=(int)target[i]-(int)previous[i];
        if(!(next.tracking_mask&bit))
        {
            next.anchor_us[i]=previous[i]; next.quiet_ms[i]=now;
            if(change<(int)ATLAS_STAB_COMMAND_WAKE_US && change>-(int)ATLAS_STAB_COMMAND_WAKE_US &&
               (change==0 || (target[i]!=1000U && target[i]!=2000U))) continue;
            next.tracking_mask|=bit; next.anchor_us[i]=target[i];
        }
        const int excursion=(int)target[i]-(int)next.anchor_us[i];
        if(excursion>=(int)ATLAS_STAB_COMMAND_QUIET_US || excursion<=-(int)ATLAS_STAB_COMMAND_QUIET_US)
        { next.anchor_us[i]=target[i]; next.quiet_ms[i]=now; }
        if((uint32_t)(now-next.quiet_ms[i])>=ATLAS_STAB_COMMAND_QUIET_MS)
            next.tracking_mask&=~bit;
        else result[i]=target[i];
    }
    *s=next;
    memcpy(pulse,result,sizeof(result));
    return true;
}
