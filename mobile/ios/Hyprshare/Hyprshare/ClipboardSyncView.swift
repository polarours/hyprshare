import SwiftUI

struct ClipboardSyncView: View {
    @EnvironmentObject var client: HyprshareClient
    @State private var inputText = ""
    @State private var clipboardHistory: [ClipboardEntry] = []
    @State private var targetDevice: DiscoveredDevice?
    @State private var isListening = false

    struct ClipboardEntry: Identifiable {
        let id = UUID()
        let text: String
        let timestamp: Date
        let isRemote: Bool
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                if let content = client.lastClipboardContent {
                    if clipboardHistory.isEmpty || clipboardHistory.first?.text != content {
                        Color.clear.onAppear {
                            clipboardHistory.insert(
                                ClipboardEntry(text: content, timestamp: Date(), isRemote: true),
                                at: 0
                            )
                        }
                    }
                }

                HStack {
                    TextField("Text to sync", text: $inputText, axis: .vertical)
                        .lineLimit(2...4)
                        .textFieldStyle(.roundedBorder)

                    Button(action: sendText) {
                        Image(systemName: "arrow.up.circle.fill")
                            .font(.title2)
                    }
                    .disabled(inputText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
                .padding()

                Divider()

                HStack {
                    Button(action: pasteFromClipboard) {
                        Label("Paste", systemImage: "doc.on.clipboard")
                    }
                    .buttonStyle(.bordered)

                    Spacer()

                    Text("\(clipboardHistory.count) entries")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    Spacer()

                    Button(action: { clipboardHistory.removeAll() }) {
                        Label("Clear", systemImage: "trash")
                    }
                    .buttonStyle(.bordered)
                    .tint(.red)
                    .disabled(clipboardHistory.isEmpty)
                }
                .padding(.horizontal)
                .padding(.vertical, 8)

                Divider()

                if clipboardHistory.isEmpty {
                    ContentUnavailableView(
                        "No Clipboard Entries",
                        systemImage: "clipboard",
                        description: Text("Send text or paste from your clipboard to sync.")
                    )
                } else {
                    List(clipboardHistory) { entry in
                        ClipboardRow(entry: entry)
                            .listRowSeparator(.hidden)
                            .onTapGesture {
                                copyToClipboard(entry.text)
                            }
                    }
                    .listStyle(.plain)
                }
            }
            .navigationTitle("Clipboard")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Toggle(isOn: $isListening) {
                        Label("Listen", systemImage: isListening ? "ear.badge.waveform" : "ear")
                    }
                    .toggleStyle(.switch)
                    .onChange(of: isListening) { _, newValue in
                        if newValue {
                            startListening()
                        } else {
                            stopListening()
                        }
                    }
                }
            }
        }
    }

    private func sendText() {
        let text = inputText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }

        Task {
            await client.sendClipboard(content: text)
        }

        clipboardHistory.insert(
            ClipboardEntry(text: text, timestamp: Date(), isRemote: false),
            at: 0
        )
        inputText = ""
    }

    private func pasteFromClipboard() {
        if let string = UIPasteboard.general.string {
            inputText = string
        }
    }

    private func copyToClipboard(_ text: String) {
        UIPasteboard.general.string = text
    }

    private func startListening() {
        // Periodically check local clipboard for changes
        Task {
            while isListening {
                if let content = UIPasteboard.general.string, !content.isEmpty {
                    let lastRemote = clipboardHistory.first(where: { !$0.isRemote })
                    if lastRemote?.text != content {
                        await client.sendClipboard(content: content)
                    }
                }
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }

    private func stopListening() {
        isListening = false
    }
}

struct ClipboardRow: View {
    let entry: ClipboardSyncView.ClipboardEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Label(entry.isRemote ? "Remote" : "Local",
                      systemImage: entry.isRemote ? "arrow.down.circle" : "arrow.up.circle")
                    .font(.caption)
                    .foregroundStyle(entry.isRemote ? .green : .blue)

                Spacer()

                Text(entry.timestamp, style: .time)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            Text(entry.text)
                .font(.body)
                .lineLimit(3)
        }
        .padding(.vertical, 4)
    }
}

#Preview {
    ClipboardSyncView()
        .environmentObject(HyprshareClient())
}
