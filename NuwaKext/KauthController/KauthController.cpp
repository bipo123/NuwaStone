//
//  KauthController.cpp
//  NuwaKext
//
//  Created by ConradSun on 2022/7/10.
//

#include "KauthController.hpp"
#include "EventDispatcher.hpp"
#include "KextLogger.hpp"
#include <sys/proc.h>

OSDefineMetaClassAndStructors(KauthController, OSObject);
SInt32 KauthController::m_activeEventCount = 0;

bool KauthController::init() {
    if (!OSObject::init()) {
        return false;
    }
    
    m_cacheManager = CacheManager::getInstance();
    if (m_cacheManager == nullptr) {
        return false;
    }
    m_eventDispatcher = EventDispatcher::getInstance();
    if (m_eventDispatcher == nullptr) {
        return false;
    }
    return true;
}

void KauthController::free() {
    m_eventDispatcher = nullptr;
    OSObject::free();
}

bool KauthController::startListeners() {
    m_vnodeListener = kauth_listen_scope(KAUTH_SCOPE_VNODE, vnode_scope_callback, reinterpret_cast<void *>(this));
    if (m_vnodeListener == nullptr) {
        return false;
    }
    m_fileopListener = kauth_listen_scope(KAUTH_SCOPE_FILEOP, fileop_scope_callback, reinterpret_cast<void *>(this));
    if (m_fileopListener == nullptr) {
        kauth_unlisten_scope(m_vnodeListener);
        return false;
    }
    return true;
}

void KauthController::stopListeners() {
    if (m_vnodeListener != nullptr) {
        kauth_unlisten_scope(m_vnodeListener);
    }
    if (m_fileopListener != nullptr) {
        kauth_unlisten_scope(m_fileopListener);
    }
}

bool KauthController::postToAuthQueue(NuwaKextEvent *eventInfo) {
    if (eventInfo == nullptr) {
        return false;
    }
    return m_eventDispatcher->postToAuthQueue(eventInfo);
}

bool KauthController::postToNotifyQueue(NuwaKextEvent *eventInfo) {
    if (eventInfo == nullptr) {
        return false;
    }
    return m_eventDispatcher->postToNtifyQueue(eventInfo);
}

void KauthController::increaseEventCount() {
    OSIncrementAtomic(&m_activeEventCount);
}

void KauthController::decreaseEventCount() {
    OSDecrementAtomic(&m_activeEventCount);
}

int KauthController::getDecisionFromClient(UInt64 vnodeID) {
    kern_return_t errCode = KERN_SUCCESS;
    int decision = KAUTH_RESULT_DEFER;
    static struct timespec time = {
        .tv_sec = kMaxAuthWaitTime / 1000,
        .tv_nsec = (kMaxAuthWaitTime - time.tv_sec * 1000) * 1000000
    };
    
    errCode = msleep((void *)vnodeID, nullptr, 0, "Wait for reply", &time);
    if (errCode == KERN_SUCCESS) {
        decision = m_cacheManager->getFromAuthResultCache(vnodeID);
    }
    else if (errCode == EWOULDBLOCK) {
        decision = KAUTH_RESULT_DEFER;
        Logger(LOG_ERROR, "Reply event [%llu] timeout.", vnodeID)
    }
    decision = decision == 0 ? KAUTH_RESULT_DEFER : decision;
    
    return decision;
}

int KauthController::vnodeCallback(const vfs_context_t ctx, const vnode_t vp, int *errno) {
    int response = KAUTH_RESULT_DEFER;
    kern_return_t errCode = KERN_SUCCESS;
    NuwaKextEvent *event = (NuwaKextEvent *)IOMallocAligned(sizeof(NuwaKextEvent), 2);
    if (event == nullptr) {
        return response;
    }
    
    event->eventType = kActionAuthProcessCreate;
    errCode = fillEventInfo(event, ctx, vp);
    if (errCode == KERN_SUCCESS) {
        postToAuthQueue(event);
        response = getDecisionFromClient(event->vnodeID);
        if (response == KAUTH_RESULT_DEFER || response == KAUTH_RESULT_ALLOW) {
            UInt64 value = ((UInt64)event->mainProcess.pid << 32) | event->mainProcess.ppid;
            m_cacheManager->setForAuthExecCache(event->vnodeID, value);
        }
    }
    
    IOFreeAligned(event, sizeof(NuwaKextEvent));
    return response;
}

void KauthController::fileOpCallback(kauth_action_t action, const vnode_t vp, const char *srcPath, const char *newPath) {
    kern_return_t errCode = KERN_SUCCESS;
    NuwaKextEvent *event = (NuwaKextEvent *)IOMallocAligned(sizeof(NuwaKextEvent), 2);
    if (event == nullptr) {
        return;
    }
    
    vfs_context_t ctx = vfs_context_create(NULL);
    switch (action) {
        case KAUTH_FILEOP_CLOSE:
            event->eventType = kActionNotifyFileCloseModify;
            strlcpy(event->fileCloseModify.path, srcPath, kMaxPathLength);
            break;
        case KAUTH_FILEOP_DELETE:
            event->eventType = kActionNotifyFileDelete;
            strlcpy(event->fileDelete.path, srcPath, kMaxPathLength);
            break;
        case KAUTH_FILEOP_EXEC:
            event->eventType = kActionNotifyProcessCreate;
            strlcpy(event->processCreate.path, srcPath, kMaxPathLength);
            break;
        case KAUTH_FILEOP_RENAME:
            event->eventType = kActionNotifyFileRename;
            strlcpy(event->fileRename.srcFile.path, srcPath, kMaxPathLength);
            strlcpy(event->fileRename.newPath, newPath, kMaxPathLength);
            break;
            
        default:
            break;
    }
    
    errCode = fillEventInfo(event, ctx, vp);
    if (action == KAUTH_FILEOP_EXEC) {
        UInt64 result = m_cacheManager->getFromAuthExecCache(event->vnodeID);
        if (result != 0 && (result >> 32) == event->mainProcess.pid) {
            event->mainProcess.ppid = (result << 32) >> 32;
        }
    }
    if (errCode == KERN_SUCCESS) {
        postToNotifyQueue(event);
    }
    
    if (ctx != NULL) {
        vfs_context_rele(ctx);
    }
    IOFreeAligned(event, sizeof(NuwaKextEvent));
}

kern_return_t KauthController::fillBasicInfo(NuwaKextEvent *eventInfo, const vfs_context_t ctx, const vnode_t vp) {
    kern_return_t errCode = KERN_SUCCESS;
    struct timeval time;
    struct vnode_attr vap;
    
    microtime(&time);
    eventInfo->eventTime = time.tv_sec;
    if (ctx == nullptr || vp == nullptr) {
        return errCode;
    }
    
    VATTR_INIT(&vap);
    VATTR_WANTED(&vap, va_fsid);
    VATTR_WANTED(&vap, va_fileid);
    errCode = vnode_getattr(vp, &vap, ctx);
    if (errCode == KERN_SUCCESS) {
        eventInfo->vnodeID = ((UInt64)vap.va_fsid << 32) | vap.va_fileid;
    }
    
    return errCode;
}

kern_return_t KauthController::fillProcInfo(NuwaKextProc *ProctInfo, const vfs_context_t ctx) {
    if (ctx == nullptr) {
        return EINVAL;
    }
    
    proc_t proc = vfs_context_proc(ctx);
    kauth_cred_t cred = vfs_context_ucred(ctx);
    
    if (proc != NULL) {
        ProctInfo->pid = proc_pid(proc);
        ProctInfo->ppid = proc_ppid(proc);
    }
    
    if (cred != NULL) {
        ProctInfo->euid = kauth_cred_getuid(cred);
        ProctInfo->ruid = kauth_cred_getruid(cred);
        ProctInfo->egid = kauth_cred_getgid(cred);
        ProctInfo->rgid = kauth_cred_getrgid(cred);
    }
    
    return KERN_SUCCESS;
}

kern_return_t KauthController::fillFileInfo(NuwaKextFile *FileInfo, const vfs_context_t ctx, const vnode_t vp) {
    kern_return_t errCode = KERN_SUCCESS;
    int length = kMaxPathLength;
    struct vnode_attr vap;
    
    if (ctx == nullptr || vp == nullptr) {
        return errCode;
    }
    
    VATTR_INIT(&vap);
    VATTR_WANTED(&vap, va_uid);
    VATTR_WANTED(&vap, va_gid);
    VATTR_WANTED(&vap, va_mode);
    VATTR_WANTED(&vap, va_access_time);
    VATTR_WANTED(&vap, va_modify_time);
    VATTR_WANTED(&vap, va_change_time);
    errCode = vnode_getattr(vp, &vap, ctx);
    
    if (errCode == KERN_SUCCESS) {
        FileInfo->uid = vap.va_uid;
        FileInfo->gid = vap.va_gid;
        FileInfo->mode = vap.va_mode;
        FileInfo->atime = vap.va_access_time.tv_sec;
        FileInfo->mtime = vap.va_modify_time.tv_sec;
        FileInfo->ctime = vap.va_change_time.tv_sec;
        errCode = vn_getpath(vp, FileInfo->path, &length);
    }
    
    return errCode;
}

kern_return_t KauthController::fillEventInfo(NuwaKextEvent *event, const vfs_context_t ctx, const vnode_t vp) {
    kern_return_t errCode = KERN_SUCCESS;
    NuwaKextFile *fileInfo = nullptr;
    
    switch (event->eventType) {
        case kActionAuthProcessCreate:
        case kActionNotifyProcessCreate:
        case kActionNotifyFileCloseModify:
        case kActionNotifyFileDelete:
            fileInfo = &event->fileDelete;
            break;
            
        case kActionNotifyFileRename:
            fileInfo = &event->fileRename.srcFile;
            break;
            
        default:
            break;
    }
    
    errCode = fillBasicInfo(event, ctx, vp);
    if (errCode != KERN_SUCCESS) {
        Logger(LOG_WARN, "Failed to fill basic info [%d].", errCode)
        return errCode;
    }
    errCode = fillProcInfo(&event->mainProcess, ctx);
    if (errCode != KERN_SUCCESS) {
        Logger(LOG_WARN, "Failed to fill proc info [%d].", errCode)
        return errCode;
    }
    errCode = fillFileInfo(fileInfo, ctx, vp);
    if (errCode != KERN_SUCCESS) {
        Logger(LOG_WARN, "Failed to fill file info [%d].", errCode)
        return errCode;
    }
    return errCode;
}

extern "C"
int vnode_scope_callback(kauth_cred_t credential, void *idata, kauth_action_t action,
                         uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    int response = KAUTH_RESULT_DEFER;
    KauthController *selfPtr = OSDynamicCast(KauthController, reinterpret_cast<OSObject *>(idata));
    if (selfPtr == nullptr) {
        return response;
    }
    
    vfs_context_t context = reinterpret_cast<vfs_context_t>(arg0);
    vnode_t vp = reinterpret_cast<vnode_t>(arg1);
    int *errno = reinterpret_cast<int *>(arg3);
    if (vnode_vtype(vp) != VREG) {
        return response;
    }
    if (action == KAUTH_VNODE_EXECUTE) {
        selfPtr->increaseEventCount();
        response = selfPtr->vnodeCallback(context, vp, errno);
        selfPtr->decreaseEventCount();
    }
    
    return response;
}

extern "C"
int fileop_scope_callback(kauth_cred_t credential, void *idata, kauth_action_t action,
                          uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    KauthController *selfPtr = OSDynamicCast(KauthController, reinterpret_cast<OSObject *>(idata));
    if (selfPtr == nullptr) {
        return KAUTH_RESULT_DEFER;
    }
    
    vnode_t vp = nullptr;
    char *srcPath = nullptr;
    char *newPath = nullptr;
    int flag = 0;
    
    switch (action) {
        case KAUTH_FILEOP_CLOSE:
            flag = (int)arg2;
            if (!(flag & KAUTH_FILEOP_CLOSE_MODIFIED)) {
                return KAUTH_RESULT_DEFER;
            }
            
        case KAUTH_FILEOP_DELETE:
        case KAUTH_FILEOP_EXEC:
            vp = reinterpret_cast<vnode_t>(arg0);
            srcPath = reinterpret_cast<char *>(arg1);
            if (vnode_vtype(vp) != VREG) {
                return KAUTH_RESULT_DEFER;
            }
            if (action == KAUTH_FILEOP_DELETE) {
                vp = nullptr;
            }
            break;
            
        case KAUTH_FILEOP_RENAME:
            srcPath = reinterpret_cast<char *>(arg0);
            newPath = reinterpret_cast<char *>(arg1);
            break;
            
        default:
            return KAUTH_RESULT_DEFER;
    }
    
    selfPtr->increaseEventCount();
    selfPtr->fileOpCallback(action, vp, srcPath, newPath);
    selfPtr->decreaseEventCount();
    
    return KAUTH_RESULT_DEFER;
}
