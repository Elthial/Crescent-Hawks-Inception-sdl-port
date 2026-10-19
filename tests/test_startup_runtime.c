#include "game.h"
#include "backend.h"
#include "dos.h"
#include "music.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real original startup and SDL backend, no game-method test doubles. */
static void check(int condition)
{
    if(!condition) { fputs("Original startup runtime mismatch\n",stderr); exit(1); }
}
static unsigned continued;
static void queueKey(SDL_Keycode key,SDL_Scancode scan)
{
    SDL_Event event={0};
    event.type=SDL_EVENT_KEY_DOWN; event.key.key=key; event.key.scancode=scan;
    check(SDL_PushEvent(&event));
}
static void queueQuit(void)
{
    SDL_Event event={0}; event.type=SDL_EVENT_QUIT;
    check(SDL_PushEvent(&event));
}
static void readEscape(void)
{
    check(Keyboard_Get_ASCII_Hex_Input()==27); ++continued;
}
static void waitRetrace(void)
{
    Wait_For_N_Vertical_Retraces(1); ++continued;
}
static void probeInput(void)
{
    (void)Pending_Input(); ++continued;
}
static unsigned milestoneSeen;
static SDL_AtomicInt startupInputPulses;
static unsigned gameplayProbe,gameplayEntered;
static unsigned explorationProbe,movementQueued,explorationObserved;
static uint64_t nextMovementPulse;
static unsigned nativeQuitProbe,pauseQueued;
static unsigned pauseMenuObserved;
static uint16_t startingNpcX,startingNpcY;
enum { ExplorationLowerPanel=4, EntryProbeHostRetracesPerSecond=1000 };
static void SDLCALL checkStartupMilestone(void *unused)
{
    (void)unused;
    check(SDL_IsMainThread());
    if(milestoneSeen) {
        if(!gameplayProbe || CurrentMap!=Map_Citadel) return;
        if(gameplayEntered) {
            if(nativeQuitProbe) {
                if(!pauseMenuObserved && CurrentMenuLayoutIndex==ExplorationLowerPanel) {
                    check(MenuControls[PauseMenuPanel].selection==0);
                    check(MenuControls[GameSettingsMenu].selection==0);
                    pauseQueued=1; queueKey(SDLK_SPACE,SDL_SCANCODE_SPACE);
                } else if(CurrentMenuLayoutIndex==PauseMenuPanel && !ExitMainLoop) {
                    pauseQueued=pauseMenuObserved=1;
                    /* Original menus commit selection only on confirmation.
                     * Observe that result, then navigate the next real menu.
                     * No direct selection writes or menu-method doubles. */
                    if(MenuControls[PauseMenuPanel].selection!=PauseMenu_Settings) {
                        queueKey(SDLK_DOWN,SDL_SCANCODE_DOWN);
                    } else if(MenuControls[GameSettingsMenu].selection!=GameSettings_Quit) {
                        for(unsigned step=0;step<GameSettings_Quit;++step)
                            queueKey(SDLK_DOWN,SDL_SCANCODE_DOWN);
                    } else queueKey(SDLK_Y,SDL_SCANCODE_Y);
                    queueKey(SDLK_RETURN,SDL_SCANCODE_RETURN);
                }
                return;
            }
            if(!explorationProbe) return;
            /* Layout4 can be observed during scene15, before its input drain.
             * Retry a swallowed pulse until actual movement acknowledges it. */
            if(CrescentHawkMapPositionY==CitadelStartingPositionY
                && CurrentMenuLayoutIndex==ExplorationLowerPanel
                && SDL_GetTicks()>=nextMovementPulse) {
                movementQueued=1;
                nextMovementPulse=SDL_GetTicks()+50;
                queueKey(SDLK_X,SDL_SCANCODE_X); /* original south movement */
            }
            if(movementQueued && CrescentHawkMapPositionY!=CitadelStartingPositionY
                && RoamingMapNpcs[1].movementDelay==0
                && (CombatantPackedX[Enemy_Infantry_CombatantId_Range_First+1]!=startingNpcX
                    || CombatantPackedY[Enemy_Infantry_CombatantId_Range_First+1]!=startingNpcY)) {
                explorationObserved=1; queueQuit();
            }
            return;
        }
        if(strcmp((char *)DynamicString,"O15.ANM")==0 && AnimationFrameNumber>0) {
            gameplayEntered=1;
            startingNpcX=CombatantPackedX[Enemy_Infantry_CombatantId_Range_First+1];
            startingNpcY=CombatantPackedY[Enemy_Infantry_CombatantId_Range_First+1];
            if(!explorationProbe && !nativeQuitProbe) queueQuit();
        } else if(CurrentMenuLayoutIndex==TextPanel_FirstTimePlayer
            && strcmp((char *)DynamicString,"O15.ANM")!=0) {
            /* With zero-duration retraces the entire animation can finish
             * between timer callbacks. Bound host pacing during entry so the
             * first decoded frame is observable; this is not native timing. */
            SDLBackend_RetracesPerSecond=EntryProbeHostRetracesPerSecond;
            /* Prompt drains pending keys before reading. Repeated SDL Y events
             * confirm first-time play only before animation entry. The panel
             * ID survives into frame zero: injecting Y there leaves stray keys
             * ahead of exploration input, whose native drain discards it. */
            queueKey(SDLK_Y,SDL_SCANCODE_Y);
        }
        return;
    }
    if(MusicNoteTickReload!=MusicPcCadenceTicks) return;
    for(unsigned i=0;i<CombatSpriteCount;++i) if(CombatSpritePointers[i]==NULL) return;
    milestoneSeen=1;
    if(gameplayProbe) queueKey(SDLK_SPACE,SDL_SCANCODE_SPACE);
    else queueQuit();
}
static Uint32 SDLCALL requestMilestoneCheck(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    /* Worker schedules a main-thread callback, never reads game globals or
     * jumps across threads. Close only after the actual startup milestone,
     * not an assumed CPU-speed-dependent elapsed time. */
    check(SDL_RunOnMainThread(checkStartupMilestone,NULL,false));
    return interval;
}
static void SDLCALL pulseStartupInput(void *unused)
{
    (void)unused;
    /* A delayed timer pulse must not become a gameplay SPACE/pause command. */
    if(!milestoneSeen) queueKey(SDLK_SPACE,SDL_SCANCODE_SPACE);
}
static Uint32 SDLCALL requestStartupInput(void *unused,SDL_TimerID timer,Uint32 interval)
{
    (void)unused; (void)timer;
    check(SDL_RunOnMainThread(pulseStartupInput,NULL,false));
    return SDL_AddAtomicInt(&startupInputPulses,-1)>1?interval:0;
}
static void releaseStartupAssets(void)
{
    for(unsigned i=0;i<CombatSpriteCount;++i) {
        SDL_free(CombatSpritePointers[i]); CombatSpritePointers[i]=NULL;
    }
    SDL_free(BorderTileset); BorderTileset=NULL;
    SDL_free(TinylandTileset); TinylandTileset=NULL;
}
int main(int argc,char **argv)
{
    const char *previewPath=NULL;
    if(argc==3) {
        check(strcmp(argv[1],"new-game")==0 || strcmp(argv[1],"exploration")==0);
        previewPath=argv[2]; argc=2;
    }
    check(SDLBackend_Open());
    if(argc==2 && (strcmp(argv[1],"assets")==0 || strcmp(argv[1],"new-game")==0
        || strcmp(argv[1],"exploration")==0 || strcmp(argv[1],"settings-quit")==0)) {
        SDL_TimerID timer,inputTimer;
        /* Installed-mode drive state is automatic. Accelerated retraces are a
         * host test setting, NOT timing parity. */
        SDLBackend_RetracesPerSecond=EntryProbeHostRetracesPerSecond;
        explorationProbe=(unsigned)(strcmp(argv[1],"exploration")==0);
        nativeQuitProbe=(unsigned)(strcmp(argv[1],"settings-quit")==0);
        gameplayProbe=(unsigned)(strcmp(argv[1],"new-game")==0 || explorationProbe || nativeQuitProbe);
        /* Sparse user-like key pulses bridge the unknown host duration between
         * the startup drain and title input without continuously flooding the
         * native drain loop. */
        SDL_SetAtomicInt(&startupInputPulses,20);
        inputTimer=SDL_AddTimer(5,requestStartupInput,NULL); check(inputTimer!=0);
        /* Quit confirmation needs separate user-like batches: flooding keys
         * during the native drain can keep it busy instead of reaching input. */
        timer=SDL_AddTimer(nativeQuitProbe?100:10,requestMilestoneCheck,NULL); check(timer!=0);
        check(SDLBackend_RunApplication(Setup_Game)==(nativeQuitProbe ?
            SDLBackend_ApplicationReturned : SDLBackend_ApplicationClosed));
        (void)SDL_RemoveTimer(timer);
        (void)SDL_RemoveTimer(inputTimer); /* Pulse timer normally exhausts itself. */
        check(milestoneSeen==1 && MusicNoteTickReload==MusicPcCadenceTicks);
        check(HasHardDisk==TRUE && SecondFloppyDriveAvailable==FALSE);
        check(GraphicsAdapter==GraphicsAdapter_Ega && BorderTileset!=NULL && TinylandTileset!=NULL);
        for(unsigned i=0;i<CombatSpriteCount;++i) check(CombatSpritePointers[i]!=NULL);
        check(CombatSpritePointers[375][SpriteHeightMinusOneByte]==10);
        check(CombatSpritePointers[375][SpriteWidthByte]==2);
        if(gameplayProbe) {
            check(gameplayEntered==1 && DisableInput==FALSE && MainCharactersAlive==TRUE);
            check(CurrentMap==Map_Citadel);
            if(explorationProbe) {
                check(explorationObserved==1 && movementQueued==1);
                check(CrescentHawkMapPositionX==CitadelStartingPositionX);
                check(CrescentHawkMapPositionY==CitadelStartingPositionY+ExplorationStepsPerInput);
                check(CBills==NewGameStartingCBills+ComstarAllowancePaymentCBills);
                puts("Actual exploration: SDL south input moved Jason, the original allowance paid, and world ticks materialized a roaming NPC.");
            } else {
                if(!nativeQuitProbe) check(CBills==NewGameStartingCBills);
                check(CrescentHawkMapPositionX==CitadelStartingPositionX && CrescentHawkMapPositionY==CitadelStartingPositionY);
            }
            check(Characters[Character_Jason].name==Character_Jason);
            check(Characters[Character_Jason].health==JasonStartingBody*CharacterHealthPerBodyPoint);
            check(Characters[Character_Jason].mechAssignment==Character_OnFoot);
            for(unsigned i=1;i<PartySize;++i) check(Characters[i].name==Character_Dead);
            for(unsigned i=0;i<MechRecordCount;++i) check(Mechs[i].name[0]==MECH_Destroyed);
            check(GameSeeds==MapRuntime.bytes+(0x09FB-MapRuntimeFirstOffset));
            puts("Real new-game entry initialized Jason/C-bills, constructed the Citadel map and played the first door-animation frame.");
        }
        releaseStartupAssets();
        if(nativeQuitProbe) {
            check(pauseQueued && ExitMainLoop==TRUE);
            check(MenuControls[PauseMenuPanel].selection==PauseMenu_Settings);
            check(MenuControls[GameSettingsMenu].selection==GameSettings_Quit);
            puts("Original exploration/pause/settings/Yes workflow returned normally through Setup_Game, without SDL QUIT.");
        } else puts("Real startup loaded external artwork, captured all376 sprites, and returned on SDL window close.");
    } else {
        check(argc==1);
        /* Gameplay ESC is a key, not a window-close request. */
        queueKey(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE);
        check(SDLBackend_RunApplication(readEscape)==SDLBackend_ApplicationReturned && continued==1);
        continued=0; queueQuit();
        check(SDLBackend_RunApplication(Setup_Game)==SDLBackend_ApplicationClosed);
        check(HasHardDisk==TRUE && SecondFloppyDriveAvailable==FALSE && continued==0);
        queueQuit(); check(SDLBackend_RunApplication(waitRetrace)==SDLBackend_ApplicationClosed && continued==0);
        queueQuit(); check(SDLBackend_RunApplication(probeInput)==SDLBackend_ApplicationClosed && continued==0);
        puts("Normal return, gameplay ESC and window close at original startup/input/retrace boundaries passed.");
    }
    if(previewPath) {
        static uint32_t pixels[ScreenWidth*ScreenHeight];
        SDL_Surface *surface;
        /* Test-only retained scanout after the scripted application close.
         * Capture never replaces the original map/sprite/sidebar compositor. */
        SDLBackend_DecodeEgaScreen(pixels);
        surface=SDL_CreateSurfaceFrom(ScreenWidth,ScreenHeight,SDL_PIXELFORMAT_RGBA32,
            pixels,ScreenWidth*4);
        check(surface!=NULL);
        if(!SDL_SaveBMP(surface,previewPath)) {
            fprintf(stderr,"Exploration preview export failed: %s\n",SDL_GetError());
            check(FALSE);
        }
        SDL_DestroySurface(surface);
    }
    SDLBackend_Close();
    return 0;
}
