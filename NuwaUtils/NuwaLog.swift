//
//  NuwaLog.swift
//  NuwaStone
//
//  Created by 孙康 on 2022/7/9.
//

import Foundation

enum NuwaLogLevel : UInt32 {
    case LOG_OFF    = 1
    case LOG_ERROR  = 2
    case LOG_WARN   = 3
    case LOG_INFO   = 4
    case LOG_DEBUG  = 5
}

struct NuwaLog {
    var logLevel: UInt32 {
        get {
            let savedLevel = UserDefaults.standard.integer(forKey: "logLevel")
            if savedLevel > 0 {
                return UInt32(savedLevel)
            }
            return NuwaLogLevel.LOG_INFO.rawValue
        }
        set {
            UserDefaults.standard.set(newValue, forKey: "logLevel")
        }
    }
    
    func log(level: NuwaLogLevel, items: Any...) {
        if level.rawValue >= logLevel {
            print(items)
        }
    }
};

func Log<T>(level: NuwaLogLevel, _ message: T, file: String = #file, lineNumber: Int = #line) {
    if level.rawValue > NuwaLog().logLevel {
        return
    }
    let fileName = (file as NSString).lastPathComponent
    print("\(level) \(fileName): \(lineNumber) [-] \(message)")
}
