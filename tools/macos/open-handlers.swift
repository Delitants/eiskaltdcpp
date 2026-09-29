// Run with Swift on macOS 12+. Default changes are opt-in, never a launch hook.
import AppKit
import CoreServices
import UniformTypeIdentifiers

let schemes = ["dchub", "adc", "nmdcs", "adcs"]
let workspace = NSWorkspace.shared

enum Failure: Error, CustomStringConvertible {
    case message(String)
    var description: String {
        switch self { case .message(let text): return text }
    }
}

func canonical(_ url: URL) -> URL {
    url.resolvingSymlinksInPath().standardizedFileURL
}

func torrentType() throws -> UTType {
    guard let type = UTType(filenameExtension: "torrent") else {
        throw Failure.message("No content type is registered for .torrent files")
    }
    return type
}

func status() -> [String: Any] {
    var result: [String: Any] = [:]
    for scheme in schemes {
        let url = URL(string: "\(scheme)://handler-test.invalid:411")!
        result[scheme] = workspace.urlForApplication(toOpen: url)?.path ?? NSNull()
    }
    if let type = UTType(filenameExtension: "torrent") {
        result["torrentType"] = type.identifier
        result["torrent"] = workspace.urlForApplication(toOpen: type)?.path ?? NSNull()
    } else {
        result["torrentType"] = NSNull()
        result["torrent"] = NSNull()
    }
    return result
}

func json(_ value: [String: Any]) throws -> Data {
    try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys])
}

func check(_ app: URL, torrent: Bool) throws {
    var missing: [String] = []
    for scheme in schemes {
        let url = URL(string: "\(scheme)://handler-test.invalid:411")!
        if !workspace.urlsForApplications(toOpen: url).contains(where: { canonical($0) == app }) {
            missing.append("\(scheme)://")
        }
    }
    let handlesTorrent = (try? workspace.urlsForApplications(toOpen: torrentType())
        .contains(where: { canonical($0) == app })) ?? false
    if handlesTorrent != torrent {
        missing.append(torrent ? ".torrent" : "unexpected .torrent registration in a non-torrent build")
    }
    if !missing.isEmpty {
        throw Failure.message("Handler registration mismatch: " + missing.joined(separator: ", "))
    }
}

func verifyDefaults(_ app: URL) throws {
    for scheme in schemes {
        let url = URL(string: "\(scheme)://handler-test.invalid:411")!
        guard let current = workspace.urlForApplication(toOpen: url), canonical(current) == app else {
            throw Failure.message("\(scheme):// does not resolve to \(app.path)")
        }
    }
    guard let current = workspace.urlForApplication(toOpen: try torrentType()), canonical(current) == app else {
        throw Failure.message(".torrent does not resolve to \(app.path)")
    }
}

func change(_ start: (@escaping (Error?) -> Void) -> Void) throws {
    var complete = false
    var failure: Error?
    start { error in failure = error; complete = true }
    let deadline = Date().addingTimeInterval(120)
    while !complete && Date() < deadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.05))
    }
    guard complete else { throw Failure.message("Timed out waiting for macOS consent; no fallback was attempted") }
    if let failure = failure { throw failure }
}

do {
    let args = Array(CommandLine.arguments.dropFirst())
    guard let command = args.first else { throw Failure.message("Missing command") }
    if command == "status" && args.count == 1 {
        FileHandle.standardOutput.write(try json(status()))
    } else {
        guard args.count >= 2 else { throw Failure.message("An application path is required") }
        let app = canonical(URL(fileURLWithPath: args[1], isDirectory: true))
        guard let bundle = Bundle(url: app), bundle.executableURL != nil else {
            throw Failure.message("Not an application bundle: \(app.path)")
        }
        switch command {
        case "register" where args.count == 2:
            let result = LSRegisterURL(app as CFURL, true)
            guard result == noErr else { throw Failure.message("LSRegisterURL failed: \(result)") }
        case "check" where args.count == 2 || (args.count == 3 && args[2] == "--no-torrent"):
            try check(app, torrent: args.count == 2)
            print("LaunchServices recognizes every expected handler for \(app.path)")
        case "verify-defaults" where args.count == 2:
            try verifyDefaults(app)
            FileHandle.standardOutput.write(try json(status()))
        case "set-defaults" where args.count == 3:
            guard bundle.bundleIdentifier == "com.github.eiskaltdcpp" else {
                throw Failure.message("Default changes are restricted to the Eiskalt application")
            }
            try check(app, torrent: true)
            // Save only the associations this operation changes, without overwriting a backup.
            let backup = URL(fileURLWithPath: args[2])
            try json(status()).write(to: backup, options: .withoutOverwriting)
            try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: backup.path)
            for scheme in schemes {
                try change { workspace.setDefaultApplication(at: app, toOpenURLsWithScheme: scheme,
                                                               completion: $0) }
            }
            let type = try torrentType()
            try change { workspace.setDefaultApplication(at: app, toOpen: type, completion: $0) }
            try verifyDefaults(app)
            FileHandle.standardOutput.write(try json(status()))
        default:
            throw Failure.message("Invalid command or arguments")
        }
    }
    print("")
} catch {
    fputs("\(error)\nUsage: open-handlers status | register APP | check APP [--no-torrent] | verify-defaults APP | set-defaults APP BACKUP.json\n", stderr)
    exit(1)
}
