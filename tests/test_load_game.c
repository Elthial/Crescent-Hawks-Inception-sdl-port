#include "game.h"
#include "dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned slot,marker,missing,retries,opens,closes,reads,keys,stage,mapCalls,cacheCalls,tileCalls,regionCalls;
static uint8_t state[OriginalSavedStateBytes];
static unsigned roundtrip,saving,filePosition,fileLength,writeCalls;
static uint8_t savedFile[OriginalSavedStateBytes+5];
static void verify(int ok,unsigned line) { if(!ok) { fprintf(stderr,"Load mismatch line%u\n",line); exit(1); } }
#define check(x) verify(!!(x),__LINE__)
void Menu_Memory_Variables(uint16_t layout) { check(layout==3); }
void Draw_Top_Graphic_Sidebar(void) { }
void Display_Text_From_Memory(uint8_t *text) { check(text!=NULL); }
uint16_t Display_Menu_Choices_And_Check(uint16_t menu) { check(menu==40); return (uint16_t)slot; }
uint16_t Keyboard_Get_ASCII_Hex_Input(void) { ++keys; return 13; }
void Select_Game_Disk_And_Drive(uint16_t disk) { if(disk==3) RequestedGameDiskNumber=3; else check(disk==1 && stage++==2); }
uint16_t Request_Game_Disk(uint16_t disk) { check(disk==3); return TRUE; }
int16_t Get_FileHandle(const uint8_t *filename,uint16_t mode,...)
{
    ++opens;
    if(!strcmp((char *)filename,"INFOCOM.CMP")) { check(mode==0x8000); return opens<=retries?-1:17; }
    check(mode==(saving?0x8101:0x8000));
    check(filename==SaveGameFileName && filename[4]=='1'+slot); return missing?-1:23;
}
int16_t DOS_close_file(uint16_t handle) { check(handle==(closes++==0?17:23)); return 0; }
int16_t DOS_read_file_handler(uint16_t handle,void *data,uint16_t count)
{
    check(handle==23);
    if(roundtrip) {
        check(filePosition+count<=fileLength);
        memcpy(data,savedFile+filePosition,count); filePosition+=count;
        ++reads; return (int16_t)count;
    }
    if(reads==0) { check(count==1); *(uint8_t *)data=(uint8_t)marker; }
    else if(reads==1) { check(count==OriginalSavedStateBytes && data==OriginalSavedState.bytes); memcpy(data,state,count); }
    else { check(count==2 && reads<4); *(uint16_t *)data=reads==2?0x0C35:0xC047; }
    ++reads; return (int16_t)count;
}
int16_t DOS_write_memory_to_save_file(uint16_t handle,const void *data,uint16_t count)
{
    check(roundtrip && saving && handle==23 && fileLength+count<=sizeof savedFile);
    memcpy(savedFile+fileLength,data,count); fileLength+=count; ++writeCalls;
    return (int16_t)count;
}
void Draw_Message_Box(void) { check(0); }
void Display_Text_From_Memory_ScreenRetrace_KeyboardInput(uint8_t *text) { (void)text; check(0); }
void Map_Construct_Nine_Regions(uint16_t region) { check(region==0xCC); ++mapCalls; }
void DOS_Load_Map_Files(uint16_t cell,uint16_t map) { check(cell<9 && map==(uint16_t)(int16_t)(int8_t)MapFileByWorldRegion[0xBB+(cell/3)*16+cell%3]); ++regionCalls; }
void Map_NineGrid_Parent(void) { check(mapCalls==1); }
void Draw_STARLEAG_ICN_AND_Game_Logic(void) { ++cacheCalls; }
void Load_And_Draw_BTTLTECH_ICN(void) { ++tileCalls; }
void Menu_Draw_MultiSelect(uint16_t redraw) { check(redraw==0 && stage++==0); }
void Draw_Health_and_C_Bills_Sidebar(uint16_t redraw) { check(redraw==1 && stage++==1); }
static void testRoundtrip(void)
{
    roundtrip=1; missing=retries=0;
    for(slot=0;slot<6;++slot) for(unsigned seed=0;seed<256;++seed) {
        for(unsigned index=0;index<sizeof state;++index) state[index]=(uint8_t)(seed+index*37);
        state[0xD34E - 0xC614]=0; /* saving only prohibited in the map room */
        state[0xD346-0xC614]=0; /* world reconstruction boundary */
        memcpy(OriginalSavedState.bytes,state,sizeof state);
        CrescentHawkMapPositionX=0x0C35; CrescentHawkMapPositionY=0xC047;
        saving=1; fileLength=filePosition=writeCalls=0;
        opens=closes=reads=keys=stage=mapCalls=cacheCalls=tileCalls=regionCalls=0;
        Save_Game(); check(stage==3 && writeCalls==4 && fileLength==sizeof savedFile);
        check(savedFile[0]==12 && !memcmp(savedFile+1,state,sizeof state));
        /* Destroy every saved field before load, rather than starting with the
         * desired state as the controller-only fixtures do. Unsaved menu bytes
         * are a separate sentinel and must remain outside the native file. */
        memset(OriginalSavedState.bytes,0xCD,sizeof OriginalSavedState.bytes);
        memset(MechSlotByMenuRow,0xBE,4);
        CrescentHawkMapPositionX=CrescentHawkMapPositionY=0xFFFF;
        memset(CacheSecurityCodeUsed,0xAA,sizeof CacheSecurityCodeUsed);
        TraitorWarning=0xABCD; TilesetId=0;
        memset(MapFileByWorldRegion,0,WorldRegionCount);
        saving=0; opens=closes=reads=keys=stage=mapCalls=cacheCalls=tileCalls=regionCalls=0;
        Load_Game();
        check(stage==3 && reads==4 && filePosition==fileLength && closes==2 && keys==0);
        check(!memcmp(OriginalSavedState.bytes,state,sizeof state));
        check(CrescentHawkMapPositionX==0x0C35 && CrescentHawkMapPositionY==0xC047);
        check(CurrentMap==(state[0xD310 - 0xC614]?11:1) && ViewedHolodisk==(state[0xD33E - 0xC614]?12:0));
        check(MenuControls[38].selection==(uint16_t)(int16_t)(int8_t)state[0xD35B-0xC614]);
        check(TraitorWarning==0 && mapCalls==1);
        for(unsigned code=0;code<CacheSecurityCodeCount;++code) check(CacheSecurityCodeUsed[code]==0);
        for(unsigned row=0;row<4;++row) check(MechSlotByMenuRow[row]==0xBE);
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--roundtrip")) { testRoundtrip(); return 0; }
    check(argc==1);
    for(slot=0;slot<7;++slot) for(marker=0;marker<256;++marker)
    for(missing=0;missing<2;++missing) for(unsigned flags=0;flags<8;++flags) {
        memset(state,0x5A,sizeof state);
        state[0xD346-0xC614]=(uint8_t)(flags&1);
        state[0xD310-0xC614]=(uint8_t)(flags&2);
        state[0xD33E - 0xC614]=(uint8_t)(flags&4);
        state[0xD35B-0xC614]=(uint8_t)marker;
        for(unsigned mech=0;mech<4;++mech) state[0x110+mech*125]=(uint8_t)(mech==0?'L':'W');
        memcpy(OriginalSavedState.bytes,state,sizeof state);
        memset(MechSlotByMenuRow,0xBE,4); memset(CacheSecurityCodeUsed,0xAA,sizeof CacheSecurityCodeUsed);
        memset(CombatantSpriteFrame,0xA5,sizeof CombatantSpriteFrame);
        memset(CombatantMovementDirection,0xA5,sizeof CombatantMovementDirection);
        memset(CombatantAnimationSelector,0xA5,sizeof CombatantAnimationDirection);
        TraitorWarning=0xABCD; CurrentMap=99; ViewedHolodisk=77; TilesetId=2;
        retries=marker%3; opens=closes=reads=keys=stage=mapCalls=cacheCalls=tileCalls=regionCalls=0;
        memset(MapFileByWorldRegion,0,WorldRegionCount);
        for(unsigned cell=0;cell<9;++cell) MapFileByWorldRegion[0xBB+(cell/3)*16+cell%3]=(uint8_t)(0x80+cell);
        Load_Game(); check(stage==3);
        check(!memcmp(OriginalSavedState.bytes,state,sizeof state) && MechSlotByMenuRow[0]==0xBE);
        unsigned loaded=slot!=6 && !missing && marker==12;
        unsigned reset=slot!=6 && !missing;
        check(reads==(loaded?4u:reset?1u:0u));
        check(closes==(slot==6?0u:loaded?2u:1u));
        check(keys==(slot!=6 && !loaded?1u:0u));
        check(TraitorWarning==(reset?0:0xABCD));
        check(mapCalls==(loaded && !(flags&1)?1u:0u) && cacheCalls==(loaded && (flags&1)?1u:0u));
        check(regionCalls==(mapCalls?9u:0u) && tileCalls==(reset && !(flags&1)?1u:0u));
        if(loaded) {
            check(CurrentMap==(flags&2?11:1) && ViewedHolodisk==(flags&4?12:0));
            check(MenuControls[38].selection==(uint16_t)(int16_t)(int8_t)marker);
            for(unsigned code=0;code<CacheSecurityCodeCount;++code) check(CacheSecurityCodeUsed[code]==0);
        }
        for(unsigned mech=0;mech<4;++mech) {
            check(CombatantSpriteFrame[mech]==(reset?0:0xA5));
            if(reset) check(CombatantAnimationCursors[mech].offset==0x270 && CombatantSpriteFamilyOffset[mech]==(mech==0?0:146));
        }
        check(CombatantSpriteFrame[4]==0xA5);
    }
    return 0;
}
