/** @file test_storage_owner.c @brief Test the real storage owner against file/RTC boundaries.
 * Major functions: calendar_cases verifies UTC/shadow ordering; request_cases
 * checks safe names, copied buffers and append outcomes; owner_script exercises
 * result backpressure and removal before queued writes. Real FatFs/BSP/disk
 * integration is separately tested using a RAM FAT volume in Tests/review. */
#include "service_model.h"
#include "../../App/Src/atlas_storage.c"
#include <assert.h>
#include <stdio.h>
uint8_t retSD;
char SDPath[4]="0:";
FATFS SDFatFS;
static RTC_HandleTypeDef rtc;
static RTC_TimeTypeDef calendar_time;
static RTC_DateTypeDef calendar_date;
static bool fail_time,fail_date,fail_read_time,read_time_latched;
static bool present,current,authorized,short_write,fail_write,fail_sync;
static unsigned opens,writes,syncs,closes,mounts,unmounts,get_dates;
static uint8_t file_data[4096];
static UINT file_length;
#if ATLAS_BRINGUP
static bool test_file_exists,corrupt_read,short_read,fail_close;
#endif

void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *r,uint32_t index,uint32_t value)
{ assert(index==RTC_BKP_DR0);r->backup=value; }
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *r,uint32_t index)
{ assert(index==RTC_BKP_DR0);return r->backup; }
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *r,const RTC_TimeTypeDef *value,uint32_t format)
{ assert(r->backup==0U && format==RTC_FORMAT_BIN);if(fail_time)return HAL_ERROR;calendar_time=*value;return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *r,const RTC_DateTypeDef *value,uint32_t format)
{ assert(r->backup==0U && format==RTC_FORMAT_BIN);if(fail_date)return HAL_ERROR;calendar_date=*value;return HAL_OK; }
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *r,RTC_TimeTypeDef *value,uint32_t format)
{ (void)r;assert(format==RTC_FORMAT_BIN && !read_time_latched);read_time_latched=true;*value=calendar_time;return fail_read_time?HAL_ERROR:HAL_OK; }
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *r,RTC_DateTypeDef *value,uint32_t format)
{ (void)r;assert(format==RTC_FORMAT_BIN && read_time_latched);read_time_latched=false;++get_dates;*value=calendar_date;return HAL_OK; }
void SD_Invalidate(void) { current=false;authorized=false; }
void SD_PrepareMount(void) { authorized=true; }
uint8_t BSP_SD_IsMediaCurrent(void) { return current?1U:0U; }
uint8_t BSP_SD_GetCardState(void) { return current?SD_TRANSFER_OK:SD_TRANSFER_ERROR; }
uint8_t BSP_SD_IsDetected(void) { return present?SD_PRESENT:SD_NOT_PRESENT; }
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE option)
{
    assert(strcmp(path,"0:")==0);
    if(fs==NULL){++unmounts;return FR_OK;}
    assert(option==1U && authorized);authorized=false;++mounts;current=present;
    return current?FR_OK:FR_NOT_READY;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode)
{
    assert(current);
#if ATLAS_BRINGUP
    if(strcmp(path,"0:/ATLASCHK.TST")==0)
    {
        assert(mode==(FA_WRITE|FA_CREATE_NEW) || mode==FA_READ);
        if(mode!=(BYTE)FA_READ)
        {
            if(test_file_exists)return FR_EXIST;
            test_file_exists=true;file_length=0U;
        }
        else assert(test_file_exists);
        ++opens;memset(file,0,sizeof(*file));file->obj.objsize=file_length;return FR_OK;
    }
#endif
    assert(strcmp(path,"0:/TEST.LOG")==0);
    assert(mode==(FA_WRITE|FA_OPEN_ALWAYS) || mode==FA_READ);++opens;
    memset(file,0,sizeof(*file));file->obj.objsize=file_length;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) { file->fptr=offset;return FR_OK; }
FRESULT f_write(FIL *file,const void *data,UINT length,UINT *transferred)
{
    ++writes;*transferred=0U;if(fail_write)return FR_DISK_ERR;
    assert(file->fptr==file_length && file_length+length<=sizeof(file_data));
    *transferred=short_write?length-1U:length;memcpy(file_data+file_length,data,*transferred);
    file_length+=*transferred;file->fptr+=*transferred;file->obj.objsize=file_length;return FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT length,UINT *transferred)
{
    *transferred=file->fptr<file_length?file_length-(UINT)file->fptr:0U;
    if(*transferred>length)*transferred=length;
    memcpy(data,file_data+file->fptr,*transferred);file->fptr+=*transferred;
#if ATLAS_BRINGUP
    if(corrupt_read && *transferred)((uint8_t *)data)[0]^=1U;
    if(short_read && *transferred)--*transferred;
#endif
    return FR_OK;
}
FRESULT f_sync(FIL *file) { (void)file;++syncs;return fail_sync?FR_DISK_ERR:FR_OK; }
FRESULT f_close(FIL *file)
{
    (void)file;++closes;
#if ATLAS_BRINGUP
    if(fail_close)return FR_DISK_ERR;
#endif
    return FR_OK;
}

/** @brief Reset an inert file/RTC/service fixture; no host files are created. */
static void fixture(void)
{
    TestRuntimeReset();memset(&health,0,sizeof(health));memset(&published_health,0,sizeof(published_health));
    memset(&rtc,0,sizeof(rtc));started=false;next_ticket=0U;
    fail_time=fail_date=fail_read_time=read_time_latched=false;get_dates=0U;
    present=true;current=authorized=short_write=fail_write=fail_sync=false;
    opens=writes=syncs=closes=mounts=unmounts=0U;retSD=0U;file_length=0U;
    assert(AtlasStorage_Start(&rtc)==ATLAS_OK);
    test_scheduler=taskSCHEDULER_RUNNING;
}
/** @brief Check Gregorian dates in the supported range and invalid backup-marker handling. */
static void calendar_cases(void)
{
    fixture();const uint32_t unknown=(1UL<<21)|(1UL<<16);
    assert(AtlasStorage_FatTime()==unknown && !health.time_valid && get_dates==0U);
    AtlasUtc utc={2000U,2U,29U,23U,59U,59U};
    assert(storage_set_utc(&utc)==ATLAS_OK && calendar_date.WeekDay==2U);
    uint32_t encoded=AtlasStorage_FatTime();
    assert(encoded==((20UL<<25)|(2UL<<21)|(29UL<<16)|(23UL<<11)|(59UL<<5)|29U));
    assert(health.time_valid && get_dates==1U && !read_time_latched);
    utc=(AtlasUtc){2026U,9U,2U,12U,34U,56U};
    assert(storage_set_utc(&utc)==ATLAS_OK && calendar_date.WeekDay==3U);
    AtlasUtc invalid[]={ {1999,1,1,0,0,0},{2100,1,1,0,0,0},{2026,2,29,0,0,0},
        {2026,0,1,0,0,0},{2026,13,1,0,0,0},{2026,4,31,0,0,0},
        {2026,1,0,0,0,0},{2026,1,1,24,0,0},{2026,1,1,0,60,0},{2026,1,1,0,0,60} };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        assert(storage_set_utc(&invalid[i])==ATLAS_ERROR_ARGUMENT && rtc.backup==STORAGE_RTC_MAGIC);
    fail_date=true;
    assert(storage_set_utc(&utc)==ATLAS_ERROR_IO && rtc.backup==0U && !health.time_valid);
    assert(AtlasStorage_FatTime()==unknown);fail_date=false;
    assert(storage_set_utc(&utc)==ATLAS_OK);
    fail_read_time=true;unsigned reads=get_dates;
    assert(AtlasStorage_FatTime()==unknown && !health.time_valid && get_dates==reads+1U && !read_time_latched);
    fail_read_time=false;calendar_date.Month=13U;
    assert(AtlasStorage_FatTime()==unknown && !health.time_valid);
}
/** @brief Validate public queue boundaries and uncertain/partial file outcomes. */
static void request_cases(void)
{
    fixture();AtlasStorageRequest request={.operation=ATLAS_STORAGE_APPEND,.filename="TEST.LOG",.length=3U};
    memcpy(request.data,"abc",3U);uint32_t ticket;
    const char *invalid[]={"", ".", "..", "../X", "A/B", "A\\B", "A:", "A.", "A.B.C", "A.LONG", "ABCDEFGHI.X"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
    {
        memset(request.filename,0,sizeof(request.filename));memcpy(request.filename,invalid[i],strlen(invalid[i]));
        assert(AtlasStorage_Submit(&request,NULL)==ATLAS_ERROR_ARGUMENT);
    }
    memset(request.filename,'A',sizeof(request.filename));
    assert(AtlasStorage_Submit(&request,NULL)==ATLAS_ERROR_ARGUMENT);
    strcpy(request.filename,"TEST.LOG");request.length=513U;
    assert(AtlasStorage_Submit(&request,NULL)==ATLAS_ERROR_ARGUMENT);request.length=3U;
    assert(AtlasStorage_Submit(&request,&ticket)==ATLAS_OK);
    memset(request.data,'Z',3U);StorageItem item;AtlasStorageResult result;
    assert(xQueueReceive(requests,&item,0U)==pdTRUE && item.ticket==ticket && memcmp(item.request.data,"abc",3U)==0);
    assert(storage_mount()==FR_OK);storage_execute(&item,&result);
    assert(result.status==ATLAS_OK && result.length==3U && writes==1U && syncs==1U && closes==1U);
    storage_execute(&item,&result);
    assert(file_length==6U && memcmp(file_data,"abcabc",6U)==0); /* Append preserves earlier bytes. */
    short_write=true;storage_execute(&item,&result);
    assert(result.status==ATLAS_ERROR_IO && result.filesystem_result==FR_DENIED && result.length==2U);
    short_write=false;fail_sync=true;unsigned previous_writes=writes;
    storage_execute(&item,&result);
    assert(result.status==ATLAS_ERROR_IO && !health.mounted && writes==previous_writes+1U);
    storage_execute(&item,&result);assert(result.status==ATLAS_ERROR_NOT_READY && writes==previous_writes+1U);
    assert(AtlasStorage_Submit(NULL,NULL)==ATLAS_ERROR_NULL);
    test_ipsr=1U;assert(AtlasStorage_Submit(&request,NULL)==ATLAS_ERROR_STATE);
}
/** @brief Inject result backpressure and a replaced card before a queued append.
 * @param iteration Completed task iterations. */
static void owner_script(unsigned iteration)
{
    AtlasStorageRequest request={.operation=ATLAS_STORAGE_APPEND,.filename="TEST.LOG",.length=1U,.data={0xA5U}};
    if(iteration==1U)for(unsigned i=0;i<4U;++i)assert(AtlasStorage_Submit(&request,NULL)==ATLAS_OK);
    if(iteration==5U){assert(writes==4U);assert(AtlasStorage_Submit(&request,NULL)==ATLAS_OK);}
    if(iteration==6U)
    {
        assert(writes==4U && uxQueueMessagesWaiting(requests)==1U);current=false;
        AtlasStorageResult result;assert(AtlasStorage_Receive(&result));
    }
}
#if ATLAS_BRINGUP
/** @brief Verify exclusive file creation and adverse outcomes without real media. */
static void self_test_cases(void)
{
    for(unsigned failure=0;failure<7U;++failure)
    {
        fixture();test_file_exists=corrupt_read=short_read=fail_close=false;
        assert(storage_mount()==FR_OK);
        if(failure==1U)test_file_exists=true;
        if(failure==2U)short_write=true;
        if(failure==3U)fail_sync=true;
        if(failure==4U)corrupt_read=true;
        if(failure==5U)short_read=true;
        if(failure==6U)fail_close=true;
        StorageItem item={.request={.operation=ATLAS_STORAGE_SELF_TEST},.ticket=99U};
        AtlasStorageResult result;
        assert(AtlasStorage_Submit(&item.request,NULL)==ATLAS_OK);
        storage_execute(&item,&result);
        assert(result.length==0U && result.ticket==99U && test_file_exists);
        if(failure==0U)
        {
            assert(result.status==ATLAS_OK && result.verified_bytes==1024U && file_length==1024U);
            assert(writes==2U && syncs==1U && opens==2U && closes==2U);
            for(unsigned i=0;i<1024U;++i)assert(file_data[i]==storage_test_byte(i));
            const unsigned before=writes;
            storage_execute(&item,&result);
            assert(result.filesystem_result==FR_EXIST && writes==before && result.verified_bytes==0U);
        }
        else assert(result.status!=ATLAS_OK && result.verified_bytes==0U);
        if(failure==1U)assert(writes==0U && opens==0U); /* Existing bytes untouched. */
    }
    fail_close=false;fixture();TestRunTask(2U,NULL);
    assert(mounts==0U && writes==0U && !published_health.mounted); /* No boot probe/write. */
    puts("Bring-up storage: explicit mount, exclusive 1024-byte test, no overwrite/retry, partial/sync/compare/close failures PASS");
}
#endif
/** @brief Run deterministic storage-service checks. @return Zero when all pass. */
int main(void)
{
    calendar_cases();request_cases();
#if ATLAS_BRINGUP
    (void)owner_script;self_test_cases();
#else
    fixture();TestRunTask(9U,owner_script);
    assert(writes==4U && opens==4U && !published_health.mounted && mounts==1U);
    assert(uxQueueMessagesWaiting(requests)==0U); /* Fifth request completed NOT_READY without touching card. */
    AtlasStorageResult result={0};while(AtlasStorage_Receive(&result)){}
    assert(result.status==ATLAS_ERROR_NOT_READY);
#endif
    puts("Storage owner: UTC/leap dates, partial update invalidation, copied requests, append and backpressure/removal PASS");
    return 0;
}
