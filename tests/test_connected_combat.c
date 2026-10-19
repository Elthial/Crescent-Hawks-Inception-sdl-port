#define _CRT_SECURE_NO_WARNINGS
#include "game.h"
#include "backend.h"
#include "music.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void verify(int condition,unsigned line)
{
    if(!condition) { fprintf(stderr,"Connected combat mismatch at line%u\n",line); exit(1); }
}
#define check(condition) verify(!!(condition),__LINE__)
static unsigned renderedCombat,assetsReady;
/* Timers may enqueue several main-thread callbacks before the game pumps
 * events. Leave a quiet interval after each operator batch so the original
 * input drains can empty; timer callbacks are not consumed user input. */
static uint64_t nextTrainingInput,nextExplorationInput,nextAmmunitionInput;
enum { OperatorQuietMilliseconds=50 };
static SDL_AtomicInt assetStartupInputPulses;
static const char *statisticsPreviewPath;
static const char *existingSaveName;
static unsigned existingSaveSelectionQueued;
static void SDLCALL selectExistingSave(void *unused)
{
    SDL_Event key;
    (void)unused; check(SDL_IsMainThread());
    if(existingSaveSelectionQueued) return;
    existingSaveSelectionQueued=1;
    memset(&key,0,sizeof key); key.type=SDL_EVENT_KEY_DOWN;
    key.key.key=SDLK_DOWN; key.key.scancode=SDL_SCANCODE_DOWN;
    for(unsigned slot=1;slot<(unsigned)(existingSaveName[4]-'0');++slot)
        check(SDL_PushEvent(&key));
    key.key.key=SDLK_RETURN; key.key.scancode=SDL_SCANCODE_RETURN;
    check(SDL_PushEvent(&key));
}
static Uint32 SDLCALL requestExistingSaveSelection(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    check(SDL_RunOnMainThread(selectExistingSave,NULL,false));
    return interval;
}
static void runExistingSave(void)
{
    uint8_t before[OriginalSavedStateBytes+5],after[sizeof before];
    FILE *file;
    SDL_TimerID timer;
    check(strlen(existingSaveName)==5 && !memcmp(existingSaveName,"GAME",4));
    check(existingSaveName[4]>='1' && existingSaveName[4]<='6');
    file=fopen(existingSaveName,"rb"); check(file!=NULL);
    check(fread(before,1,sizeof before,file)==sizeof before && fgetc(file)==EOF);
    check(fclose(file)==0 && before[0]==OriginalSaveFormatMarker);
    check(MenuControls[SaveSlotMenu].selection==0);
    timer=SDL_AddTimer(100,requestExistingSaveSelection,NULL); check(timer!=0);
    check(SDLBackend_RunApplication(Load_Game)==SDLBackend_ApplicationReturned);
    check(SDL_RemoveTimer(timer)); check(existingSaveSelectionQueued);
    check(MenuControls[SaveSlotMenu].selection==(unsigned)(existingSaveName[4]-'1'));
    /* Compare independent file bytes, not a second loader. Fog/world rendering
     * may update visibility; party/Mech records and economy must load intact. */
    check(!memcmp(Characters,before+1,sizeof Characters));
    check(!memcmp(Mechs,before+1+offsetof(OriginalSavedStateStorage,fields.world),sizeof Mechs));
    check(!memcmp(PersistentState.bytes,before+1+
        offsetof(OriginalSavedStateStorage,fields.world.fields.persistent),PersistentBldStateBytes));
    check(!memcmp(&CBills,before+1+offsetof(OriginalSavedStateStorage,fields.cbills),sizeof CBills));
    check(!memcmp(StockBalances,before+1+offsetof(OriginalSavedStateStorage,fields.stocks),sizeof StockBalances));
    check(CrescentHawkMapPositionX==(uint16_t)(before[OriginalSavedStateBytes+1]|
        ((uint16_t)before[OriginalSavedStateBytes+2]<<8)));
    check(CrescentHawkMapPositionY==(uint16_t)(before[OriginalSavedStateBytes+3]|
        ((uint16_t)before[OriginalSavedStateBytes+4]<<8)));
    file=fopen(existingSaveName,"rb"); check(file!=NULL);
    check(fread(after,1,sizeof after,file)==sizeof after && fgetc(file)==EOF);
    check(fclose(file)==0 && !memcmp(before,after,sizeof before));
    printf("Actual %s loaded through SDL menu/file/map methods; party, Mechs, story flags, economy and position match; source bytes unchanged.\n",existingSaveName);
}
static void SDLCALL closeAtAssetMilestone(void *unused)
{
    SDL_Event quit;
    (void)unused;
    check(SDL_IsMainThread());
    if(assetsReady || MusicNoteTickReload!=MusicPcCadenceTicks) return;
    for(unsigned sprite=0;sprite<CombatSpriteCount;++sprite)
        if(CombatSpritePointers[sprite]==NULL) return;
    assetsReady=1;
    memset(&quit,0,sizeof quit); quit.type=SDL_EVENT_QUIT;
    check(SDL_PushEvent(&quit));
}
static Uint32 SDLCALL requestAssetMilestone(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    SDL_RunOnMainThread(closeAtAssetMilestone,NULL,false);
    return interval;
}
static Uint32 SDLCALL requestAssetStartupInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    SDL_Event key={0};
    (void)unused; (void)timer; (void)interval;
    key.type=SDL_EVENT_KEY_DOWN; key.key.key=SDLK_SPACE; key.key.scancode=SDL_SCANCODE_SPACE;
    check(SDL_PushEvent(&key));
    return SDL_AddAtomicInt(&assetStartupInputPulses,-1)>1?interval:0;
}
static void loadCombatAssets(void)
{
    SDL_TimerID timer,inputTimer;
    uint32_t savedRetraceRate=SDLBackend_RetracesPerSecond;
    SDLBackend_RetracesPerSecond=1000;
    SDL_SetAtomicInt(&assetStartupInputPulses,20);
    inputTimer=SDL_AddTimer(5,requestAssetStartupInput,NULL); check(inputTimer!=0);
    timer=SDL_AddTimer(10,requestAssetMilestone,NULL); check(timer!=0);
    check(SDLBackend_RunApplication(Setup_Game)==SDLBackend_ApplicationClosed);
    check(SDL_RemoveTimer(timer)); check(assetsReady);
    (void)SDL_RemoveTimer(inputTimer);
    /* The asset milestone deliberately closes startup before its next native
     * input drain.  Do not let a test-only splash pulse enter later dialogs. */
    Drain_Pending_Keyboard_Input();
    SDLBackend_RetracesPerSecond=savedRetraceRate;
    Load_And_Draw_BTTLTECH_ICN();
}
static void runAnimationScenes(void)
{
    /* Original scene parent, disk/file calls, XOR decoder, EGA transfer and
     * SDL retraces: no method doubles or replacement animation algorithm. */
    enum { ShippedSceneCount=22 };
    OuttakeFrequency=0;
    for(uint16_t scene=0;scene<ShippedSceneCount;++scene) {
        uint16_t frameCount=0;
        char filename[16];
        (void)snprintf(filename,sizeof filename,"O%u.ANM",(unsigned)scene);
        Display_Animation_Scene(scene,AnimationPlayback_Force_CallerRedraw);
        while(frameCount<AnmDelayTableOffset && AnimationFileData[frameCount]) ++frameCount;
        check(frameCount>0 && frameCount<AnmDelayTableOffset);
        check(AnimationFrameNumber==frameCount);
        check(AnimationStreamOffset>AnmStreamNativeOffset);
        check(AnimationStreamOffset<=AnmFileNativeOffset+AnmFileMaximumBytes);
        check(strcmp((char *)DynamicString,filename)==0);
        check(SDLBackend_PresentEgaScreen());
    }
    /* Also exercise the native hold/sidebar restoration branch. Audio parity
     * is separate; the original sound-enabled gate remains authoritative. */
    Display_Animation_Scene(AnimationO00_MechStartUp,AnimationPlayback_RestoreGameView);
    check(CurrentMenuLayoutIndex==4);
    /* Synthetic control bytes only, in the real BLD storage: prove the native
     * two-operand scene handoff and disabled-input gate using a shipped ANM. */
    BTStatsOrBldMemory[0]=Bld_ConditionalScene;
    BTStatsOrBldMemory[1]=AnimationO06_CrescentHawkCard;
    BTStatsOrBldMemory[2]=AnimationPlayback_Force_CallerRedraw;
    BTStatsOrBldMemory[3]=Bld_Exit;
    DisableInput=FALSE; AnimationFrameNumber=UINT16_MAX;
    Execute_Bld_Bytecode(BTStatsOrBldMemory);
    check(AnimationFrameNumber>0 && AnimationFrameNumber<AnmDelayTableOffset);
    check(strcmp((char *)DynamicString,"O6.ANM")==0);
    /* An exit-valued scene argument must be consumed, not executed as an
     * opcode; the following layout instruction witnesses interpreter progress. */
    BTStatsOrBldMemory[2]=Bld_Exit;
    BTStatsOrBldMemory[3]=Bld_SetTextLayout;
    BTStatsOrBldMemory[4]=7; BTStatsOrBldMemory[5]=8;
    BTStatsOrBldMemory[6]=Bld_Exit;
    TextColumn=TextRow=0;
    DisableInput=TRUE; AnimationFrameNumber=UINT16_MAX;
    Execute_Bld_Bytecode(BTStatsOrBldMemory);
    check(AnimationFrameNumber==UINT16_MAX && TextColumn==7 && TextRow==8);
    DisableInput=FALSE;
    puts("Connected animation scenes: all22 ANM parents, view restoration and BLD scene gate completed.");
}
static unsigned trainingReturning,trainingInputs,trainingDelayUpdates,trainingDelayPhase;
static int trainingDelayStarted;
static void SDLCALL operateTrainingMission(void *unused)
{
    SDL_Event key;
    uint16_t x=CrescentHawkMapPositionX,y=CrescentHawkMapPositionY;
    (void)unused; check(SDL_IsMainThread());
    /* The scripted operator must not flood unconsumed movement keys while
     * original synchronous sound is playing; it is not a live-input count. */
    if(SDLBackend_SoundEffectActive()) return;
    if(SDL_GetTicks()<nextTrainingInput) return;
    if(getenv("CHI_TEST_SLOW_OPERATOR")) SDL_Delay(12);
    nextTrainingInput=SDL_GetTicks()+OperatorQuietMilliseconds;
    if(x<TrainingMarchEndX && !trainingReturning && y==TrainingMarchY) return;
    /* Callback count is a host scheduling budget, not consumed game input.
     * Allow the measured Debug return march after the observed idle updates;
     * CTest separately bounds the paced scenario to120 seconds. */
    if(++trainingInputs>=3000) {
        fprintf(stderr,"Training operator stalled at %04X/%04X, returning=%u\n",x,y,trainingReturning);
        exit(1);
    }
    if(x>=TrainingSoutheastX && y>=TrainingSoutheastY) trainingReturning=1;
    memset(&key,0,sizeof key); key.type=SDL_EVENT_KEY_DOWN;
    if(!trainingReturning && x==TrainingMarchEndX && y==TrainingMarchY && trainingDelayUpdates) {
        unsigned phase=MissionNpcUpdatePhase&MissionNpcUpdatePhaseMask;
        if(trainingDelayStarted) {
            unsigned updates=(phase-trainingDelayPhase)&MissionNpcUpdatePhaseMask;
            trainingDelayUpdates=updates<trainingDelayUpdates?trainingDelayUpdates-updates:0;
        }
        trainingDelayStarted=1; trainingDelayPhase=phase;
        key.key.key=SDLK_RETURN;
    } else if(!trainingReturning) key.key.key=x<TrainingSoutheastX?SDLK_D:SDLK_X;
    /* Return through the east-side hangar approach, not its south wall. */
    else if(x>TrainingMarchEndX) key.key.key=SDLK_A;
    else if(y>TrainingMarchY) key.key.key=SDLK_W;
    else key.key.key=SDLK_A;
    check(SDL_PushEvent(&key));
}
static Uint32 SDLCALL requestTrainingInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    check(SDL_RunOnMainThread(operateTrainingMission,NULL,false));
    return interval;
}
static void runTrainingMission(void)
{
    SDL_TimerID timer;
    uint8_t savedTrainingTile;
    Load_Game_Map_Data();
    /* Bound accelerated idle updates: zero-duration retraces make the native
     * signed WORD mission timer and posted/consumed input host-speed dependent. */
    SDLBackend_RetracesPerSecond=1000;
    savedTrainingTile=MapFileTiles[TrainingTemporaryTileIndex];
    SelectedTrainingMech=0; PersistentState.bytes[0]=Mission_SoutheastCorner;
    trainingReturning=trainingInputs=0;
    /* Observe the original world-update phase, not posted keys: native drains
     * can discard batches. Modulo sampling may undercount full wraps, never
     * count an unconsumed key as a mission update. Leave a late-return margin. */
    trainingDelayStarted=0;
    trainingDelayUpdates=2*(TrainingSoutheastPassTime+TrainingLocustTimeAllowance+1);
    timer=SDL_AddTimer(10,requestTrainingInput,NULL); check(timer!=0);
    Citadel_Building_Dialogs(CitadelAction_StartTraining);
    check(SDL_RemoveTimer(timer));
    check(trainingReturning && trainingInputs>0);
    if(trainingDelayUpdates || TrainingMissionPassed!=FALSE)
        fprintf(stderr,"Training delay updates remaining=%u, passed=%u, callbacks=%u\n",trainingDelayUpdates,TrainingMissionPassed,trainingInputs);
    check(trainingDelayUpdates==0 && TrainingMissionPassed==FALSE);
    check(MapFileTiles[TrainingTemporaryTileIndex]==savedTrainingTile);
    check(CrescentHawkMapPositionX<=TrainingHangarMaxX);
    check(CrescentHawkMapPositionY>=TrainingHangarMinY && CrescentHawkMapPositionY<=TrainingHangarMaxY);
    check(Mechs[0].name[0]=='L' && Characters[Character_Jason].mechAssignment==0);
    Citadel_Building_Dialogs(CitadelAction_LeaveTraining);
    check(Mechs[0].name[0]==MECH_Destroyed && Characters[Character_Jason].mechAssignment==Character_OnFoot);
    check(CrescentHawkMapPositionX==TrainingReturnX && CrescentHawkMapPositionY==TrainingReturnY);
    puts("Connected training: original map, Locust march, southeast objective, late failure and return completed.");
}
static unsigned explorationInputActive,explorationPhase,explorationCallbacks,explorationPauseSeen,explorationResumeSeen;
static uint8_t explorationFinanceAtPause;
static void SDLCALL operateExploration(void *unused)
{
    SDL_Event event;
    (void)unused; check(SDL_IsMainThread());
    if(!explorationInputActive) return;
    if(SDL_GetTicks()<nextExplorationInput) return;
    if(getenv("CHI_TEST_SLOW_OPERATOR")) SDL_Delay(12);
    nextExplorationInput=SDL_GetTicks()+OperatorQuietMilliseconds;
    check(++explorationCallbacks<2000);
    memset(&event,0,sizeof event); event.type=SDL_EVENT_KEY_DOWN;
    if(explorationPhase==0) {
        /* No keys during scene15 or the first five real idle world ticks. */
        if(PartyHealthRecoveryTimer!=0) return;
        check(TrainingMissionAndQuizCooldown==0 && SchoolTrainingCooldown==0);
        check(UnclassifiedWorldCountdownA==0 && UnclassifiedWorldCountdownB==0);
        explorationPhase=1; event.key.key=SDLK_SPACE;
    } else if(explorationPhase==1) {
        if(CurrentMenuLayoutIndex!=PauseMenuPanel) return;
        check(MenuControls[PauseMenuPanel].optionCount==PauseMenuBaseChoices);
        check(MenuControls[PauseMenuPanel].selection==0);
        explorationPauseSeen=1; explorationPhase=2; event.key.key=SDLK_RETURN;
        explorationFinanceAtPause=ComstarFinanceCountdown;
    } else {
        /* Sidebar drawing changes layout to1 after return. Layout4 is only
         * transient; a completed parent world tick proves actual resumption. */
        if(ComstarFinanceCountdown!=explorationFinanceAtPause) {
            explorationResumeSeen=1; event.type=SDL_EVENT_QUIT;
        } else event.key.key=SDLK_RETURN; /* After the original menu's input drain. */
    }
    check(SDL_PushEvent(&event));
}
static Uint32 SDLCALL requestExplorationInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    check(SDL_RunOnMainThread(operateExploration,NULL,false));
    return interval;
}
static void enterExploration(void) { Main_Game_Loop(FALSE); }
static void runExploration(void)
{
    SDL_TimerID timer;
    uint16_t fogIndex;
    uint8_t fogBefore[3],fogMask;
    Load_Game_Map_Data();
    check(CrescentHawkMapPositionX==CitadelStartingPositionX);
    check(CrescentHawkMapPositionY==CitadelStartingPositionY);
    check(!KuritaDestroyedCitadel && !InsideStarLeagueCache && !HasMapper);
    /* Synthetic countdown inputs, not substitute update routines. */
    PartyHealthRecoveryTimer=TrainingMissionAndQuizCooldown=SchoolTrainingCooldown=5;
    UnclassifiedWorldCountdownA=UnclassifiedWorldCountdownB=5;
    ComstarFinanceCountdown=255;
    fogIndex=(uint16_t)((((uint16_t)CitadelStartingPositionY&PackedPositionYRegionMask)>>5
        |((uint16_t)CitadelStartingPositionY&FogOfWarLocalRowMask))+(CitadelStartingPositionX>>8));
    fogMask=FogOfWarColumnBitMask[((uint16_t)CitadelStartingPositionX&FogOfWarLocalColumnMask)>>FogOfWarLocalColumnShift];
    fogBefore[0]=MapFogOfWar[fogIndex-MapFogOfWarRowBytes];
    fogBefore[1]=MapFogOfWar[fogIndex]; fogBefore[2]=MapFogOfWar[fogIndex+MapFogOfWarRowBytes];
    check((fogBefore[1]&fogMask)==0); /* The input iteration must actually reveal a new bit. */
    explorationPhase=explorationCallbacks=explorationPauseSeen=explorationResumeSeen=0;
    explorationInputActive=1;
    timer=SDL_AddTimer(10,requestExplorationInput,NULL); check(timer!=0);
    check(SDLBackend_RunApplication(enterExploration)==SDLBackend_ApplicationClosed);
    explorationInputActive=0; check(SDL_RemoveTimer(timer));
    check(explorationPauseSeen && explorationResumeSeen);
    check(PartyHealthRecoveryTimer==0 && TrainingMissionAndQuizCooldown==0 && SchoolTrainingCooldown==0);
    check(UnclassifiedWorldCountdownA==0 && UnclassifiedWorldCountdownB==0);
    check(ComstarFinanceCountdown<255 && MainCharactersAlive && !ExitMainLoop);
    check(CrescentHawkMapPositionX==CitadelStartingPositionX && CrescentHawkMapPositionY==CitadelStartingPositionY);
    check(MapFogOfWar[fogIndex-MapFogOfWarRowBytes]==(uint8_t)(fogBefore[0]|fogMask));
    check(MapFogOfWar[fogIndex]==(uint8_t)(fogBefore[1]|fogMask));
    check(MapFogOfWar[fogIndex+MapFogOfWarRowBytes]==(uint8_t)(fogBefore[2]|fogMask));
    puts("Connected exploration: real idle ticks, countdown expiry, pause/return, fog and SDL close completed.");
}
static unsigned totalArmour(const Mech *mech)
{
    unsigned total=0;
    for(unsigned location=0;location<MechArmourLocationCount;++location)
        total+=mech->currentArmour[location];
    return total;
}
/* Test-only operator: keep acknowledging native dialogs after their input
 * drain. The timer touches SDL events only, never game state. */
static Uint32 SDLCALL acknowledgeSalvage(void *userdata,SDL_TimerID timer,Uint32 interval)
{
    SDL_Event key;
    (void)userdata; (void)timer;
    memset(&key,0,sizeof key);
    key.type=SDL_EVENT_KEY_DOWN; key.key.key=SDLK_RETURN;
    SDL_PushEvent(&key);
    return interval;
}
static void runMechStatistics(void)
{
    Mech before;
    SDL_TimerID timer;
    /* Actual shipped CMP loader/decompressor, EGA text/gauges and SDL retraces.
     * The only operator assistance is an Enter event for the final key read. */
    Mechs[0]=MechRefs[MechRef_Wasp];
    Mechs[0].pilotId=Character_Jason; Mechs[0].riderId=MECH_NoRider;
    Characters[Character_Jason].name=Character_Jason;
    MechHeatLevel[0]=10; CBills=1234;
    before=Mechs[0]; BTStatsAssetLoaded=FALSE; DisableInput=TRUE;
    timer=SDL_AddTimer(20,acknowledgeSalvage,NULL); check(timer!=0);
    Examine_Screen_BTSTATS_CMP(0);
    check(SDL_RemoveTimer(timer));
    check(BTStatsAssetLoaded==TRUE && MechHeatLevel[0]==10 && CBills==1234);
    check(memcmp(&before,&Mechs[0],sizeof before)==0);
    check(SDLBackend_PresentEgaScreen());
    if(statisticsPreviewPath) {
        static uint32_t pixels[ScreenWidth*ScreenHeight];
        SDL_Surface *surface;
        /* Optional test-only export of actual scanout, after the routine's
         * default-palette restoration. No replacement drawing or game hook. */
        SDLBackend_DecodeEgaScreen(pixels);
        surface=SDL_CreateSurfaceFrom(ScreenWidth,ScreenHeight,SDL_PIXELFORMAT_RGBA32,
            pixels,ScreenWidth*4);
        check(surface!=NULL);
        if(!SDL_SaveBMP(surface,statisticsPreviewPath)) {
            fprintf(stderr,"Statistics preview export failed: %s\n",SDL_GetError());
            check(FALSE);
        }
        SDL_DestroySurface(surface);
    }
    puts("Actual Mech statistics: shipped CMP decoded/rendered, palette continuation and automatic dismissal returned without changing Mech or C-bills.");
}
static void runMedicalServices(void)
{
    enum { ServiceTier=3, StartingHealth=40, MaximumHealth=100 };
    SDL_TimerID timer=SDL_AddTimer(20,acknowledgeSalvage,NULL);
    check(timer!=0);
    for(unsigned scenario=0;scenario<3;++scenario) {
        uint32_t price=(uint32_t)MedicalServiceFee[ServiceTier];
        memset(&OriginalSavedState,0,sizeof OriginalSavedState);
        for(unsigned character=0;character<CharacterRecordCount;++character)
            Characters[character].name=Character_Dead;
        for(unsigned mech=0;mech<MechRecordCount;++mech) Mechs[mech].name[0]=MECH_Destroyed;
        Characters[Character_Jason].name=Character_Jason;
        Characters[Character_Jason].body=10; Characters[Character_Jason].health=StartingHealth;
        Characters[Character_Jason].mechAssignment=Character_OnFoot;
        SelectedMedicalServiceTier=ServiceTier;
        CBills=scenario==1?price-1:price;
        PartyHealthRecoveryTimer=scenario==2?HealingRecoveryWorldTicks:0;
        RandomByteLow=0x25; RandomByteMiddle=0x13; RandomByteHigh=0x90;
        Citadel_Building_Dialogs(CitadelAction_TreatParty);
        if(scenario==0) {
            uint8_t packedRoll=HealingDice[ServiceTier][MedEquip_HospitalFacilities-MedEquip_TornCloth];
            unsigned dice=packedRoll&15,multiplier=packedRoll>>4;
            unsigned minimum=StartingHealth+dice*(multiplier?multiplier:1);
            unsigned maximum=StartingHealth+dice*6*(multiplier?multiplier:1);
            if(minimum>MaximumHealth) minimum=MaximumHealth;
            if(maximum>MaximumHealth) maximum=MaximumHealth;
            check(CBills==0 && PartyHealthRecoveryTimer==HealingRecoveryWorldTicks);
            check(Characters[Character_Jason].health>=minimum && Characters[Character_Jason].health<=maximum);
        } else {
            check(Characters[Character_Jason].health==StartingHealth);
            check(RandomByteLow==0x25 && RandomByteMiddle==0x13 && RandomByteHigh==0x90);
            if(scenario==1) {
                check(CBills==price-1+HospitalFacilitiesRefund && PartyHealthRecoveryTimer==0);
            } else {
                /* Direct dispatcher charges first even when healing is gated.
                 * Actual HOSPITAL.BLD clears the timer before its treatment call. */
                check(CBills==0 && PartyHealthRecoveryTimer==HealingRecoveryWorldTicks);
            }
        }
    }
    check(SDL_RemoveTimer(timer));
    puts("Connected medical service: treatment, unaffordable refund and recovery gate preserved.");
}
static unsigned ammunitionInputSent,ammunitionRequest;
static void runRepairServices(void)
{
    enum { MissingArmourPoints=3,MissingStructurePoints=1,RepairScenarioCount=6 };
    SDL_TimerID timer=SDL_AddTimer(20,acknowledgeSalvage,NULL);
    check(timer!=0);
    for(unsigned scenario=0;scenario<RepairScenarioCount;++scenario) {
        Mech expected;
        uint32_t expectedBalance;
        memset(&OriginalSavedState,0,sizeof OriginalSavedState);
        for(unsigned mech=0;mech<MechRecordCount;++mech) Mechs[mech].name[0]=MECH_Destroyed;
        Mechs[0]=MechRefs[MechRef_Wasp]; Mechs[0].pilotId=Character_Jason;
        if(scenario<3) {
            unsigned total=MissingArmourPoints*MechRepairArmourPointCostCBills
                +MissingStructurePoints*MechRepairStructurePointCostCBills;
            check(Mechs[0].maxArmour[0]>=2 && Mechs[0].maxArmour[1]>=1);
            check(Mechs[0].maxStructure[0]>=MissingStructurePoints);
            Mechs[0].currentArmour[0]-=2; Mechs[0].currentArmour[1]-=1;
            Mechs[0].currentStructure[0]-=MissingStructurePoints;
            CBills=scenario==0?total:scenario==1?MechRepairArmourPointCostCBills-1:total-1;
            expected=Mechs[0]; expectedBalance=CBills;
            if(scenario!=1) {
                expected.currentArmour[0]=expected.maxArmour[0];
                expected.currentArmour[1]=expected.maxArmour[1];
                expectedBalance-=MissingArmourPoints*MechRepairArmourPointCostCBills;
                if(scenario==0) {
                    expected.currentStructure[0]=expected.maxStructure[0];
                    expectedBalance-=MissingStructurePoints*MechRepairStructurePointCostCBills;
                }
            }
        } else if(scenario<5) {
            unsigned sinkSlot=MechCriticalSlotCount;
            for(unsigned slot=0;slot<MechCriticalSlotCount;++slot)
                if(Mechs[0].criticalSlots[slot]==Heat_Sink) { sinkSlot=slot; break; }
            check(sinkSlot<MechCriticalSlotCount);
            check(Mechs[0].maxActuators[0]!=0 && Mechs[0].maxActuators[1]!=0);
            Mechs[0].criticalSlots[sinkSlot]=Destroyed_Heat_Sink;
            Mechs[0].currentActuators[0]=Mechs[0].currentActuators[1]=0;
            CBills=MechRepairHeatSinkCostCBills+MechRepairActuatorsCostCBills-(scenario==4?1u:0u);
            expected=Mechs[0]; expected.criticalSlots[sinkSlot]=Heat_Sink;
            expectedBalance=CBills-MechRepairHeatSinkCostCBills;
            if(scenario==3) {
                expected.currentActuators[0]=expected.maxActuators[0];
                expected.currentActuators[1]=expected.maxActuators[1];
                expectedBalance-=MechRepairActuatorsCostCBills;
            }
        } else {
            Mechs[0].engineHits=1; CBills=MechRepairHeatSinkCostCBills+MechRepairActuatorsCostCBills;
            expected=Mechs[0]; expectedBalance=CBills; /* Facility cannot repair engine hits. */
        }
        Menu_Memory_Variables(4);
        Citadel_Building_Dialogs(CitadelAction_RepairMech);
        check(SelectedMechId==0 && CBills==expectedBalance);
        check(!memcmp(&Mechs[0],&expected,sizeof expected));
        for(unsigned mech=1;mech<MechRecordCount;++mech) check(Mechs[mech].name[0]==MECH_Destroyed);
    }
    check(SDL_RemoveTimer(timer));
    puts("Connected Mechlub repair: full, unaffordable, partial, sinks, actuators and engine rejection completed.");
}
static void SDLCALL enterAmmunitionRequest(void *unused)
{
    SDL_Event key;
    (void)unused; check(SDL_IsMainThread());
    memset(&key,0,sizeof key); key.type=SDL_EVENT_KEY_DOWN;
    if(SDL_GetTicks()<nextAmmunitionInput) return;
    if(getenv("CHI_TEST_SLOW_OPERATOR")) SDL_Delay(22);
    if(!ammunitionInputSent && strcmp((char *)DynamicString,"0")==0) {
        key.key.key=(SDL_Keycode)('0'+ammunitionRequest);
        check(SDL_PushEvent(&key)); ammunitionInputSent=1;
    }
    key.key.key=SDLK_RETURN; check(SDL_PushEvent(&key));
    nextAmmunitionInput=SDL_GetTicks()+OperatorQuietMilliseconds;
}
static Uint32 SDLCALL requestAmmunitionInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    check(SDL_RunOnMainThread(enterAmmunitionRequest,NULL,false));
    return interval;
}
static void runAmmunitionServices(void)
{
    enum { MissingRounds=3,AmmoSlot=1 };
    unsigned price=(unsigned)MissileAmmoPriceByComponent[Mech_SRMissile2-Mech_LRMissile5];
    check(price>0);
    for(unsigned scenario=0;scenario<3;++scenario) {
        SDL_TimerID timer;
        unsigned funds=scenario==1?price-1:price*MissingRounds;
        memset(&OriginalSavedState,0,sizeof OriginalSavedState);
        for(unsigned mech=0;mech<MechRecordCount;++mech) Mechs[mech].name[0]=MECH_Destroyed;
        Mechs[0]=MechRefs[MechRef_Wasp]; Mechs[0].pilotId=Character_Jason;
        Mechs[0].currentAmmo[AmmoSlot]=(uint8_t)(Mechs[0].maxAmmo[AmmoSlot]-MissingRounds);
        CBills=funds; ammunitionInputSent=0; ammunitionRequest=scenario==2?9:MissingRounds;
        memcpy(DynamicString,"fixture",sizeof "fixture");
        Menu_Memory_Variables(4);
        timer=SDL_AddTimer(20,requestAmmunitionInput,NULL); check(timer!=0);
        Mechlube_Buy_Ammo();
        check(SDL_RemoveTimer(timer)); check(ammunitionInputSent && SelectedMechId==0);
        check(Mechs[0].currentAmmo[AmmoSlot]==Mechs[0].maxAmmo[AmmoSlot]-(scenario==1?MissingRounds:0));
        check(CBills==(scenario==1?funds:0));
    }
    puts("Connected ammunition service: purchase, insufficient funds and capacity cap preserved.");
}
static unsigned hospitalInputActive;
enum { HospitalRootLayout=6,HospitalRootMenuWithoutKits=28,
    HospitalStartingHealth=40,HospitalProfessionalHelpUnavailableState=0x27 };
static void SDLCALL enterHospitalChoice(void *unused)
{
    SDL_Event key;
    (void)unused; check(SDL_IsMainThread());
    if(!hospitalInputActive) return;
    memset(&key,0,sizeof key); key.type=SDL_EVENT_KEY_DOWN;
    if(CurrentMenuLayoutIndex==HospitalRootLayout &&
        Characters[Character_Jason].health>HospitalStartingHealth && PartyHealthRecoveryTimer==HealingRecoveryWorldTicks) {
        /* With neither kit owned, the script uses the seven-choice root.
         * Native south commands choose its last (exit) row after treatment. */
        key.key.key=SDLK_X;
        for(unsigned step=0;step<(unsigned)MenuControls[HospitalRootMenuWithoutKits].optionCount-1u;++step)
            check(SDL_PushEvent(&key));
    }
    key.key.key=SDLK_RETURN; check(SDL_PushEvent(&key));
}
static Uint32 SDLCALL requestHospitalInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    SDL_RunOnMainThread(enterHospitalChoice,NULL,false);
    return interval;
}
static void runHospitalScript(void)
{
    enum { HospitalBuilding=9,InitialFunds=100 };
    SDL_TimerID timer;
    memset(&OriginalSavedState,0,sizeof OriginalSavedState);
    for(unsigned character=0;character<CharacterRecordCount;++character) Characters[character].name=Character_Dead;
    for(unsigned mech=0;mech<MechRecordCount;++mech) Mechs[mech].name[0]=MECH_Destroyed;
    Characters[Character_Jason].name=Character_Jason; Characters[Character_Jason].body=10;
    Characters[Character_Jason].health=HospitalStartingHealth; Characters[Character_Jason].skillMedical=SkillLevel_Good;
    Characters[Character_Jason].mechAssignment=Character_OnFoot;
    CBills=InitialFunds; PartyHealthRecoveryTimer=HealingRecoveryWorldTicks;
    PersistentState.bytes[HospitalProfessionalHelpUnavailableState]=TRUE;
    AlternativeBldByBuildingId[HospitalBuilding]=HospitalBuilding;
    hospitalInputActive=1;
    timer=SDL_AddTimer(20,requestHospitalInput,NULL); check(timer!=0);
    Interact_with_BLD(HospitalBuilding);
    hospitalInputActive=0; check(SDL_RemoveTimer(timer));
    check(EnteredBuildingId==HospitalBuilding && BldFileIndex==HospitalBuilding);
    check(Characters[Character_Jason].health>HospitalStartingHealth && Characters[Character_Jason].health<=100);
    check(CBills==InitialFunds-HospitalFacilitiesFee && PartyHealthRecoveryTimer==HealingRecoveryWorldTicks);
    check(SelectedMedicalServiceTier==MedicalService_UsePartyMedicAndEquipment);
    puts("Connected HOSPITAL.BLD: clears old recovery, charges facilities, treats and exits natively.");
}
static void runMechs(uint16_t startingHeat,unsigned fatalTarget)
{
    unsigned damagedSinkOffset=0;
    enum { Friendly=0,Enemy=Enemy_All_CombatantId_Range_First,
        EnemyRecord=Enemy_Mech_Record_First,EnemyPilot=Enemy_Infantry_Record_First,
        FixtureX=0x0220,FixtureY=0x3020,NormalFinalHeat=15,ShutdownFinalHeat=20 };
    /* Intact Wasp:4 engine sinks +6 critical-slot sinks =10. The ordinary
     * round adds5 firing heat; no movement order contributes heat here.
     * Shutdown adds no firing heat, so30 cools to20 without spending ammo. */
    memset(&OriginalSavedState,0,sizeof OriginalSavedState);
    memset(CombatantActive,0,sizeof CombatantActive);
    memset(CombatMap,0,sizeof CombatMap);
    memset(CombatMovementOrders,255,sizeof CombatMovementOrders);
    memset(CombatMovementPlanBytes,CombatMovementPlanEnd,sizeof CombatMovementPlanBytes);
    memset(CombatWeaponTarget,255,sizeof CombatWeaponTarget);
    for(unsigned id=0;id<AllCombatantCount;++id)
        CombatantPackedX[id]=CombatantPackedY[id]=CombatantPosition_Unused;
    for(unsigned id=0;id<MechRecordCount;++id) Mechs[id].name[0]=MECH_Destroyed;
    for(unsigned id=0;id<CharacterRecordCount;++id) Characters[id].name=Character_Dead;
    Mechs[Friendly]=MechRefs[MechRef_Wasp]; Mechs[EnemyRecord]=MechRefs[MechRef_Wasp];
    Mechs[EnemyRecord].pilotId=EnemyPilot;
    if(fatalTarget) {
        Mech *damaged=&Mechs[fatalTarget==2?Friendly:EnemyRecord];
        memset(damaged->currentArmour,0,sizeof damaged->currentArmour);
        memset(damaged->currentStructure,0,sizeof damaged->currentStructure);
    }
    Characters[0].name=Character_Jason; Characters[0].health=100;
    Characters[0].body=10; Characters[0].dexterity=12;
    Characters[0].skillGunnery=5; Characters[0].skillPiloting=5;
    Characters[0].mechAssignment=Friendly;
    Characters[EnemyPilot]=Characters[0]; Characters[EnemyPilot].name=1;
    Characters[EnemyPilot].mechAssignment=EnemyRecord;
    if(fatalTarget>=3) {
        uint8_t *record=(uint8_t *)&Mechs[Friendly];
        for(unsigned slot=0;slot<MechCriticalSlotCount;++slot) {
            unsigned offset=MECH_ComponentBlock_Start+slot;
            if(record[offset]==Heat_Sink) {
                damagedSinkOffset=offset; record[offset]=Destroyed_Heat_Sink; break;
            }
        }
        check(damagedSinkOffset!=0);
        Characters[0].skillTech=SkillLevel_Average;
        CBills=100;
    }
    if(fatalTarget>=4) {
        /* Rex is an eligible on-foot pilot. The Excellent/Good distinction
         * decides whether the actual catastrophic enemy wreck can be recovered. */
        Characters[Character_Rex]=Characters[0];
        Characters[Character_Rex].name=Character_Rex;
        Characters[Character_Rex].mechAssignment=Character_OnFoot;
        Characters[Character_Rex].skillTech=fatalTarget==4?SkillLevel_Excellent:SkillLevel_Good;
    }
    CombatantActive[Friendly]=CombatantActive[Enemy]=TRUE;
    CombatantPackedX[Friendly]=FixtureX; CombatantPackedY[Friendly]=FixtureY;
    CombatantPackedX[Enemy]=FixtureX+8; CombatantPackedY[Enemy]=FixtureY;
    CrescentHawkMapPositionX=FixtureX; CrescentHawkMapPositionY=FixtureY;
    RandomByteLow=0x25; RandomByteMiddle=0x13; RandomByteHigh=0x90;
    MainCharactersAlive=TRUE; CombatDisplayGraphics=renderedCombat?1:CombatGraphics_None;
    CombatMessageVerbosity=CombatMessage_None; GraphicsAdapter=GraphicsAdapter_Ega;
    BlockingTileCodeThreshold=MapOrdinaryBlockingThreshold;
    MechHeatLevel[Friendly]=MechHeatLevel[EnemyRecord]=(int8_t)startingHeat;
    Map_Construct_Nine_Regions(0x32);
    Map_NineGrid_Parent(); PosXY_OffsetGrid(FixtureX,FixtureY);
    if(renderedCombat) {
        CombatSpeedSetting=0;
        Draw_Menu_MultiSelect();
        check(CombatantVisibleOnScreen[Friendly] && CombatantVisibleOnScreen[Enemy]);
    }
    unsigned armourBefore=totalArmour(&Mechs[Friendly])+totalArmour(&Mechs[EnemyRecord]);
    Combat_Computer_Control(Friendly,FALSE); Combat_Computer_Control(Enemy,FALSE);
    if(startingHeat==MechHeatShutdownLevel) {
        check(CombatWeaponTarget[Friendly*CombatWeaponTargetSlots]==UINT8_MAX);
        check(CombatWeaponTarget[Enemy*CombatWeaponTargetSlots]==UINT8_MAX);
    } else {
        check(CombatWeaponTarget[Friendly*CombatWeaponTargetSlots]==Enemy);
        check(CombatWeaponTarget[Enemy*CombatWeaponTargetSlots]==Friendly);
    }
    Combat_Mechanics(FALSE);
    if(fatalTarget) {
        uint16_t record=fatalTarget==2?Friendly:EnemyRecord;
        uint16_t actor=fatalTarget==2?Friendly:Enemy;
        check(Mechs[record].name[0]==MECH_Destroyed && DestroyedMechNameInitial[record]=='W');
        check(!CombatantActive[actor] && CombatantCasualtyFlags[actor]);
        check(MainCharactersAlive && Characters[0].name==Character_Jason);
        if(fatalTarget==2) {
            check(Characters[0].mechAssignment==Character_OnFoot);
            check(CombatantActive[Friendly_Infantry_Combatant_Range_First]);
            check(CombatantPackedX[Friendly_Infantry_Combatant_Range_First]==FixtureX);
            check(CombatantPackedY[Friendly_Infantry_Combatant_Range_First]==FixtureY);
        } else {
            /* Native enemy ejection retires the Mech but does not materialize
             * enemy infantry or rewrite its pilot's stored assignment. */
            check(Characters[EnemyPilot].mechAssignment==EnemyRecord);
            check(!CombatantActive[Enemy_Infantry_CombatantId_Range_First]);
        }
        check(MapEffectSpriteIndex[0]==Sprite_Locust_Wreck);
        /* Effects cleanup1AE8:1E03 follows ejection: coordinates of the retired
         * Mech becomeFFFF, while the crew retains its ejection position. */
        check(CombatantPackedX[actor]==CombatantPosition_Unused);
        check(CombatantPackedY[actor]==CombatantPosition_Unused);
        check(CrescentHawkMapPositionX==FixtureX && CrescentHawkMapPositionY==FixtureY);
        if(fatalTarget==3) {
            SDL_Event key;
            memset(&key,0,sizeof key);
            key.type=SDL_EVENT_KEY_DOWN; key.key.key=SDLK_RETURN;
            check(((uint8_t *)&Mechs[Friendly])[damagedSinkOffset]==Destroyed_Heat_Sink);
            check(SDL_PushEvent(&key));
            Salvage_Armour_Dialog();
            check(((uint8_t *)&Mechs[Friendly])[damagedSinkOffset]==Heat_Sink);
            check(CombatantCasualtyFlags[Enemy] && Mechs[EnemyRecord].name[0]==MECH_Destroyed);
            check(CBills==100); /* Native slot-zero technician suppresses scrap payout. */
            puts("Connected salvage: actual enemy wreck feeds sink repair; slot-zero payout bug preserved.");
        }
        if(fatalTarget>=4) {
            SDL_TimerID timer=SDL_AddTimer(20,acknowledgeSalvage,NULL);
            check(timer!=0);
            Salvage_Mechs_Dialog();
            check(SDL_RemoveTimer(timer));
            check(!CombatantCasualtyFlags[Enemy]); /* Rejected wrecks are also consumed. */
            check(Mechs[EnemyRecord].name[0]==MECH_Destroyed && CBills==100);
            if(fatalTarget==4) {
                check(Mechs[1].name[0]=='W' && Mechs[1].pilotId==Character_Rex);
                check(Characters[Character_Rex].mechAssignment==1);
                check(Mechs[1].engineHits==1 && Mechs[1].gyroHits==1);
                check(Mechs[1].currentStructure[3]==1 && Mechs[1].currentStructure[4]==1);
                check(Mechs[1].currentAmmo[1]==Mechs[EnemyRecord].currentAmmo[1]);
                check(CombatantSpriteFamilyOffset[1]==MECH_Sprite_COMMANDO); /* Native non-L fallback. */
                puts("Connected whole-Mech salvage: Excellent Tech recovers actual wreck and binds Rex.");
            } else {
                check(Mechs[1].name[0]==MECH_Destroyed);
                check(Characters[Character_Rex].mechAssignment==Character_OnFoot);
                puts("Connected whole-Mech salvage: Good Tech rejects catastrophic wreck; flag consumed.");
            }
        }
        puts(fatalTarget==2?"Connected destruction: friendly pilot ejected on foot; wreck registered."
            :"Connected destruction: enemy Mech retired, crew assignment preserved; wreck registered.");
        return;
    }
    if(startingHeat==MechHeatShutdownLevel) {
        check(Mechs[Friendly].currentAmmo[1]==MechRefs[MechRef_Wasp].currentAmmo[1]);
        check(Mechs[EnemyRecord].currentAmmo[1]==MechRefs[MechRef_Wasp].currentAmmo[1]);
        check(totalArmour(&Mechs[Friendly])+totalArmour(&Mechs[EnemyRecord])==armourBefore);
        check(MechHeatLevel[Friendly]==ShutdownFinalHeat && MechHeatLevel[EnemyRecord]==ShutdownFinalHeat);
    } else {
        check(Mechs[Friendly].currentAmmo[1]==MechRefs[MechRef_Wasp].currentAmmo[1]-1);
        check(Mechs[EnemyRecord].currentAmmo[1]==MechRefs[MechRef_Wasp].currentAmmo[1]-1);
        check(totalArmour(&Mechs[Friendly])<totalArmour(&MechRefs[MechRef_Wasp]));
        check(totalArmour(&Mechs[EnemyRecord])<totalArmour(&MechRefs[MechRef_Wasp]));
        if(renderedCombat) {
            /* Real rendering populates terrain overlap/cooling. Animation
             * choices also consume native RNG and can change critical hits:
             * do not require the graphics-disabled encounter's heat totals. */
            for(unsigned side=0;side<2;++side) {
                unsigned record=side?EnemyRecord:Friendly,actor=side?Enemy:Friendly;
                Mech *mech=&Mechs[record];
                int expectedHeat=20+5+(int)mech->engineHits*MechEngineHitHeatPerRound-mech->engineHeatSinks;
                for(unsigned offset=MECH_ComponentBlock_Start;offset<=MECH_ComponentBlock_End;++offset)
                    if(((uint8_t *)mech)[offset]==Heat_Sink) --expectedHeat;
                if(TerrainOverlapRows[actor] && MapTileUnderCombatant[actor]<CombatCoolingTerrainTileEnd)
                    expectedHeat-=MechTerrainCoolingHeatPerRound;
                check(MechHeatLevel[record]==expectedHeat);
            }
        } else check(MechHeatLevel[Friendly]==NormalFinalHeat && MechHeatLevel[EnemyRecord]==NormalFinalHeat);
    }
    check(CrescentHawkMapPositionX==FixtureX && CrescentHawkMapPositionY==FixtureY);
    printf("Connected Wasp combat: starting heat %u, final heat %d/%d, SRM ammo %u/%u.\n",
        (unsigned)startingHeat,(int)MechHeatLevel[Friendly],(int)MechHeatLevel[EnemyRecord],
        (unsigned)Mechs[Friendly].currentAmmo[1],(unsigned)Mechs[EnemyRecord].currentAmmo[1]);
}

static void runMechAutoplayRegression(void)
{
    enum { FriendlyCount=3,EnemyActor=Enemy_All_CombatantId_Range_First,
        EnemyRecord=Enemy_Mech_Record_First,EnemyPilot=Enemy_Infantry_Record_First,
        FixtureX=0x0220,FixtureY=0x3020,MaximumRounds=8 };
    static const uint8_t templates[FriendlyCount]={MECH_REF_Locust,MechRef_Stinger,MechRef_Commando};
    static const uint16_t y[FriendlyCount]={FixtureY-8,FixtureY,FixtureY+8};
    unsigned fired=0;

    memset(&OriginalSavedState,0,sizeof OriginalSavedState);
    memset(CombatantActive,0,sizeof CombatantActive);
    memset(CombatantActionState,0,sizeof CombatantActionState);
    memset(CombatMovementOrders,255,sizeof CombatMovementOrders);
    memset(CombatMovementPlanBytes,CombatMovementPlanEnd,sizeof CombatMovementPlanBytes);
    memset(CombatWeaponTarget,255,sizeof CombatWeaponTarget);
    memset(CombatantCasualtyFlags,0,sizeof CombatantCasualtyFlags);
    memset(MechHeatLevel,0,sizeof MechHeatLevel);
    for(unsigned id=0;id<AllCombatantCount;++id)
        CombatantPackedX[id]=CombatantPackedY[id]=CombatantPosition_Unused;
    for(unsigned id=0;id<MechRecordCount;++id) Mechs[id].name[0]=MECH_Destroyed;
    for(unsigned id=0;id<CharacterRecordCount;++id) Characters[id].name=Character_Dead;

    for(unsigned actor=0;actor<FriendlyCount;++actor) {
        Mechs[actor]=MechRefs[templates[actor]]; Mechs[actor].pilotId=(uint8_t)actor;
        memset(Mechs[actor].currentArmour,100,sizeof Mechs[actor].currentArmour);
        memset(Mechs[actor].currentStructure,100,sizeof Mechs[actor].currentStructure);
        Characters[actor].name=(uint8_t)actor; Characters[actor].health=100;
        Characters[actor].body=10; Characters[actor].dexterity=12;
        Characters[actor].skillGunnery=5; Characters[actor].skillPiloting=5;
        Characters[actor].mechAssignment=(uint8_t)actor;
        CombatantActive[actor]=TRUE;
        CombatantPackedX[actor]=FixtureX; CombatantPackedY[actor]=y[actor];
    }
    Mechs[EnemyRecord]=MechRefs[MechRef_Wasp]; Mechs[EnemyRecord].pilotId=EnemyPilot;
    memset(Mechs[EnemyRecord].currentArmour,100,sizeof Mechs[EnemyRecord].currentArmour);
    memset(Mechs[EnemyRecord].currentStructure,100,sizeof Mechs[EnemyRecord].currentStructure);
    Characters[EnemyPilot]=Characters[0]; Characters[EnemyPilot].name=EnemyPilot;
    Characters[EnemyPilot].mechAssignment=EnemyRecord;
    CombatantActive[EnemyActor]=TRUE;
    CombatantPackedX[EnemyActor]=FixtureX+0x24; CombatantPackedY[EnemyActor]=FixtureY;

    CrescentHawkMapPositionX=FixtureX; CrescentHawkMapPositionY=FixtureY;
    RandomByteLow=0x25; RandomByteMiddle=0x13; RandomByteHigh=0x90;
    MainCharactersAlive=TRUE; CombatDisplayGraphics=CombatGraphics_None;
    CombatMessageVerbosity=CombatMessage_None; GraphicsAdapter=GraphicsAdapter_Ega;
    BlockingTileCodeThreshold=MapOrdinaryBlockingThreshold;
    Map_Construct_Nine_Regions(0x32);
    Map_NineGrid_Parent(); PosXY_OffsetGrid(FixtureX,FixtureY);
    /* Keep this a planner/executor test, independent of procedural obstacles. */
    memset(CombatMap,0,sizeof CombatMap);

    for(unsigned round=0;round<MaximumRounds;++round) {
        uint16_t beforeX[FriendlyCount];
        unsigned enemyBefore=totalArmour(&Mechs[EnemyRecord]);
        uint8_t ammoBefore[FriendlyCount][MechWeaponOrdinalCount];
        for(unsigned actor=0;actor<FriendlyCount;++actor) {
            beforeX[actor]=CombatantPackedX[actor];
            memcpy(ammoBefore[actor],Mechs[actor].currentAmmo,MechWeaponOrdinalCount);
        }
        Combat_Plan_Computer_Side(0);
        Combat_Plan_Computer_Side(EnemyActor);
        for(unsigned actor=0;actor<FriendlyCount;++actor) {
            uint16_t plan=(uint16_t)(actor*CombatMovementPlanBytesPerUnit);
            check(CombatWeaponTarget[actor*CombatWeaponTargetSlots]==EnemyActor);
            for(unsigned byte=0;byte<CombatMovementPlanBytesPerUnit;byte+=2) {
                if(CombatMovementPlanBytes[plan+byte]==CombatMovementPlanEnd) break;
                check((int8_t)CombatMovementPlanBytes[plan+byte]>=0);
            }
        }
        Combat_Mechanics(FALSE);
        for(unsigned actor=0;actor<FriendlyCount;++actor) {
            /* The enemy moves concurrently and can make the range metric rise
             * by one while sidestepping.  Test the friendly's own eastward
             * approach, not a false stationary-target assumption. */
            check(CombatantPackedX[actor]>=beforeX[actor]);
            if(memcmp(ammoBefore[actor],Mechs[actor].currentAmmo,MechWeaponOrdinalCount)!=0) fired=1;
        }
        if(totalArmour(&Mechs[EnemyRecord])<enemyBefore) fired=1;
    }
    check(fired);
    printf("Connected autoplay: Locust/Stinger/Commando close on one Wasp for %u rounds without retreating; attacks execute.\n",MaximumRounds);
}
int main(int argc,char **argv)
{
    if(argc==3 && strcmp(argv[1],"load-existing-save")==0) {
        existingSaveName=argv[2]; argc=2;
    }
    if(argc==3 && strcmp(argv[1],"mech-statistics")==0) {
        statisticsPreviewPath=argv[2]; argc=2;
    }
    enum { Friendly=Friendly_Infantry_Combatant_Range_First,
        Enemy=Enemy_Infantry_CombatantId_Range_First, EnemyCharacter=Enemy_Infantry_Record_First,
        MaximumRounds=8, FixtureX=0x0220,FixtureY=0x3020 };
    unsigned rounds=0;
    check(SDLBackend_Open());
    /* Gameplay integration checks rules and presentation dispatch, not human
     * listening duration. Keep them bounded at the former DOSBox-like rate;
     * test_platform separately certifies the playable 240-cycles/ms default. */
    SDLBackend_SetSoundCpuCyclesPerMillisecond(SDLBackend_SoundCpuDosBoxNostalgia);
    SDLBackend_RetracesPerSecond=0;
    if(argc==2 && (strcmp(argv[1],"rendered-mechs")==0 || strcmp(argv[1],"medical-services")==0
        || strcmp(argv[1],"ammunition-services")==0 || strcmp(argv[1],"hospital-script")==0
        || strcmp(argv[1],"animation-scenes")==0 || strcmp(argv[1],"training-mission")==0
        || strcmp(argv[1],"exploration-loop")==0 || strcmp(argv[1],"repair-services")==0
        || strcmp(argv[1],"mech-statistics")==0 || strcmp(argv[1],"load-existing-save")==0)) {
        loadCombatAssets();
        if(strcmp(argv[1],"medical-services")==0) runMedicalServices();
        else if(strcmp(argv[1],"ammunition-services")==0) runAmmunitionServices();
        else if(strcmp(argv[1],"hospital-script")==0) runHospitalScript();
        else if(strcmp(argv[1],"animation-scenes")==0) runAnimationScenes();
        else if(strcmp(argv[1],"training-mission")==0) runTrainingMission();
        else if(strcmp(argv[1],"exploration-loop")==0) runExploration();
        else if(strcmp(argv[1],"repair-services")==0) runRepairServices();
        else if(strcmp(argv[1],"mech-statistics")==0) runMechStatistics();
        else if(strcmp(argv[1],"load-existing-save")==0) { check(existingSaveName!=NULL); runExistingSave(); }
        else { renderedCombat=1; runMechs(20,0); }
        check(SDLBackend_PresentEgaScreen());
        for(unsigned sprite=0;sprite<CombatSpriteCount;++sprite) {
            SDL_free(CombatSpritePointers[sprite]); CombatSpritePointers[sprite]=NULL;
        }
        SDL_free(BorderTileset); BorderTileset=NULL;
        SDL_free(TinylandTileset); TinylandTileset=NULL;
        SDLBackend_Close(); return 0;
    }
    if(argc==2 && (strcmp(argv[1],"mechs")==0 || strcmp(argv[1],"shutdown")==0
        || strcmp(argv[1],"enemy-wreck")==0 || strcmp(argv[1],"ejection")==0
        || strcmp(argv[1],"salvage")==0 || strcmp(argv[1],"recover")==0
        || strcmp(argv[1],"reject-wreck")==0 || strcmp(argv[1],"autoplay-mechs")==0)) {
        if(strcmp(argv[1],"autoplay-mechs")==0) {
            runMechAutoplayRegression(); SDLBackend_Close(); return 0;
        }
        unsigned fatalTarget=strcmp(argv[1],"enemy-wreck")==0?1u:strcmp(argv[1],"ejection")==0?2u:
            strcmp(argv[1],"salvage")==0?3u:strcmp(argv[1],"recover")==0?4u:
            strcmp(argv[1],"reject-wreck")==0?5u:0u;
        runMechs(strcmp(argv[1],"shutdown")==0?MechHeatShutdownLevel:20,fatalTarget);
        SDLBackend_Close(); return 0;
    }
    check(argc==1);
    /* Synthetic encounter inputs only. No game-method doubles: actual RNG,
     * planning, path/occupancy, twelve-slice execution, dice and casualty code. */
    memset(&OriginalSavedState,0,sizeof OriginalSavedState);
    memset(CombatantActive,0,sizeof CombatantActive);
    memset(CombatMap,0,sizeof CombatMap);
    memset(CombatMovementOrders,255,sizeof CombatMovementOrders);
    memset(CombatMovementPlanBytes,CombatMovementPlanEnd,sizeof CombatMovementPlanBytes);
    memset(CombatWeaponTarget,255,sizeof CombatWeaponTarget);
    memset(CombatantCasualtyFlags,0,sizeof CombatantCasualtyFlags);
    for(unsigned id=0;id<AllCombatantCount;++id)
        CombatantPackedX[id]=CombatantPackedY[id]=CombatantPosition_Unused;
    for(unsigned id=0;id<MechRecordCount;++id) Mechs[id].name[0]=MECH_Destroyed;
    for(unsigned id=0;id<CharacterRecordCount;++id) Characters[id].name=Character_Dead;
    Characters[0].name=Character_Jason; Characters[0].health=100;
    Characters[0].body=10; Characters[0].dexterity=12;
    Characters[0].weapon=WeaponIndex_Pistol; Characters[0].skillPistol=5;
    Characters[0].mechAssignment=Character_OnFoot;
    Characters[EnemyCharacter]=Characters[0];
    Characters[EnemyCharacter].name=1; Characters[EnemyCharacter].health=20;
    CombatantActive[Friendly]=CombatantActive[Enemy]=TRUE;
    CombatantPackedX[Friendly]=FixtureX; CombatantPackedY[Friendly]=FixtureY;
    CombatantPackedX[Enemy]=FixtureX+8; CombatantPackedY[Enemy]=FixtureY;
    CrescentHawkMapPositionX=FixtureX; CrescentHawkMapPositionY=FixtureY;
    RandomByteLow=0x25; RandomByteMiddle=0x13; RandomByteHigh=0x90;
    MainCharactersAlive=TRUE; CombatDisplayGraphics=CombatGraphics_None;
    CombatMessageVerbosity=CombatMessage_None; GraphicsAdapter=GraphicsAdapter_Ega;
    BlockingTileCodeThreshold=MapOrdinaryBlockingThreshold;
    Map_Construct_Nine_Regions(0x32);
    Map_NineGrid_Parent(); PosXY_OffsetGrid(FixtureX,FixtureY);
    while(CombatantActive[Enemy] && rounds<MaximumRounds) {
        Combat_Computer_Control(Friendly,FALSE);
        Combat_Computer_Control(Enemy,FALSE);
        check(CombatWeaponTarget[Friendly*CombatWeaponTargetSlots]==Enemy);
        check(CombatWeaponTarget[Enemy*CombatWeaponTargetSlots]==Friendly);
        Combat_Mechanics(FALSE);
        check(CrescentHawkMapPositionX==FixtureX && CrescentHawkMapPositionY==FixtureY);
        ++rounds;
    }
    check(rounds>0 && rounds<MaximumRounds);
    check(Characters[EnemyCharacter].name==Character_Dead);
    check(Characters[EnemyCharacter].health==0 && CombatantCasualtyFlags[Enemy]);
    check(!CombatantActive[Enemy] && CombatantActive[Friendly] && MainCharactersAlive);
    check(Characters[0].health>0 && Characters[0].health<100); /* actual enemy return fire */
    check(CombatantPackedX[Friendly]>FixtureX || CombatantPackedX[Enemy]<FixtureX+8);
    check(CombatWeaponTarget[Friendly*CombatWeaponTargetSlots]==UINT8_MAX);
    printf("Connected combat: actual AI/targeting/execution/RNG/damage/casualty completed in %u rounds.\n",rounds);
    SDLBackend_Close();
    return 0;
}
