//
//  NuwaEvents.swift
//  NuwaStone
//
//  Created by ConradSun on 2022/7/14.
//

import Foundation

enum NuwaEventType : String {
    case TypeNil
    case FileCreate
    case FileDelete
    case FileCloseModify
    case FileRename
    case ProcessCreate
    case ProcessExit
}

protocol NuwaEventProtocol {
    func displayNuwaEvent(_ event: NuwaEventInfo)
}

class NuwaEventInfo {
    var eventType: NuwaEventType
    var eventTime: UInt64
    var pid: UInt32
    var ppid: UInt32
    var procPath: String
    
    var props: Dictionary<String, Any>
    var desc: String {
        let pretty = """
        Event Type: \(eventType)
        Timestamp: \(eventTime)
        Pid: \(pid) (Parent) -> \(ppid)
        ProcPath: \(procPath)
        Props:
        \(props as AnyObject)
        """
        return pretty
    }
    
    init() {
        eventType = .TypeNil
        eventTime = 0
        pid = 0
        ppid = 0
        procPath = ""
        props = Dictionary<String, Any>()
    }
    
    func fillProcPath() {
        XPCConnection.sharedInstance.getProcPath(pid: Int32(pid), eventHandler: { path, error in
            if error == EPERM {
                let proxy = XPCConnection.sharedInstance.connection?.remoteObjectProxy as? DaemonXPCProtocol
                proxy?.getProcPath(pid: Int32(self.pid), eventHandler: { path, error in
                    self.procPath = path
                })
                return
            }
            self.procPath = path
        })
    }
    
    func fillProcCurrentDir() {
        XPCConnection.sharedInstance.getProcCurrentDir(pid: Int32(pid), eventHandler: { cwd, error in
            if error == EPERM {
                let proxy = XPCConnection.sharedInstance.connection?.remoteObjectProxy as? DaemonXPCProtocol
                proxy?.getProcCurrentDir(pid: Int32(self.pid), eventHandler: { cwd, error in
                    self.props.updateValue(cwd, forKey: ProcessCWD)
                })
                return
            }
            self.props.updateValue(cwd, forKey: ProcessCWD)
        })
    }
    
    func fillProcArgs() {
        XPCConnection.sharedInstance.getProcArgs(pid: Int32(pid)) { args, error in
            if error == EPERM {
                let proxy = XPCConnection.sharedInstance.connection?.remoteObjectProxy as? DaemonXPCProtocol
                proxy?.getProcArgs(pid: Int32(self.pid), eventHandler: { args, error in
                    self.props.updateValue(args, forKey: ProcessArgs)
                })
                return
            }
            self.props.updateValue(args, forKey: ProcessArgs)
        }
    }
}
