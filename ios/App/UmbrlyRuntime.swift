import Foundation
import SwiftUI
import UIKit
import AudioToolbox
import UmbrlyCore

// MARK: - Console model

/// One run of text sharing a colour, as produced by COLOR().
struct ConsoleSegment: Identifiable, Hashable {
    let id = UUID()
    var text: String
    var colorIndex: Int  // Windows console index 0...15, or -1 for the default

    static func == (a: ConsoleSegment, b: ConsoleSegment) -> Bool { a.id == b.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

/// What the console is waiting for, which is what the input bar renders.
enum ConsoleDemand: Equatable {
    case none
    case line       // INPUT:
    case key        // GETKEY()
}

@MainActor
@Observable
final class ConsoleModel {
    private(set) var segments: [ConsoleSegment] = []
    private(set) var isRunning = false
    var title = "Umbrly"
    var demand: ConsoleDemand = .none

    /// Colour currently selected by COLOR(); -1 is the theme default.
    fileprivate var currentColor = -1

    func reset() {
        segments = []
        currentColor = -1
        title = "Umbrly"
        demand = .none
    }

    func setRunning(_ running: Bool) {
        isRunning = running
        if !running { demand = .none }
    }

    /// Appends text, merging into the last segment when the colour is unchanged
    /// so the array does not grow one element per character.
    func append(_ text: String) {
        guard !text.isEmpty else { return }
        if var last = segments.last, last.colorIndex == currentColor {
            last.text += text
            segments[segments.count - 1] = last
        } else {
            segments.append(ConsoleSegment(text: text, colorIndex: currentColor))
        }
    }

    func clear() {
        segments = []
    }

    func setColor(_ fg: Int) {
        currentColor = fg
    }
}

// MARK: - Engine

/// Drives one script run and bridges the C callbacks to the UI.
///
/// `umbrly_run` blocks, so it gets its own thread. Callbacks that need an answer
/// park that thread on a semaphore while the main actor collects one.
final class UmbrlyEngine: @unchecked Sendable {
    fileprivate let console: ConsoleModel
    private let sandbox: URL

    /// Guards the fields the interpreter thread and the main actor both touch.
    fileprivate let lock = NSLock()
    private let answerReady = DispatchSemaphore(value: 0)
    private var pendingLine: String?
    private var pendingKey: Int32 = 0
    private var confirmResult = false
    private var cancelled = false
    fileprivate var lastTouch = CGPoint.zero

    init(console: ConsoleModel, sandbox: URL) {
        self.console = console
        self.sandbox = sandbox
    }

    // MARK: Answers supplied by the UI

    /// Hands a typed line to a script blocked in INPUT:.
    func submit(line: String) {
        lock.lock()
        pendingLine = line
        lock.unlock()
        answerReady.signal()
    }

    /// Hands a key code to a script blocked in GETKEY().
    func submit(key: Int32) {
        lock.lock()
        pendingKey = key
        lock.unlock()
        answerReady.signal()
    }

    func submit(confirm: Bool) {
        lock.lock()
        confirmResult = confirm
        lock.unlock()
        answerReady.signal()
    }

    func record(touch: CGPoint) {
        lock.lock()
        lastTouch = touch
        lock.unlock()
    }

    /// Stops the script. Also releases a thread parked on input, otherwise a
    /// script sitting in INPUT: would never notice the stop.
    func cancel() {
        lock.lock()
        cancelled = true
        pendingLine = nil
        lock.unlock()
        umbrly_request_stop()
        answerReady.signal()
    }

    // MARK: Running

    func run(source: String) {
        umbrly_clear_stop()
        lock.lock()
        cancelled = false
        lock.unlock()

        let console = self.console
        Task { @MainActor in
            console.reset()
            console.setRunning(true)
        }

        Thread.detachNewThread { [weak self] in
            guard let self else { return }

            var host = self.makeHost()
            let status = source.withCString { src in
                self.sandbox.path.withCString { root in
                    umbrly_run(src, root, &host)
                }
            }

            Task { @MainActor in
                switch status {
                case UMBRLY_OK:
                    console.append("\n[скрипт завершён]\n")
                case UMBRLY_STOPPED:
                    console.append("\n[остановлено]\n")
                default:
                    break  // the interpreter already wrote the error text
                }
                console.setRunning(false)
            }
        }
    }

    // MARK: C host table

    private func makeHost() -> UmbrlyHost {
        var host = UmbrlyHost()
        host.ctx = Unmanaged.passUnretained(self).toOpaque()

        host.write = { ctx, text in
            guard let engine = UmbrlyEngine.from(ctx), let text else { return }
            let s = String(cString: text)
            let console = engine.console
            // DispatchQueue rather than Task: output must stay in the order the
            // script produced it, and unstructured Tasks give no such promise.
            DispatchQueue.main.async { MainActor.assumeIsolated { console.append(s) } }
        }

        host.cls = { ctx in
            guard let engine = UmbrlyEngine.from(ctx) else { return }
            let console = engine.console
            DispatchQueue.main.async { MainActor.assumeIsolated { console.clear() } }
        }

        host.color = { ctx, fg, _ in
            guard let engine = UmbrlyEngine.from(ctx) else { return }
            let console = engine.console
            DispatchQueue.main.async { MainActor.assumeIsolated { console.setColor(Int(fg)) } }
        }

        // GOTOXY has no meaning in a scrolling transcript; see the note in
        // umbrly_platform_report().
        host.gotoxy = { _, _, _ in }

        host.title = { ctx, text in
            guard let engine = UmbrlyEngine.from(ctx), let text else { return }
            let s = String(cString: text)
            let console = engine.console
            DispatchQueue.main.async { MainActor.assumeIsolated { console.title = s } }
        }

        host.msgbox = { ctx, title, text in
            guard let engine = UmbrlyEngine.from(ctx) else { return }
            let t = title.map { String(cString: $0) } ?? "Umbrly"
            let m = text.map { String(cString: $0) } ?? ""
            engine.presentAlert(title: t, message: m, isConfirm: false)
        }

        host.confirm = { ctx, title, text in
            guard let engine = UmbrlyEngine.from(ctx) else { return 0 }
            let t = title.map { String(cString: $0) } ?? "Umbrly"
            let m = text.map { String(cString: $0) } ?? ""
            return engine.presentAlert(title: t, message: m, isConfirm: true) ? 1 : 0
        }

        host.readline = { ctx, buf, cap in
            guard let engine = UmbrlyEngine.from(ctx), let buf, cap > 1 else { return 0 }
            guard let line = engine.awaitLine() else { return 0 }
            engine.copy(line, into: buf, cap: cap)
            return 1
        }

        host.getkey = { ctx, blocking in
            guard let engine = UmbrlyEngine.from(ctx) else { return 0 }
            return engine.awaitKey(blocking: blocking != 0)
        }

        host.beep = { _, _, _ in
            // iOS exposes no tone generator; 1057 is the standard short alert.
            AudioServicesPlaySystemSound(1057)
        }

        host.play = { _, _ in
            AudioServicesPlaySystemSound(1057)
        }

        host.clipboard_set = { _, text in
            guard let text else { return }
            let s = String(cString: text)
            DispatchQueue.main.async { UIPasteboard.general.string = s }
        }

        host.clipboard_get = { ctx, buf, cap in
            guard let engine = UmbrlyEngine.from(ctx), let buf, cap > 1 else { return 0 }
            var value = ""
            // UIPasteboard is main-thread only, and this is not it — hop and wait.
            DispatchQueue.main.sync { value = UIPasteboard.general.string ?? "" }
            engine.copy(value, into: buf, cap: cap)
            return 1
        }

        host.metrics = { _, w, h in
            var size = CGSize.zero
            DispatchQueue.main.sync {
                // iOS 26 wants the screen reached through the active scene
                // rather than the deprecated global UIScreen.main.
                let scene = UIApplication.shared.connectedScenes
                    .compactMap { $0 as? UIWindowScene }
                    .first { $0.activationState == .foregroundActive }
                size = scene?.screen.bounds.size ?? .zero
            }
            w?.pointee = Int32(size.width)
            h?.pointee = Int32(size.height)
        }

        host.device_name = { ctx, buf, cap in
            guard let engine = UmbrlyEngine.from(ctx), let buf, cap > 1 else { return 0 }
            var name = "iOS"
            DispatchQueue.main.sync { name = UIDevice.current.name }
            engine.copy(name, into: buf, cap: cap)
            return 1
        }

        host.open_url = { _, url in
            guard let url, let parsed = URL(string: String(cString: url)) else { return 0 }
            var accepted = false
            DispatchQueue.main.sync {
                accepted = UIApplication.shared.canOpenURL(parsed)
                if accepted { UIApplication.shared.open(parsed) }
            }
            return accepted ? 1 : 0
        }

        host.touch_x = { ctx in
            guard let engine = UmbrlyEngine.from(ctx) else { return 0 }
            engine.lock.lock(); defer { engine.lock.unlock() }
            return Double(engine.lastTouch.x)
        }

        host.touch_y = { ctx in
            guard let engine = UmbrlyEngine.from(ctx) else { return 0 }
            engine.lock.lock(); defer { engine.lock.unlock() }
            return Double(engine.lastTouch.y)
        }

        return host
    }

    private static func from(_ ctx: UnsafeMutableRawPointer?) -> UmbrlyEngine? {
        guard let ctx else { return nil }
        return Unmanaged<UmbrlyEngine>.fromOpaque(ctx).takeUnretainedValue()
    }

    /// Writes `value` as NUL-terminated UTF-8, truncating on a character
    /// boundary so a long answer cannot hand invalid bytes to the C side.
    fileprivate func copy(_ value: String, into buf: UnsafeMutablePointer<CChar>, cap: Int32) {
        var bytes = Array(value.utf8.prefix(Int(cap) - 1))
        while !bytes.isEmpty && String(bytes: bytes, encoding: .utf8) == nil {
            bytes.removeLast()
        }
        bytes.append(0)
        bytes.withUnsafeBufferPointer { src in
            src.baseAddress!.withMemoryRebound(to: CChar.self, capacity: bytes.count) { typed in
                buf.update(from: typed, count: bytes.count)
            }
        }
    }

    // MARK: Blocking waits

    /// Blocks the interpreter thread until a line arrives, or nil if stopped.
    private func awaitLine() -> String? {
        let console = self.console
        DispatchQueue.main.async { MainActor.assumeIsolated { console.demand = .line } }

        answerReady.wait()

        DispatchQueue.main.async { MainActor.assumeIsolated { console.demand = .none } }

        lock.lock()
        defer { lock.unlock() }
        if cancelled { return nil }
        let line = pendingLine
        pendingLine = nil
        return line
    }

    private func awaitKey(blocking: Bool) -> Int32 {
        if !blocking {
            lock.lock()
            defer { lock.unlock() }
            let key = pendingKey
            pendingKey = 0
            return key
        }

        let console = self.console
        DispatchQueue.main.async { MainActor.assumeIsolated { console.demand = .key } }

        answerReady.wait()

        DispatchQueue.main.async { MainActor.assumeIsolated { console.demand = .none } }

        lock.lock()
        defer { lock.unlock() }
        if cancelled { return 0 }
        let key = pendingKey
        pendingKey = 0
        return key
    }

    /// Presents an alert from the interpreter thread and waits for the tap.
    @discardableResult
    private func presentAlert(title: String, message: String, isConfirm: Bool) -> Bool {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            guard let root = UmbrlyEngine.topViewController() else {
                // No window to present on — unblock rather than hang forever.
                self.submit(confirm: false)
                return
            }

            let alert = UIAlertController(title: title, message: message,
                                          preferredStyle: .alert)
            if isConfirm {
                alert.addAction(UIAlertAction(title: "Нет", style: .cancel) { _ in
                    self.submit(confirm: false)
                })
                alert.addAction(UIAlertAction(title: "Да", style: .default) { _ in
                    self.submit(confirm: true)
                })
            } else {
                alert.addAction(UIAlertAction(title: "OK", style: .default) { _ in
                    self.submit(confirm: true)
                })
            }
            root.present(alert, animated: true)
        }

        answerReady.wait()
        lock.lock()
        defer { lock.unlock() }
        return confirmResult
    }

    @MainActor
    private static func topViewController() -> UIViewController? {
        let scene = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first { $0.activationState == .foregroundActive }
        var top = scene?.keyWindow?.rootViewController
        while let presented = top?.presentedViewController {
            top = presented
        }
        return top
    }
}

// MARK: - Console colours

extension ConsoleSegment {
    /// Maps the Windows console palette onto readable on-screen colours.
    var displayColor: Color {
        switch colorIndex {
        case 1, 9: return Color(red: 0.40, green: 0.62, blue: 1.00)   // blue
        case 2, 10: return Color(red: 0.36, green: 0.85, blue: 0.52)  // green
        case 3, 11: return Color(red: 0.36, green: 0.83, blue: 0.86)  // cyan
        case 4, 12: return Color(red: 1.00, green: 0.45, blue: 0.45)  // red
        case 5, 13: return Color(red: 0.83, green: 0.55, blue: 1.00)  // magenta
        case 6, 14: return Color(red: 0.97, green: 0.78, blue: 0.36)  // yellow
        case 8: return .secondary
        default: return .primary
        }
    }
}
