import AppKit
import CoreServices

// No default-setting APIs: every launch names the disposable app explicitly.
let fm = FileManager.default
let ws = NSWorkspace.shared
var failures: [String] = []
var checks: [[String: Any]] = []
func check(_ ok: Bool, _ name: String) {
    checks.append(["check": name, "passed": ok])
    print("\(ok ? "PASS" : "FAIL") \(name)")
    if !ok { failures.append(name) }
}
struct Failure: Error { let message: String }
func pump(until predicate: () -> Bool, seconds: Double) -> Bool {
    let deadline = Date().addingTimeInterval(seconds)
    while !predicate() && Date() < deadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.03))
    }
    return predicate()
}
func rows(_ file: URL) -> [[String: Any]] {
    guard let text = try? String(contentsOf: file, encoding: .utf8) else { return [] }
    return text.split(separator: "\n").compactMap {
        (try? JSONSerialization.jsonObject(with: Data($0.utf8))) as? [String: Any]
    }
}
func candidates(_ url: URL) -> [URL] { ws.urlsForApplications(toOpen: url) }
func contains(_ urls: [URL], _ app: URL) -> Bool {
    urls.contains { $0.resolvingSymlinksInPath().standardizedFileURL.path == app.resolvingSymlinksInPath().standardizedFileURL.path }
}
func runCommand(_ path: String, _ args: [String]) throws -> Int32 {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: path)
    process.arguments = args
    try process.run()
    process.waitUntilExit()
    return process.terminationStatus
}

guard CommandLine.arguments.count == 4 || (CommandLine.arguments.count == 5 && CommandLine.arguments[4] == "--no-torrent") else {
    fputs("Usage: smoke-driver RECEIVER SOURCE_PLIST QA_DIRECTORY [--no-torrent]\n", stderr)
    exit(64)
}
let torrentEnabled = CommandLine.arguments.count == 4
let binary = URL(fileURLWithPath: CommandLine.arguments[1])
let sourcePlist = URL(fileURLWithPath: CommandLine.arguments[2])
let qa = URL(fileURLWithPath: CommandLine.arguments[3], isDirectory: true).resolvingSymlinksInPath().standardizedFileURL
let id = "org.eiskaltdcpp.test.nativeopen." + UUID().uuidString.lowercased()
let app = qa.appendingPathComponent("NativeOpen-\(UUID().uuidString).app", isDirectory: true)
var registered = false
var launched: [NSRunningApplication] = []
var probes: [URL] = []
var schemes: [String] = []
var types: [String] = []
var before: [String: String] = [:]
func defaults() -> [String: String] {
    var result: [String: String] = [:]
    for scheme in schemes {
        result["scheme:" + scheme] = LSCopyDefaultHandlerForURLScheme(scheme as CFString)?.takeRetainedValue() as String? ?? "<none>"
    }
    for type in types {
        result["type:" + type] = LSCopyDefaultRoleHandlerForContentType(type as CFString, .all)?.takeRetainedValue() as String? ?? "<none>"
    }
    for url in probes {
        result[url.absoluteString] = ws.urlForApplication(toOpen: url)?.path ?? "<none>"
    }
    return result
}
func open(_ url: URL, report: URL) throws -> NSRunningApplication {
    let configuration = NSWorkspace.OpenConfiguration()
    configuration.activates = false
    configuration.addsToRecentItems = false
    configuration.arguments = [report.path]
    configuration.environment = ["HOME": qa.appendingPathComponent("home").path,
                                 "XDG_CONFIG_HOME": qa.appendingPathComponent("home/config").path,
                                 "QT_QPA_PLATFORM": "cocoa"]
    var finished = false
    var result: NSRunningApplication?
    var error: Error?
    ws.open([url], withApplicationAt: app, configuration: configuration) { running, failure in
        result = running
        error = failure
        finished = true
    }
    guard pump(until: { finished }, seconds: 10), let running = result else {
        throw Failure(message: "NSWorkspace explicit open failed: \(String(describing: error))")
    }
    launched.append(running)
    return running
}

do {
    let original = try Data(contentsOf: sourcePlist)
    guard var plist = try PropertyListSerialization.propertyList(from: original, format: nil) as? [String: Any],
          plist["CFBundlePackageType"] as? String == "APPL",
          !String(decoding: original, as: UTF8.self).contains("${") else {
        throw Failure(message: "Provide a built app Info.plist, not an unexpanded template")
    }
    let urlTypes = plist["CFBundleURLTypes"] as? [[String: Any]] ?? []
    let docs = plist["CFBundleDocumentTypes"] as? [[String: Any]] ?? []
    schemes = Array(Set(["dchub", "adc", "nmdcs", "adcs"] + urlTypes.flatMap { $0["CFBundleURLSchemes"] as? [String] ?? [] })).sorted()
    types = Array(Set(["org.bittorrent.torrent"] + docs.flatMap { $0["LSItemContentTypes"] as? [String] ?? [] })).sorted()
    let torrent = qa.appendingPathComponent("local fixture # 100%.torrent")
    try Data("d4:infod6:lengthi0e4:name5:empty12:piece lengthi16384e6:pieces0:ee".utf8).write(to: torrent)
    probes = schemes.map { URL(string: "\($0)://native-open.invalid:411/smoke%20path")! } + [torrent]
    for ext in Set(docs.flatMap { $0["CFBundleTypeExtensions"] as? [String] ?? [] }) where ext != "*" && ext != "torrent" {
        guard !ext.contains("/") else { throw Failure(message: "Unsafe document extension") }
        let file = qa.appendingPathComponent("declared-fixture." + ext)
        try Data().write(to: file)
        probes.append(file)
    }
    before = defaults()
    try fm.createDirectory(at: qa.appendingPathComponent("home/config"), withIntermediateDirectories: true)
    let contents = app.appendingPathComponent("Contents")
    let macOS = contents.appendingPathComponent("MacOS")
    try fm.createDirectory(at: macOS, withIntermediateDirectories: true)
    try fm.copyItem(at: binary, to: macOS.appendingPathComponent("native_open_receiver"))
    plist["CFBundleIdentifier"] = id
    plist["CFBundleExecutable"] = "native_open_receiver"
    plist["CFBundleName"] = "Native Open Smoke"
    if plist["CFBundleDisplayName"] != nil { plist["CFBundleDisplayName"] = "Native Open Smoke" }
    let fixtureData = try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
    try fixtureData.write(to: contents.appendingPathComponent("Info.plist"))
    try fixtureData.write(to: qa.appendingPathComponent("fixture.Info.plist"))
    check(try runCommand("/usr/bin/codesign", ["--force", "--sign", "-", app.path]) == 0, "ad-hoc sign fixture")
    registered = true // Cleanup also covers partial registration failure.
    check(LSRegisterURL(app as CFURL, true) == noErr, "register fixture only")
    for probe in probes {
        let expectedCandidate = torrentEnabled || probe != torrent
        let matched = pump(until: { contains(candidates(probe), app) == expectedCandidate }, seconds: 1)
        check(matched, "LaunchServices candidate expected=\(expectedCandidate): \(probe.absoluteString)")
        checks.append(["probe": probe.absoluteString, "candidates": candidates(probe).map { $0.path }])
    }
    for type in types {
        let handlers = LSCopyAllRoleHandlersForContentType(type as CFString, .all)?.takeRetainedValue() as? [String] ?? []
        check(handlers.contains(id) == (torrentEnabled || type != "org.bittorrent.torrent"), "LaunchServices content type: \(type)")
    }
    let nativeTests = ["dchub", "adc", "nmdcs", "adcs"].map {
        ( $0, URL(string: "\($0)://native-open.invalid:411/smoke%20path")! )
    } + (torrentEnabled ? [("torrent", torrent)] : [])
    for (name, url) in nativeTests {
        let report = qa.appendingPathComponent("\(name).jsonl")
        let expected = url.isFileURL ? url.path : url.absoluteString
        let cold = try open(url, report: report)
        check(pump(until: { rows(report).filter { $0["event"] as? String == "delivered" }.count >= 1 }, seconds: 5), "\(name) cold delivery")
        let warm = try open(url, report: report)
        check(cold.processIdentifier == warm.processIdentifier, "\(name) warm uses cold PID")
        check(pump(until: { cold.isTerminated }, seconds: 17), "\(name) self-exited")
        let events = rows(report)
        let received = events.filter { $0["event"] as? String == "received" }
        let delivered = events.filter { $0["event"] as? String == "delivered" }
        check(received.count == 2 && delivered.count == 2, "\(name) received/delivered exactly once per open")
        check((received + delivered).allSatisfy { $0["source"] as? String == expected && $0["pid"] as? Int32 == cold.processIdentifier && $0["accepted"] as? Bool == true }, "\(name) exact normalized sources and PID")
        check(received.map { $0["ready"] as? Bool } == [false, true] && delivered.allSatisfy { $0["ready"] as? Bool == true }, "\(name) cold held unready, warm ready")
        check(events.last?["event"] as? String == "finished" && !events.contains { $0["event"] as? String == "timeout" }, "\(name) clean finish without timeout")
    }
} catch {
    check(false, "driver error: \(error)")
}

// Always allow the fixture's own watchdog to exit; never terminate by name/PID.
check(pump(until: { launched.allSatisfy { $0.isTerminated } }, seconds: 18), "all fixture processes self-exited")
if registered {
    do {
        let status = try runCommand("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister", ["-u", app.path])
        check(status == 0, "unregister fixture")
    } catch { check(false, "unregister error: \(error)") }
}
if fm.fileExists(atPath: app.path) {
    do { try fm.removeItem(at: app) } catch { check(false, "remove fixture error: \(error)") }
}
for probe in probes {
    check(!contains(candidates(probe), app), "cleanup candidate absent: \(probe.absoluteString)")
}
for type in types {
    let handlers = LSCopyAllRoleHandlersForContentType(type as CFString, .all)?.takeRetainedValue() as? [String] ?? []
    check(!handlers.contains(id), "cleanup content type absent: \(type)")
}
for scheme in schemes {
    let handlers = LSCopyAllHandlersForURLScheme(scheme as CFString)?.takeRetainedValue() as? [String] ?? []
    check(!handlers.contains(id), "cleanup scheme absent: \(scheme)")
}
let after = defaults()
check(before == after, "global defaults unchanged (no setter called)")
let summary: [String: Any] = ["passed": failures.isEmpty, "failures": failures, "checks": checks,
                              "torrentEnabled": torrentEnabled,
                              "bundleID": id, "fixturePath": app.path,
                              "defaultsBefore": before, "defaultsAfter": after,
                              "coverage": "Native Cocoa QFileOpenEvent -> production PendingOpenEvents only; no production app UI, profile, networking, torrent parsing or installed-default dispatch"]
do {
    try JSONSerialization.data(withJSONObject: summary, options: [.prettyPrinted, .sortedKeys]).write(to: qa.appendingPathComponent("summary.json"))
} catch { fputs("Could not write summary: \(error)\n", stderr); exit(1) }
exit(failures.isEmpty ? 0 : 1)
