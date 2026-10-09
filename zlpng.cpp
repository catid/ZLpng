// ZLpng's native container around reversible image transforms and OpenZL.
#include "zlpng.h"
#ifndef ZPNG_FUSED8
#define ZPNG_FUSED8
#endif
#include "internal/filter_runtime.hpp"
#include "zlpng_backend.h"
#include <climits>
#include <cstdlib>
#include <memory>

#include "zlpng_policy.hpp"

using namespace zlpng_internal;

namespace {
using Allocation = std::unique_ptr<uint8_t, decltype(&std::free)>;

static bool big_endian_host() {
    const uint16_t probe = 1;
    return *reinterpret_cast<const uint8_t*>(&probe) == 0;
}

static void swap_sample_bytes(Bytes& bytes) {
    require(bytes.size() % 2 == 0, "Odd 16-bit sample payload");
    for (size_t i = 0; i < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
}

static size_t raw_size(unsigned w, unsigned h, unsigned channels, unsigned bytes) {
    require(w && h && w <= 65535 && h <= 65535, "Invalid image dimensions");
    require(channels >= 1 && channels <= 4 && (bytes == 1 || bytes == 2), "Invalid image format");
    const uint64_t raw = uint64_t(w) * h * channels * bytes;
    require(raw <= UINT_MAX, "Image exceeds buffer interface");
    return size_t(raw);
}

static size_t validate_input(const ZLPNG_ImageData* source) {
    require(source && source->Buffer.Data, "Missing image pixels");
    const size_t raw = raw_size(source->WidthPixels, source->HeightPixels,
                               source->Channels, source->BytesPerChannel);
    const size_t row = size_t(source->WidthPixels) * source->Channels * source->BytesPerChannel;
    require(source->StrideBytes >= row, "Short image stride");
    require(uint64_t(source->HeightPixels - 1) * source->StrideBytes + row <= source->Buffer.Bytes,
            "Short image buffer");
    return raw;
}

static const ZLPNG_Policy& policy_for(unsigned channels, unsigned bits) {
    for (const ZLPNG_Policy& policy : ZLPNG_POLICIES)
        if (policy.channels == channels && policy.bits == bits) {
            require(policy.count >= 1 && policy.count <= 8, "Invalid compression policy");
            return policy;
        }
    throw std::runtime_error("Unsupported image format");
}

static void copy_pixels(Image& image, const ZLPNG_ImageData& source) {
    const size_t row = size_t(image.w) * image.c * (image.bits / 8);
    image.bytes.resize(row * image.h);
    for (unsigned y = 0; y < image.h; ++y)
        std::memcpy(image.bytes.data() + size_t(y) * row,
                    source.Buffer.Data + size_t(y) * source.StrideBytes, row);
}

static Bytes transform(const Image& image, const R3Config& q, bool decode, const Bytes& input) {
    if (r5_runs_supported(image, q)) return r5_runs(image, q, decode, input);
    if (r5_triangular_supported(image, q)) return r5_triangular(image, q, decode, input);
    if (r5_packing_supported(image, q)) return r5_packing(image, q, decode, input);
    if (q.family >= 26 && q.family <= 30) return r5_blend(image, q, decode, input);
    return r3_run(image, q, decode, input, true);
}

static Config sample_config(const Image& image, const std::string& method) {
    Config q;
    if (method == "sample_raw") return q;
    q.layout = 1;
    if (method == "sample_planar") return q;
    if (method == "sample_med_none") { q.pred = 5; return q; }
    q.color = image.c >= 3 ? 2 : 0;
    if (method == "sample_med_green_planar") q.pred = 5;
    else if (method == "sample_gradient_green_planar") q.pred = 6;
    else if (method == "sample_left_green_planar") q.pred = 1;
    else throw std::runtime_error("Unknown sample transform");
    return q;
}

static Bytes legacy_header(const Image& image) {
    Bytes out(8);
    out[0] = 0xf8; out[1] = 0xfb;
    out[2] = uint8_t(image.w); out[3] = uint8_t(image.w >> 8);
    out[4] = uint8_t(image.h); out[5] = uint8_t(image.h >> 8);
    out[6] = uint8_t(image.c); out[7] = uint8_t(image.bits / 8);
    return out;
}

static void legacy_filter(const ZLPNG_ImageData& source, uint8_t* output) {
    switch (source.Channels * source.BytesPerChannel) {
#define ZLPNG_PIXEL(P) case P: r5_legacy_filter<P>(source, output); return
        ZLPNG_PIXEL(1); ZLPNG_PIXEL(2); ZLPNG_PIXEL(3);
        ZLPNG_PIXEL(4); ZLPNG_PIXEL(6); ZLPNG_PIXEL(8);
#undef ZLPNG_PIXEL
    }
    throw std::runtime_error("Invalid pixel width");
}

template<unsigned Pixel>
static void legacy_inverse(const uint8_t* input, uint8_t* output, const Image& image) {
    const size_t plane = size_t(image.w) * image.h;
    for (unsigned y = 0; y < image.h; ++y) {
        uint8_t previous[Pixel] = {};
        for (unsigned x = 0; x < image.w; ++x) {
            const size_t i = size_t(y) * image.w + x;
            if (Pixel == 3 || Pixel == 4) {
                const uint8_t blue = input[i];
                const uint8_t green = uint8_t(input[plane + i] + blue);
                previous[0] += uint8_t(green - input[2 * plane + i]);
                previous[1] += green; previous[2] += blue;
                if (Pixel == 4) previous[3] += input[3 * plane + i];
                for (unsigned c = 0; c < Pixel; ++c) output[i * Pixel + c] = previous[c];
            } else {
                for (unsigned c = 0; c < Pixel; ++c) {
                    previous[c] += input[i * Pixel + c];
                    output[i * Pixel + c] = previous[c];
                }
            }
        }
    }
}

static void legacy_unfilter(const uint8_t* input, uint8_t* output, const Image& image) {
    switch (image.c * (image.bits / 8)) {
#define ZLPNG_PIXEL(P) case P: legacy_inverse<P>(input, output, image); return
        ZLPNG_PIXEL(1); ZLPNG_PIXEL(2); ZLPNG_PIXEL(3);
        ZLPNG_PIXEL(4); ZLPNG_PIXEL(6); ZLPNG_PIXEL(8);
#undef ZLPNG_PIXEL
    }
    throw std::runtime_error("Invalid pixel width");
}

static Bytes filtered_payload(Image& image, const std::string& method, Bytes& head) {
    if (method.compare(0, 7, "sample_") == 0) {
        const Config q = sample_config(image, method);
        head = header(image, q); return run_filter(image, q);
    }
    if (method.compare(0, 3, "r3_") == 0) {
        const R3Config q = r3_parse(method, image.bits);
        Bytes payload = transform(image, q, false, image.bytes);
        head = r3_header(image, q, payload.size()); return payload;
    }
    if (method.compare(0, 3, "r2_") == 0) {
        const R2Config q = r2_parse(method, image.bits);
        Bytes payload = r2_run(image, q, false, image.bytes, true);
        head = r2_header(image, q, payload.size()); return payload;
    }
    const Config q = r3_old_config(method, image.bits);
    head = header(image, q); return run_filter(image, q);
}

static ZLPNG_Buffer encode_member(const ZLPNG_ImageData& source, Image& image,
                                  const ZLPNG_PolicyMember& member) {
    require(member.codec == 1 || member.codec == 2, "ZLpng requires OpenZL");
    const std::string method = member.method;
    require(member.codec != 2 || method.compare(0, 7, "sample_") == 0,
            "Numeric codec requires a sample transform");
    Bytes head, payload;
    Allocation packed(nullptr, &std::free);
    const uint8_t* input = nullptr;
    size_t size = 0;
    if (method == "legacy_level1") {
        size = size_t(image.w) * image.h * image.c * (image.bits / 8);
        packed.reset(static_cast<uint8_t*>(std::malloc(size)));
        if (!packed) throw std::bad_alloc();
        legacy_filter(source, packed.get()); input = packed.get(); head = legacy_header(image);
    } else {
        if (image.bytes.empty()) copy_pixels(image, source);
        payload = filtered_payload(image, method, head); input = payload.data(); size = payload.size();
    }
    const unsigned element = member.codec == 2 && method.compare(0, 7, "sample_") == 0 ? image.bits / 8 : 1;
    // OpenZL numeric inputs use host endianness; image transform payloads use
    // little-endian samples on every platform.
    if (member.codec == 2 && element == 2 && big_endian_host()) {
        swap_sample_bytes(payload); input = payload.data();
    }
    const size_t bound = zlpng_backend_bound(member.codec, size);
    const size_t offset = 8 + head.size();
    require(bound && bound <= UINT_MAX - offset, "Compressed allocation exceeds interface");
    Allocation output(static_cast<uint8_t*>(std::malloc(offset + bound)), &std::free);
    if (!output) throw std::bad_alloc();
    std::memcpy(output.get(), "ZLP1", 4);
    output.get()[4] = uint8_t(member.codec); output.get()[5] = uint8_t(element);
    output.get()[6] = uint8_t(head.size()); output.get()[7] = uint8_t(head.size() >> 8);
    std::memcpy(output.get() + 8, head.data(), head.size());
    const size_t compressed = zlpng_backend_compress(member.codec, member.level, member.window,
                                                  element, input, size, output.get() + offset, bound);
    require(compressed && compressed <= bound, "OpenZL compression failed");
    return {output.release(), unsigned(offset + compressed)};
}

static ZLPNG_ImageData decoded_image(const Image& shape, Allocation pixels, size_t raw) {
    ZLPNG_ImageData out = {};
    out.WidthPixels = shape.w; out.HeightPixels = shape.h;
    out.Channels = shape.c; out.BytesPerChannel = shape.bits / 8;
    out.StrideBytes = shape.w * shape.c * (shape.bits / 8);
    out.Buffer = {pixels.release(), unsigned(raw)};
    return out;
}

static ZLPNG_ImageData decode(ZLPNG_Buffer encoded, unsigned max_raw_bytes) {
    require(encoded.Data && encoded.Bytes >= 17 && !std::memcmp(encoded.Data, "ZLP1", 4), "Invalid ZLpng stream");
    const uint8_t* data = encoded.Data;
    const unsigned codec = data[4], element = data[5];
    const size_t hsize = unsigned(data[6]) | unsigned(data[7]) << 8;
    require((codec == 1 && element == 1) || (codec == 2 && (element == 1 || element == 2)), "Invalid OpenZL input type");
    require((hsize == 8 || hsize == 24 || hsize == 32 || hsize == 48) && 8 + hsize < encoded.Bytes,
            "Invalid transform header length");
    Bytes head(data + 8, data + 8 + hsize);
    Image image;
    if (hsize == 8) {
        require(head[0] == 0xf8 && head[1] == 0xfb && codec == 1, "Invalid legacy transform header");
        image.w = unsigned(head[2]) | unsigned(head[3]) << 8;
        image.h = unsigned(head[4]) | unsigned(head[5]) << 8;
        image.c = head[6]; image.bits = unsigned(head[7]) * 8;
    } else {
        const char* magic = hsize == 24 ? "ZPF1" : hsize == 32 ? "ZPF2" : "ZPF3";
        require(!std::memcmp(head.data(), magic, 4), "Wrong transform header magic");
        image.w = read32(head.data() + 4); image.h = read32(head.data() + 8);
        image.c = head[12]; image.bits = head[13];
    }
    require(image.bits == 8 || image.bits == 16, "Invalid bit depth");
    const size_t raw = raw_size(image.w, image.h, image.c, image.bits / 8);
    require(raw <= max_raw_bytes, "Image exceeds decode limit");
    size_t expected = raw;
    R3Config q3; R2Config q2; Config q1;
    if (hsize == 48) q3 = r3_read_header(head, image, expected);
    else if (hsize == 32) {
        require(head[19] == 0, "Invalid reserved header byte");
        q2 = r2_read_header(head, image, expected);
    } else if (hsize == 24) {
        q1 = read_header(head, image);
        require(head[22] == 0 && head[23] == 0 && (image.bits == 16 || !q1.split), "Invalid fixed transform flags");
        const uint64_t length = uint64_t(raw) + selectors(image, q1);
        require(length <= UINT_MAX && length <= std::numeric_limits<size_t>::max(),
                "Fixed transform payload exceeds interface");
        expected = size_t(length);
    }
    require(expected && expected <= UINT_MAX && expected <= uint64_t(raw) * 16 + 1048576,
            "Invalid transform payload size");
    if (codec == 2)
        require(hsize == 24 && element == image.bits / 8 && !q1.adaptive && !q1.split,
                "Numeric codec requires unsplit sample payload");
    const uint8_t* frame = data + 8 + hsize;
    const size_t compressed = encoded.Bytes - 8 - hsize;
    size_t frame_output = 0; unsigned frame_element = 0;
    require(zlpng_backend_inspect(frame, compressed, &frame_output, &frame_element) &&
            frame_output == expected && frame_element == (codec == 1 ? 1 : 0),
            "OpenZL frame metadata mismatch");

    if (hsize == 8) {
        Allocation packed(static_cast<uint8_t*>(std::malloc(raw)), &std::free);
        Allocation pixels(static_cast<uint8_t*>(std::malloc(raw)), &std::free);
        if (!packed || !pixels) throw std::bad_alloc();
        require(zlpng_backend_decompress_typed(codec, element, frame, compressed, packed.get(), raw) == raw,
                "OpenZL decompression failed");
        legacy_unfilter(packed.get(), pixels.get(), image);
        return decoded_image(image, std::move(pixels), raw);
    }
    Bytes payload(expected);
    require(zlpng_backend_decompress_typed(codec, element, frame, compressed, payload.data(), payload.size()) == expected,
            "OpenZL decompression failed");
    if (codec == 2 && element == 2 && big_endian_host()) swap_sample_bytes(payload);
    image.bytes.resize(raw, 0);
    Bytes pixels = hsize == 48 ? transform(image, q3, true, payload) :
                   hsize == 32 ? r2_run(image, q2, true, payload, true) : run_unfilter(payload, image, q1);
    require(pixels.size() == raw, "Reconstructed image size mismatch");
    Allocation output(static_cast<uint8_t*>(std::malloc(raw)), &std::free);
    if (!output) throw std::bad_alloc();
    std::memcpy(output.get(), pixels.data(), raw);
    return decoded_image(image, std::move(output), raw);
}
} // namespace

extern "C" ZLPNG_Buffer ZLPNG_Compress(const ZLPNG_ImageData* source, unsigned effort) {
    try {
        require(effort >= 1 && effort <= 8, "Effort must be 1..8");
        validate_input(source);
        Image image; image.w = source->WidthPixels; image.h = source->HeightPixels;
        image.c = source->Channels; image.bits = source->BytesPerChannel * 8;
        const ZLPNG_Policy& policy = policy_for(image.c, image.bits);
        Allocation best(nullptr, &std::free); unsigned best_size = 0;
        for (unsigned i = 0; i < std::min(effort, policy.count); ++i) {
            ZLPNG_Buffer encoded = encode_member(*source, image, policy.members[i]);
            Allocation candidate(encoded.Data, &std::free);
            if (!best || encoded.Bytes < best_size) {
                best = std::move(candidate); best_size = encoded.Bytes;
            }
        }
        return {best.release(), best_size};
    } catch (...) { return {}; }
}

extern "C" ZLPNG_ImageData ZLPNG_DecompressWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes) {
    try { return decode(encoded, max_raw_bytes); }
    catch (...) { return {}; }
}

extern "C" ZLPNG_ImageData ZLPNG_Decompress(ZLPNG_Buffer encoded) {
    return ZLPNG_DecompressWithLimit(encoded, UINT_MAX);
}

extern "C" void ZLPNG_Free(ZLPNG_Buffer* buffer) {
    if (buffer) { std::free(buffer->Data); buffer->Data = nullptr; buffer->Bytes = 0; }
}
