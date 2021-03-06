#include "boxedwine.h"
#ifdef BOXEDWINE_MULTI_THREADED
#include "../sdlwindow.h"
#include <SDL.h>
#include "sdlcallback.h"
#include "devfb.h"
#if !defined(BOXEDWINE_DISABLE_UI) && !defined(__TEST)
#include "../../ui/mainui.h"
#endif

U32 sdlCustomEvent;
SDL_threadID sdlMainThreadId;
extern U32 platformThreadCount;
extern U32 exceptionCount;
extern U32 dynamicCodeExceptionCount;
static U32 lastTitleUpdate = 0;
extern U32 nativeMemoryPagesAllocated;
bool doMainLoop() {
    SDL_Event e;

    if (!sdlCustomEvent) {
        sdlCustomEvent = SDL_RegisterEvents(1);
    }
    sdlMainThreadId = SDL_ThreadID();

    while (platformThreadCount) {
        bool hasEvent;
        U32 timeout = 5000;
        U32 t = KSystem::getMilliesSinceStart();

        if (KSystem::killTime) {
            if (KSystem::killTime <= t) {
                SDL_Quit();
                exit(9);
                return true;
            }
            if (t - KSystem::killTime < timeout) {
                timeout = t - KSystem::killTime;
            }
        }
        if (isShutdownWindowIsOpen()) {
            timeout = 100;
            updateShutdownWindow();
        }
#if !defined(BOXEDWINE_DISABLE_UI) && !defined(__TEST)
        if (uiIsRunning()) {
            timeout = 33;
        }
#endif
#ifdef BOXEDWINE_RECORDER
        if (Player::instance || Recorder::instance) {
            hasEvent = (SDL_WaitEventTimeout(&e, 10)==1);
            BOXEDWINE_RECORDER_RUN_SLICE();
        } else  {
            hasEvent = (SDL_WaitEventTimeout(&e, timeout) == 1);
        }
#else
        hasEvent = (SDL_WaitEventTimeout(&e, timeout) == 1);
#endif    
#if !defined(BOXEDWINE_DISABLE_UI) && !defined(__TEST)
        if (uiIsRunning()) {
            uiLoop();
        }
#endif                
        //flipFB();
        if (lastTitleUpdate+5000 < t) {
            char tmp[256];
            lastTitleUpdate = t;
            if (KSystem::title.length()) {
                snprintf(tmp, sizeof(tmp), "%s - BoxedWine 20.1.1", KSystem::title.c_str());
            } else {
                snprintf(tmp, sizeof(tmp), "BoxedWine 20.1.1 %dMB", (int)(nativeMemoryPagesAllocated >> 8));
            }
            fbSetCaption(tmp, "BoxedWine");
        }
        if (hasEvent) {
            if (e.type == sdlCustomEvent) {
                SdlCallback* callback = (SdlCallback*)e.user.data1;
                callback->result = (U32)callback->pfn();
                BOXEDWINE_CONDITION_LOCK(callback->cond);
                BOXEDWINE_CONDITION_SIGNAL(callback->cond);
                BOXEDWINE_CONDITION_UNLOCK(callback->cond);
            } else if (!handlSdlEvent(&e)) {
                return true;
            }
        }
    };
    return true;
}

void waitForProcessToFinish(const std::shared_ptr<KProcess>& process, KThread* thread) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(KSystem::processesCond);
    while (!process->isTerminated()) {
        BOXEDWINE_CONDITION_WAIT(KSystem::processesCond);
    }
}

#endif
