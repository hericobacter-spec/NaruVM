import Foundation

@main
struct FetchROM {
    static func main() async throws {
        guard CommandLine.arguments.count == 2 else {
            throw NSError(domain: "HengeLab", code: 2, userInfo: [NSLocalizedDescriptionKey: "usage: fetch-rom OUTPUT_FILE"])
        }
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        try FileManager.default.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
        let rom = try await BootROM.download()
        try rom.write(to: output, options: .atomic)
        print("ROM verified using upstream SHA-256: \(rom.count) bytes -> \(output.path)")
    }
}
