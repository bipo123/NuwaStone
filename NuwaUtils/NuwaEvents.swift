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

struct NuwaEventInfo {
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
    
    private func getSysctlArgmax() -> Int {
        var argmax: Int = 0
        var mib: [Int32] = [CTL_KERN, KERN_ARGMAX]
        var size = Swift.Int(MemoryLayout.size(ofValue: argmax))
        
        guard sysctl(&mib, 2, &argmax, &size, nil, 0) == 0 else {
            return 0
        }
        return argmax
    }
    
    private func getProcArgs(pid: Int32, args: UnsafeMutablePointer<CChar>, size: UnsafeMutablePointer<Int>) -> Bool {
        var mib: [Int32] = [CTL_KERN, KERN_PROCARGS2, pid]
        guard sysctl(&mib, 3, args, size, nil, 0) >= 0 else {
            return false
        }
        return true
    }
    
    mutating func fillProcPath() {
        var buffer = [CChar](repeating: 0, count: Swift.Int(PROC_PIDPATHINFO_SIZE))
        guard proc_pidpath(Int32(pid), &buffer, UInt32(buffer.count)) > 0 else {
            if errno != ESRCH {
                Logger(.Warning, "Failed to get proc [\(pid)] path for [\(String(describing: strerror(errno)))]")
            }
            return
        }
        procPath = String(cString: buffer)
    }
    
    mutating func fillVnodeInfo() {
        var info = proc_vnodepathinfo()
        guard proc_pidinfo(Int32(pid), PROC_PIDVNODEPATHINFO, 0, &info, Int32(MemoryLayout.size(ofValue: info))) > 0 else {
            if errno != ESRCH {
                Logger(.Warning, "Failed to get proc [\(pid)] vnode info for [\(String(describing: strerror(errno)))]")
            }
            return
        }
        let cwd = String(cString: &info.pvi_cdir.vip_path.0)
        props.updateValue(cwd, forKey: "current working dir")
    }
    
    mutating func fillBsdInfo() {
        var info = proc_bsdinfo()
        guard proc_pidinfo(Int32(pid), PROC_PIDTBSDINFO, 0, &info, Int32(MemoryLayout.size(ofValue: info))) > 0 else {
            if errno != ESRCH {
                Logger(.Warning, "Failed to get proc [\(pid)] bsd info for [\(String(describing: strerror(errno)))]")
            }
            return
        }
        
        ppid = info.pbi_ppid
    }
    
    mutating func fillProcArgs() {
        var argc: Int32 = 0
        var argmax = getSysctlArgmax()
        let size = MemoryLayout.size(ofValue: argc)
        var begin = size
        
        if argmax == 0 {
            return
        }
        var args = [CChar](repeating: CChar.zero, count: Int(argmax))
        guard getProcArgs(pid: Int32(pid), args: &args, size: &argmax) else {
            return
        }
        NSData(bytes: args, length: size).getBytes(&argc, length: size)
        
        repeat {
            if args[begin] == 0x0 {
                begin += 1
                break
            }
            begin += 1
        } while begin < argmax
        if begin == argmax {
            return
        }
        
        var last = begin
        var argv = Array<String>()
        while begin < argmax && argc > 0 {
            if args[begin] == 0x0 {
                var temp = Array(args[last...begin])
                let arg = String(cString: &temp)
                if arg.count > 0 {
                    argv.append(arg)
                }
                
                last = begin + 1
                argc -= 1
            }
            begin += 1
        }
        
        if argv.count > 1 {
            argv.remove(at: 0)
            props.updateValue(argv, forKey: "arguments")
        }
    }
}
