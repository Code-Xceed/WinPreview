import Foundation

public struct FileMetadata: Identifiable, Equatable {
    public let id: UUID
    public let fileName: String
    public let byteSize: Int64
    public let modifiedDate: Date

    public init(fileName: String, byteSize: Int64, modifiedDate: Date = Date()) {
        self.id = UUID()
        self.fileName = fileName
        self.byteSize = byteSize
        self.modifiedDate = modifiedDate
    }

    public var formattedSize: String {
        ByteCountFormatter.string(fromByteCount: byteSize, countStyle: .file)
    }
}

@MainActor
public final class PreviewViewModel: ObservableObject {
    @Published public private(set) var activeFiles: [FileMetadata] = []
    @Published public var selectedFile: FileMetadata?

    public init() {}

    public func register(file: FileMetadata) {
        activeFiles.append(file)
        if selectedFile == nil {
            selectedFile = file
        }
    }
}
