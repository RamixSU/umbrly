import SwiftUI
import UniformTypeIdentifiers
import UmbrlyCore

@main
struct UmbrlyApp: App {
    var body: some Scene {
        WindowGroup {
            LibraryView()
        }
    }
}

// MARK: - Script

/// A script the user can run, whether built in or loaded from a file.
struct Script: Identifiable, Hashable {
    let id = UUID()
    let name: String
    let subtitle: String
    let symbol: String
    let source: String

    static func == (a: Script, b: Script) -> Bool { a.id == b.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

// MARK: - Library

struct LibraryView: View {
    @State private var isImporting = false
    /// Set once something is picked; drives the confirmation sheet. Nothing is
    /// loaded or run until the user confirms.
    @State private var candidate: Script?
    @State private var active: Script?
    @State private var errorMessage: String?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 22) {
                    header

                    Text("Примеры")
                        .font(.headline)
                        .padding(.horizontal, 20)

                    GlassEffectContainer(spacing: 14) {
                        LazyVStack(spacing: 14) {
                            ForEach(Examples.all) { script in
                                Button {
                                    candidate = script
                                } label: {
                                    ScriptRow(script: script)
                                }
                                .buttonStyle(.plain)
                                .glassEffect(.regular, in: .rect(cornerRadius: 18))
                            }
                        }
                        .padding(.horizontal, 20)
                    }

                    openTile
                }
                .padding(.vertical, 18)
            }
            .background(Backdrop())
            .navigationTitle("Umbrly")
            .navigationDestination(item: $active) { script in
                ConsoleView(script: script)
            }
        }
        .fileImporter(
            isPresented: $isImporting,
            allowedContentTypes: Self.scriptTypes
        ) { result in
            // Deferred to the next main-actor tick: applied inline it would run
            // while the picker is still dismissing, and SwiftUI drops state
            // changes made during a presentation transition — the sheet would
            // silently never appear.
            Task { @MainActor in
                load(result)
            }
        }
        // Tap a script, look it over, then confirm — nothing runs until then.
        .sheet(item: $candidate) { script in
            ConfirmLoadSheet(script: script) {
                candidate = nil
                active = script
            } onCancel: {
                candidate = nil
            }
        }
        .alert("Не удалось открыть файл",
               isPresented: Binding(get: { errorMessage != nil },
                                    set: { if !$0 { errorMessage = nil } })) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(errorMessage ?? "")
        }
    }

    /// `.umb` has no UTI registered with the system, so iOS types it as a
    /// dynamic or generic kind. The picker renders anything outside
    /// `allowedContentTypes` as disabled — the row simply does not respond to a
    /// tap — and in iCloud Drive locations that filtering is stricter still.
    ///
    /// `public.item` is the root of the type hierarchy, so everything conforms
    /// to it and nothing can be greyed out. Whether the bytes are actually a
    /// script is decided after loading, where a bad file produces a real error
    /// message instead of an unexplained dead row in the picker.
    private static let scriptTypes: [UTType] = [.item]

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Интерпретатор Umbrly")
                .font(.title2.weight(.semibold))
            Text("Тот же движок, что в CLI и Android — собран для iOS.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 20)
    }

    private var openTile: some View {
        VStack(spacing: 12) {
            Button {
                isImporting = true
            } label: {
                HStack(spacing: 12) {
                    Image(systemName: "folder.badge.plus")
                        .font(.title3)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Открыть файл .umb")
                            .font(.body.weight(.medium))
                        Text("Выбери файл, затем подтверди загрузку")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 18)
                .padding(.vertical, 16)
                .contentShape(.rect)
            }
            .buttonStyle(.plain)
            .glassEffect(.regular, in: .rect(cornerRadius: 18))

            NavigationLink {
                PlatformReportView()
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: "info.circle")
                    Text("Что стало с функциями WinAPI")
                        .font(.footnote)
                    Spacer(minLength: 0)
                }
                .foregroundStyle(.secondary)
                .padding(.horizontal, 18)
                .padding(.vertical, 12)
                .contentShape(.rect)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 20)
    }

    @MainActor
    private func load(_ result: Result<URL, any Error>) {
        switch result {
        case .failure(let error):
            errorMessage = error.localizedDescription

        case .success(let url):
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }

            do {
                let data = try Data(contentsOf: url)
                guard let text = String(data: data, encoding: .utf8)
                        ?? String(data: data, encoding: .isoLatin1) else {
                    errorMessage = "«\(url.lastPathComponent)» не читается как текст."
                    return
                }
                candidate = Script(
                    name: url.lastPathComponent,
                    subtitle: "\(data.count) байт · из файлов",
                    symbol: "doc.text",
                    source: text
                )
            } catch {
                errorMessage = error.localizedDescription
            }
        }
    }
}

private struct ScriptRow: View {
    let script: Script

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: script.symbol)
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)

            VStack(alignment: .leading, spacing: 3) {
                Text(script.name)
                    .font(.body.weight(.medium))
                Text(script.subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer(minLength: 0)

            Image(systemName: "chevron.right")
                .font(.footnote.weight(.semibold))
                .foregroundStyle(.tertiary)
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 16)
        .contentShape(.rect)
    }
}

// MARK: - Confirmation

/// Shows what was picked and waits for an explicit confirmation.
struct ConfirmLoadSheet: View {
    let script: Script
    let onConfirm: () -> Void
    let onCancel: () -> Void

    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 0) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(script.name)
                        .font(.headline)
                    Text(script.subtitle)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 20)
                .padding(.bottom, 12)

                Divider()

                ScrollView {
                    Text(script.source)
                        .font(.system(size: 13, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                        .padding(20)
                }

                Button {
                    onConfirm()
                } label: {
                    Text("Загрузить и запустить")
                        .font(.body.weight(.semibold))
                        .frame(maxWidth: .infinity)
                        .frame(height: 50)
                }
                .buttonStyle(.glassProminent)
                .padding(20)
            }
            .navigationTitle("Подтверди загрузку")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Отмена") { onCancel() }
                }
            }
        }
    }
}

// MARK: - Console

struct ConsoleView: View {
    let script: Script

    @State private var console = ConsoleModel()
    @State private var engine: UmbrlyEngine?
    @State private var draft = ""
    @FocusState private var inputFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            transcript
            inputBar
        }
        .background(Backdrop())
        .navigationTitle(console.title)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                if console.isRunning {
                    Button("Стоп", systemImage: "stop.fill") { engine?.cancel() }
                } else {
                    Button("Заново", systemImage: "arrow.clockwise") { start() }
                }
            }
        }
        .onAppear { if engine == nil { start() } }
        .onDisappear { engine?.cancel() }
    }

    private var transcript: some View {
        ScrollViewReader { proxy in
            ScrollView {
                Text(rendered)
                    .font(.system(size: 14, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(20)
                    .id("end")
            }
            .onChange(of: console.segments.count) {
                withAnimation(.easeOut(duration: 0.15)) {
                    proxy.scrollTo("end", anchor: .bottom)
                }
            }
        }
    }

    /// Concatenates the coloured runs into one attributed string, so selection
    /// and wrapping behave like a single block of text.
    private var rendered: AttributedString {
        var out = AttributedString()
        for segment in console.segments {
            var piece = AttributedString(segment.text)
            piece.foregroundColor = segment.displayColor
            out.append(piece)
        }
        return out
    }

    @ViewBuilder
    private var inputBar: some View {
        if console.demand != .none {
            HStack(spacing: 10) {
                Image(systemName: console.demand == .key ? "keyboard" : "chevron.right")
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(.secondary)

                TextField(console.demand == .key ? "Клавиша, затем Ввод" : "Ответ",
                          text: $draft)
                    .textFieldStyle(.plain)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .focused($inputFocused)
                    .onSubmit(send)

                Button("Ввод", action: send)
                    .buttonStyle(.glassProminent)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(.bar)
            .onAppear { inputFocused = true }
        }
    }

    private func send() {
        guard let engine else { return }
        switch console.demand {
        case .line:
            // Echo the answer so the transcript reads like a real session.
            console.append(draft + "\n")
            engine.submit(line: draft)
        case .key:
            console.append(draft + "\n")
            engine.submit(key: Int32(draft.unicodeScalars.first?.value ?? 13))
        case .none:
            return
        }
        draft = ""
    }

    private func start() {
        let documents = FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask)[0]
        let engine = UmbrlyEngine(console: console, sandbox: documents)
        self.engine = engine
        engine.run(source: script.source)
    }
}

// MARK: - Platform report

struct PlatformReportView: View {
    var body: some View {
        ScrollView {
            Text(String(cString: umbrly_platform_report()))
                .font(.system(size: 13, design: .monospaced))
                .frame(maxWidth: .infinity, alignment: .leading)
                .textSelection(.enabled)
                .padding(20)
        }
        .background(Backdrop())
        .navigationTitle("WinAPI на iOS")
        .navigationBarTitleDisplayMode(.inline)
    }
}

// MARK: - Chrome

struct Backdrop: View {
    var body: some View {
        LinearGradient(
            colors: [
                Color(red: 0.22, green: 0.28, blue: 0.62),
                Color(red: 0.42, green: 0.24, blue: 0.58),
                Color(red: 0.62, green: 0.34, blue: 0.42)
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .opacity(0.35)
        .ignoresSafeArea()
        .background(Color(.systemBackground))
    }
}
