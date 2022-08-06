//
//  SocketHandler.hpp
//  NuwaKext
//
//  Created by ConradSun on 2022/8/1.
//

#ifndef SocketHandler_hpp
#define SocketHandler_hpp

#include <sys/kpi_socketfilter.h>
#include "CacheManager.hpp"
#include "EventDispatcher.hpp"


class SocketHandler : public OSObject {
    OSDeclareDefaultStructors(SocketHandler);

public:
    // Used for initialization after instantiation.
    bool init() override;

    // Called automatically when retain count drops to 0.
    void free() override;
    
    void notifySocketCallback(socket_t socket, sflt_event_t event);
    void bindSocketCallback(socket_t socket, const sockaddr *to);
    
private:
    errno_t fillBasicInfo(NuwaKextEvent *netEvent, NuwaKextAction action);
    errno_t fillConnectionInfo(NuwaKextEvent *netEvent);
    errno_t fillNetEventInfo(NuwaKextEvent *netEvent, NuwaKextAction action);
    
    socket_t m_socket;
    sockaddr m_localAddr;
    sockaddr m_remoteAddr;
    
    CacheManager *m_cacheManager;
    EventDispatcher *m_eventDispatcher;
};

#endif /* SocketHandler_hpp */
