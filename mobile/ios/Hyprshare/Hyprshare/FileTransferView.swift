import SwiftUI
import UniformTypeIdentifiers

struct FileTransferView: View {
    let device: DiscoveredDevice
    @EnvironmentObject var client: HyprshareClient
    @State private var selectedFiles: [SelectedFile] = []
    @State private var isSending = false
    @State private var sendProgress: Double = 0
    @State private var statusMessage: String?
    @State private var isError = false
    @State private var showDocumentPicker = false

    var body: some View {
        VStack(spacing: 0) {
            if selectedFiles.isEmpty {
                ContentUnavailableView(
                    "No Files Selected",
                    systemImage: "doc.badge.plus",
                    description: Text("Select files to send to \(device.name)")
                )
            } else {
                List {
                    ForEach(selectedFiles) { file in
                        FileRow(file: file) {
                            selectedFiles.removeAll { $0.id == file.id }
                        }
                    }
                    .onDelete { indexSet in
                        selectedFiles.remove(atOffsets: indexSet)
                    }
                }
                .listStyle(.plain)
            }

            if let msg = statusMessage {
                Text(msg)
                    .font(.caption)
                    .foregroundStyle(isError ? .red : .green)
                    .padding(.vertical, 4)
            }

            if isSending {
                ProgressView(value: sendProgress)
                    .progressViewStyle(.linear)
                    .padding(.horizontal)
            }

            HStack {
                Button(action: { showDocumentPicker = true }) {
                    Label("Add Files", systemImage: "plus")
                }
                .buttonStyle(.bordered)
                .disabled(isSending)

                Spacer()

                Button(action: { Task { await sendFiles() } }) {
                    Label("Send", systemImage: "arrow.up.circle.fill")
                }
                .buttonStyle(.borderedProminent)
                .disabled(selectedFiles.isEmpty || isSending)
            }
            .padding()
        }
        .navigationTitle("Send to \(device.name)")
        .navigationBarTitleDisplayMode(.inline)
        .fileImporter(
            isPresented: $showDocumentPicker,
            allowedContentTypes: [.item],
            allowsMultipleSelection: true
        ) { result in
            switch result {
            case .success(let urls):
                for url in urls {
                    guard url.startAccessingSecurityScopedResource() else { continue }
                    let fileSize = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int) ?? 0
                    selectedFiles.append(SelectedFile(
                        url: url,
                        name: url.lastPathComponent,
                        size: fileSize
                    ))
                }
            case .failure(let error):
                statusMessage = error.localizedDescription
                isError = true
            }
        }
    }

    private func sendFiles() async {
        isSending = true
        sendProgress = 0
        statusMessage = nil
        isError = false

        let total = Double(selectedFiles.count)
        var sent = 0

        for file in selectedFiles {
            do {
                let fileData = try Data(contentsOf: file.url)
                file.url.stopAccessingSecurityScopedResource()

                let fileId = UUID().uuidString
                let sha256 = HyprshareClient.computeSHA256(fileData)
                let mimeType = HyprshareClient.mimeType(for: file.url)

                let accepted = await client.sendFileOffer(
                    fileId: fileId,
                    name: file.name,
                    size: fileData.count,
                    mimeType: mimeType,
                    sha256: sha256
                )
                guard accepted else {
                    statusMessage = "\(file.name): rejected by receiver"
                    isError = true
                    isSending = false
                    return
                }

                let chunkSize = 65536
                var offset = 0
                while offset < fileData.count {
                    let end = min(offset + chunkSize, fileData.count)
                    let chunk = fileData.subdata(in: offset..<end)
                    let ok = await client.sendFileData(fileId: fileId, data: chunk, offset: offset)
                    guard ok else {
                        statusMessage = "\(file.name): transfer failed"
                        isError = true
                        isSending = false
                        return
                    }
                    offset = end
                }

                await client.sendFileComplete(fileId: fileId)
                sent += 1
                sendProgress = Double(sent) / total
            } catch {
                statusMessage = "\(file.name): \(error.localizedDescription)"
                isError = true
                isSending = false
                return
            }
        }

        statusMessage = "Sent \(sent) file(s) to \(device.name)"
        isError = false
        isSending = false
        selectedFiles = []
    }
}

struct FileRow: View {
    let file: SelectedFile
    let onRemove: () -> Void

    var body: some View {
        HStack {
            Image(systemName: iconForFile(file.name))
                .font(.title2)
                .foregroundStyle(.blue)
                .frame(width: 32)

            VStack(alignment: .leading, spacing: 2) {
                Text(file.name)
                    .font(.body)
                    .lineLimit(1)
                Text(formatFileSize(file.size))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            Button(action: onRemove) {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
        }
    }

    private func iconForFile(_ name: String) -> String {
        let ext = (name as NSString).pathExtension.lowercased()
        switch ext {
        case "jpg", "jpeg", "png", "gif", "heic", "webp": return "photo"
        case "mp4", "mov", "avi": return "film"
        case "mp3", "wav", "m4a": return "music.note"
        case "pdf": return "doc.richtext"
        case "zip", "tar", "gz": return "archivebox"
        default: return "doc"
        }
    }
}

func formatFileSize(_ size: Int) -> String {
    let units = ["B", "KB", "MB", "GB"]
    var value = Double(size)
    var unitIndex = 0
    while value >= 1024 && unitIndex < units.count - 1 {
        value /= 1024
        unitIndex += 1
    }
    return String(format: "%.1f %@", value, units[unitIndex])
}

#Preview {
    NavigationStack {
        FileTransferView(device: DiscoveredDevice(
            name: "Desktop",
            host: "192.168.1.100",
            port: 28556,
            capabilities: ["file_transfer", "screen_share"]
        ))
    }
    .environmentObject(HyprshareClient())
}
