// Authored Metal reference for the Android primitive-order experiment; no guest shaders.
import Foundation
import Metal

func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw NSError(domain: message, code: 1) }
}
func run(_ command: MTLCommandBuffer) throws {
    command.commit()
    command.waitUntilCompleted()
    try require(command.status == .completed, "GPU command failed: \(String(describing: command.error))")
}
func qualify(_ libraryURL: URL) throws {
    guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
        throw NSError(domain: "Metal device missing", code: 1)
    }
    try require(device.areRasterOrderGroupsSupported, "native Metal ROG unavailable")
    print("METAL_REFERENCE device=\(device.name) native_rog=\(device.areRasterOrderGroupsSupported)")
    let library = try device.makeLibrary(URL: libraryURL)
    for (width, height) in [(32, 32), (1024, 768)] {
        for drawKind in ["instanced", "indexed16", "indexed32"] {
        for (format, native, serial, steps, bpp) in [
            (MTLPixelFormat.r32Uint, "lab_texture_native_rog", "lab_texture_serial", 16, 4),
            (.rgba16Float, "lab_texture_native_rog_half", "lab_texture_serial_half", 8, 8)
        ] {
            let image = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: width, height: height, mipmapped: false)
            image.usage = [.shaderRead, .shaderWrite]
            image.storageMode = .private
            let color = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: width, height: height, mipmapped: false)
            color.usage = .renderTarget
            color.storageMode = .private
            guard let texture = device.makeTexture(descriptor: image), let target = device.makeTexture(descriptor: color),
                  let zeros = device.makeBuffer(length: width * height * bpp, options: .storageModeShared),
                  let readback = device.makeBuffer(length: width * height * bpp, options: .storageModeShared) else {
                throw NSError(domain: "owned resource allocation failed", code: 1)
            }
            memset(zeros.contents(), 0, zeros.length)
            var indexBytes = Data()
            if drawKind == "indexed16" {
                for x in [0x7fff, 0x7fff] + Array(3..<(3 + 3 * steps)) {
                    var value = UInt16(x).littleEndian
                    withUnsafeBytes(of: &value) { indexBytes.append(contentsOf: $0) }
                }
            } else {
                for x in [0x7fffffff] + Array(3..<(3 + 3 * steps)) {
                    var value = UInt32(x).littleEndian
                    withUnsafeBytes(of: &value) { indexBytes.append(contentsOf: $0) }
                }
            }
            let indices = indexBytes.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared)! }
            let indexType: MTLIndexType = drawKind == "indexed16" ? .uint16 : .uint32
            let indexSize = drawKind == "indexed16" ? 2 : 4
            var reference: Data?
            for (mode, entry) in [("native-rog", native), ("serialized-render-passes", serial)] {
                let descriptor = MTLRenderPipelineDescriptor()
                descriptor.vertexFunction = library.makeFunction(name: drawKind == "instanced" ? "lab_order_vertex" : "lab_order_list_vertex")
                descriptor.fragmentFunction = library.makeFunction(name: entry)
                descriptor.colorAttachments[0].pixelFormat = .rgba8Unorm
                let pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
                guard let clearCommand = queue.makeCommandBuffer(), let clear = clearCommand.makeBlitCommandEncoder() else {
                    throw NSError(domain: "clear encoder unavailable", code: 1)
                }
                clear.copy(from: zeros, sourceOffset: 0, sourceBytesPerRow: width * bpp,
                           sourceBytesPerImage: zeros.length, sourceSize: MTLSize(width: width, height: height, depth: 1),
                           to: texture, destinationSlice: 0, destinationLevel: 0, destinationOrigin: MTLOrigin())
                clear.endEncoding()
                try run(clearCommand)
                guard let command = queue.makeCommandBuffer() else { throw NSError(domain: "command unavailable", code: 1) }
                let start = DispatchTime.now().uptimeNanoseconds
                let passes = mode == "native-rog" ? 1 : steps
                for index in 0..<passes {
                    let pass = MTLRenderPassDescriptor()
                    pass.colorAttachments[0].texture = target
                    pass.colorAttachments[0].loadAction = .clear
                    pass.colorAttachments[0].storeAction = .store
                    guard let render = command.makeRenderCommandEncoder(descriptor: pass) else {
                        throw NSError(domain: "render encoder unavailable", code: 1)
                    }
                    render.setRenderPipelineState(pipeline)
                    render.setFragmentTexture(texture, index: 0)
                    if drawKind == "instanced" {
                        render.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3,
                                              instanceCount: mode == "native-rog" ? steps : 1,
                                              baseInstance: mode == "native-rog" ? 0 : index)
                    } else {
                        render.drawIndexedPrimitives(type: .triangle, indexCount: mode == "native-rog" ? 3 * steps : 3,
                            indexType: indexType, indexBuffer: indices,
                            indexBufferOffset: 4 + (mode == "native-rog" ? 0 : 3 * index * indexSize),
                            instanceCount: 1, baseVertex: -3, baseInstance: 5)
                    }
                    render.endEncoding()
                }
                try run(command)
                let elapsed = (DispatchTime.now().uptimeNanoseconds - start) / 1000
                guard let copyCommand = queue.makeCommandBuffer(), let copy = copyCommand.makeBlitCommandEncoder() else {
                    throw NSError(domain: "readback encoder unavailable", code: 1)
                }
                copy.copy(from: texture, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(),
                          sourceSize: MTLSize(width: width, height: height, depth: 1), to: readback,
                          destinationOffset: 0, destinationBytesPerRow: width * bpp, destinationBytesPerImage: readback.length)
                copy.endEncoding()
                try run(copyCommand)
                let data = Data(bytes: readback.contents(), count: readback.length)
                let expected: [UInt8]
                if format == .r32Uint {
                    var value: UInt32 = 0
                    for step in 1...steps { value = value &* 3 &+ UInt32(step + (drawKind == "instanced" ? 0 : 5)) }
                    expected = withUnsafeBytes(of: value.littleEndian) { Array($0) }
                } else {
                    expected = drawKind == "instanced" ? [0x01, 0x4b, 0, 0, 0, 0, 0, 0] : [0xfe, 0x4d, 0, 0, 0, 0, 0, 0]
                }
                for offset in stride(from: 0, to: data.count, by: bpp) {
                    try require(Array(data[offset..<(offset + bpp)]) == expected, "\(mode) pixel mismatch at \(offset / bpp)")
                }
                if let baseline = reference {
                    try require(data == baseline, "serialized bytes differ from native ROG")
                } else { reference = data }
                print("PASS draw=\(drawKind) mode=\(mode) format=\(format.rawValue) size=\(width)x\(height) overlaps=\(steps) pixels=\(width * height) elapsed_us=\(elapsed)")
            }
        }
    }
    }
    print("METAL_REFERENCE_RESULT failures=0 scope=authored-single-sample-triangles guest_integration=0")
}
do {
    try require(CommandLine.arguments.count == 2, "usage: qualify_primitive_order check.metallib")
    try qualify(URL(fileURLWithPath: CommandLine.arguments[1]))
} catch {
    fputs("FAIL \(error)\n", stderr)
    exit(1)
}
