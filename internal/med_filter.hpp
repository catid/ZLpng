// Lossless median-edge prediction. Pixel samples and residuals are little endian.
#ifndef ZLPNG_MED_FILTER_HPP
#define ZLPNG_MED_FILTER_HPP
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>
#if defined(__SSE2__) && !defined(ZLPNG_MED_DISABLE_SIMD)
#include <immintrin.h>
#define ZLPNG_MED_SSE2 1
#endif

namespace zlpng_med {
struct Format {
    unsigned width, height, channels, bits;
};
inline bool supported(const Format& f) {
    return f.width && f.height && f.channels >= 1 && f.channels <= 4 &&
        (f.bits == 8 || f.bits == 16);
}
namespace detail {
#if defined(_MSC_VER)
#define ZLM_RESTRICT __restrict
#else
#define ZLM_RESTRICT __restrict__
#endif
template<class T> inline T load(const uint8_t* p) {
    if constexpr(sizeof(T) == 1) return *p;
    else return T(unsigned(p[0]) | unsigned(p[1]) << 8);
}
template<class T> inline void store(uint8_t* p, T value) {
    p[0] = uint8_t(value);
    if constexpr(sizeof(T) == 2) p[1] = uint8_t(value >> 8);
}
inline unsigned predict(unsigned l, unsigned u, unsigned ul) {
    const int swap = (int(l) ^ int(u)) & -int(l < u);
    const int lo = int(u) ^ swap, hi = int(l) ^ swap;
    int p = int(l) + int(u) - int(ul);
    p ^= (p ^ lo) & -int(p < lo);
    p ^= (p ^ hi) & -int(p > hi);
    return unsigned(p);
}
template<class T, unsigned C, bool Green>
inline void color_row(const uint8_t* ZLM_RESTRICT src, T* ZLM_RESTRICT dst,
                      unsigned width, size_t pitch) {
    T* p0 = dst + 1;
    T* p1 = dst + (C >= 2 ? pitch : 0) + 1;
    T* p2 = dst + (C >= 3 ? 2 * pitch : 0) + 1;
    T* p3 = dst + (C == 4 ? 3 * pitch : 0) + 1;
    for (unsigned x = 0; x < width; ++x) {
        const uint8_t* p = src + size_t(x) * C * sizeof(T);
        const T a = load<T>(p);
        if constexpr(C < 3 || !Green) {
            p0[x] = a;
            if constexpr(C >= 2) p1[x] = load<T>(p + sizeof(T));
            if constexpr(C >= 3) p2[x] = load<T>(p + 2 * sizeof(T));
        } else {
            const T g = load<T>(p + sizeof(T));
            p0[x] = g; p1[x] = T(a - g);
            p2[x] = T(load<T>(p + 2 * sizeof(T)) - g);
        }
        if constexpr(C == 4) p3[x] = load<T>(p + 3 * sizeof(T));
    }
}

// min(L,U) + sat(max(L,U)-UL) - sat(min(L,U)-UL) is exactly MED.
// Its narrow unsigned operations avoid widening either 8-bit or 16-bit samples.
template<class T>
inline void residual_row(const T* current, const T* above, uint8_t* output,
                         unsigned width) {
    unsigned x = 0;
#if defined(ZLPNG_MED_SSE2)
    const uint16_t endian_probe = 1;
    if (*reinterpret_cast<const uint8_t*>(&endian_probe)) {
#if defined(__AVX512BW__)
        for (; x + 64 / sizeof(T) <= width; x += 64 / sizeof(T)) {
            const __m512i v = _mm512_loadu_si512(current + x);
            const __m512i l = _mm512_loadu_si512(current + x - 1);
            const __m512i u = _mm512_loadu_si512(above + x);
            const __m512i ul = _mm512_loadu_si512(above + x - 1);
            __m512i prediction, residual;
            if constexpr(sizeof(T) == 1) {
                const __m512i lo = _mm512_min_epu8(l, u), hi = _mm512_max_epu8(l, u);
                prediction = _mm512_sub_epi8(_mm512_add_epi8(lo, _mm512_subs_epu8(hi, ul)), _mm512_subs_epu8(lo, ul));
                residual = _mm512_sub_epi8(v, prediction);
            } else {
                const __m512i lo = _mm512_min_epu16(l, u), hi = _mm512_max_epu16(l, u);
                prediction = _mm512_sub_epi16(_mm512_add_epi16(lo, _mm512_subs_epu16(hi, ul)), _mm512_subs_epu16(lo, ul));
                residual = _mm512_sub_epi16(v, prediction);
            }
            _mm512_storeu_si512(output + size_t(x) * sizeof(T), residual);
        }
#endif
#if defined(__AVX2__)
        for (; x + 32 / sizeof(T) <= width; x += 32 / sizeof(T)) {
            const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current + x));
            const __m256i l = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current + x - 1));
            const __m256i u = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(above + x));
            const __m256i ul = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(above + x - 1));
            __m256i prediction, residual;
            if constexpr(sizeof(T) == 1) {
                const __m256i lo = _mm256_min_epu8(l, u), hi = _mm256_max_epu8(l, u);
                prediction = _mm256_sub_epi8(_mm256_add_epi8(lo, _mm256_subs_epu8(hi, ul)), _mm256_subs_epu8(lo, ul));
                residual = _mm256_sub_epi8(v, prediction);
            } else {
                const __m256i lo = _mm256_min_epu16(l, u), hi = _mm256_max_epu16(l, u);
                prediction = _mm256_sub_epi16(_mm256_add_epi16(lo, _mm256_subs_epu16(hi, ul)), _mm256_subs_epu16(lo, ul));
                residual = _mm256_sub_epi16(v, prediction);
            }
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + size_t(x) * sizeof(T)), residual);
        }
#endif
        for (; x + 16 / sizeof(T) <= width; x += 16 / sizeof(T)) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(current + x));
            const __m128i l = _mm_loadu_si128(reinterpret_cast<const __m128i*>(current + x - 1));
            const __m128i u = _mm_loadu_si128(reinterpret_cast<const __m128i*>(above + x));
            const __m128i ul = _mm_loadu_si128(reinterpret_cast<const __m128i*>(above + x - 1));
            __m128i prediction, residual;
            if constexpr(sizeof(T) == 1) {
                const __m128i lo = _mm_min_epu8(l, u), hi = _mm_max_epu8(l, u);
                prediction = _mm_sub_epi8(_mm_add_epi8(lo, _mm_subs_epu8(hi, ul)), _mm_subs_epu8(lo, ul));
                residual = _mm_sub_epi8(v, prediction);
            } else {
                const __m128i delta = _mm_subs_epu16(l, u);
                const __m128i lo = _mm_sub_epi16(l, delta), hi = _mm_add_epi16(u, delta);
                prediction = _mm_sub_epi16(_mm_add_epi16(lo, _mm_subs_epu16(hi, ul)), _mm_subs_epu16(lo, ul));
                residual = _mm_sub_epi16(v, prediction);
            }
            _mm_storeu_si128(reinterpret_cast<__m128i*>(output + size_t(x) * sizeof(T)), residual);
        }
    }
#endif
    for (; x < width; ++x)
        store<T>(output + size_t(x) * sizeof(T), T(current[x] - predict((current + x)[-1], above[x], (above + x)[-1])));
}

template<class T, unsigned C, bool Green>
void encode_impl(const uint8_t* pixels, size_t stride, uint8_t* packed, const Format& f) {
    const size_t pitch = size_t(f.width) + 1, row = pitch * C;
    std::vector<T> scratch(2 * row, 0);
    T* previous = scratch.data(); T* current = previous + row;
    const size_t plane = size_t(f.width) * f.height;
    for (unsigned y = 0; y < f.height; ++y) {
        color_row<T, C, Green>(pixels + size_t(y) * stride, current, f.width, pitch);
        for (unsigned c = 0; c < C; ++c) {
            uint8_t* output = packed + (size_t(c) * plane + size_t(y) * f.width) * sizeof(T);
            residual_row(current + c * pitch + 1, previous + c * pitch + 1, output,
                         f.width);
        }
        std::swap(previous, current);
    }
}

#if defined(ZLPNG_MED_SSE2)
template<class T, unsigned C, bool Green>
void decode_simd(const uint8_t* packed, uint8_t* pixels, size_t stride, const Format& f) {
    // Each lane is one independent channel. Horizontal prediction retains a
    // real left dependency; advancing multiple pixels together is not valid.
    using Pixel = typename std::conditional<sizeof(T) == 1, uint32_t, uint64_t>::type;
    std::vector<Pixel> scratch(size_t(f.width) * 2, 0);
    Pixel* previous = scratch.data(); Pixel* current = previous + f.width;
    const size_t plane = size_t(f.width) * f.height;
    for (unsigned y = 0; y < f.height; ++y) {
        const uint8_t* input[C];
        for (unsigned c = 0; c < C; ++c) input[c] = packed + (size_t(c) * plane + size_t(y) * f.width) * sizeof(T);
        __m128i left = _mm_setzero_si128(), upper_left = left;
        uint8_t* output = pixels + size_t(y) * stride;
        for (unsigned x = 0; x < f.width; ++x) {
            Pixel residual = 0;
            for (unsigned c = 0; c < C; ++c) residual |= Pixel(load<T>(input[c] + size_t(x) * sizeof(T))) << (c * 8 * sizeof(T));
            const __m128i u = _mm_cvtsi64_si128(previous[x]);
            const __m128i r = _mm_cvtsi64_si128(residual);
            if constexpr(sizeof(T) == 1) {
                const __m128i lo = _mm_min_epu8(u, upper_left), hi = _mm_max_epu8(u, upper_left);
                const __m128i clipped = _mm_min_epu8(_mm_max_epu8(left, lo), hi);
                left = _mm_sub_epi8(_mm_add_epi8(left, _mm_add_epi8(r, u)), clipped);
            } else {
#if defined(__SSE4_1__)
                const __m128i lo = _mm_min_epu16(u, upper_left), hi = _mm_max_epu16(u, upper_left);
                const __m128i clipped = _mm_min_epu16(_mm_max_epu16(left, lo), hi);
                left = _mm_sub_epi16(_mm_add_epi16(left, _mm_add_epi16(r, u)), clipped);
#else
                const __m128i delta = _mm_subs_epu16(left, u);
                const __m128i lo = _mm_sub_epi16(left, delta), hi = _mm_add_epi16(u, delta);
                const __m128i p = _mm_sub_epi16(_mm_add_epi16(lo, _mm_subs_epu16(hi, upper_left)), _mm_subs_epu16(lo, upper_left));
                left = _mm_add_epi16(r, p);
#endif
            }
            upper_left = u;
            const Pixel value = Pixel(_mm_cvtsi128_si64(left)); current[x] = value;
            T values[C];
            for (unsigned c = 0; c < C; ++c) values[c] = T(value >> (c * 8 * sizeof(T)));
            if constexpr(Green && C >= 3) {
                const T g = values[0]; values[0] = T(g + values[1]);
                values[1] = g; values[2] = T(g + values[2]);
            }
            for (unsigned c = 0; c < C; ++c) store<T>(output + (size_t(x) * C + c) * sizeof(T), values[c]);
        }
        std::swap(previous, current);
    }
}
#endif
template<class T, unsigned C, bool Green>
void decode_impl(const uint8_t* packed, uint8_t* pixels, size_t stride, const Format& f) {
#if defined(ZLPNG_MED_SSE2)
    decode_simd<T,C,Green>(packed,pixels,stride,f); return;
#endif
    const size_t pitch = size_t(f.width) + 1, row = pitch * C;
    std::vector<T> scratch(2 * row, 0);
    T* previous = scratch.data(); T* current = previous + row;
    const size_t plane = size_t(f.width) * f.height;
    for (unsigned y = 0; y < f.height; ++y) {
        const uint8_t* input[C];
        for (unsigned c = 0; c < C; ++c) input[c] = packed + (size_t(c) * plane + size_t(y) * f.width) * sizeof(T);
        T left[C] = {};
        uint8_t* output = pixels + size_t(y) * stride;
        for (unsigned x = 0; x < f.width; ++x) {
            T values[C];
            for (unsigned c = 0; c < C; ++c) {
                const uint8_t* p = input[c] + size_t(x) * sizeof(T);
                const T residual = load<T>(p);
                const T* upper = previous + c * pitch + 1;
                values[c] = T(residual + predict(left[c], upper[x], (upper + x)[-1]));
                current[c * pitch + x + 1] = left[c] = values[c];
            }
            if constexpr(Green && C >= 3) {
                const T g = values[0]; values[0] = T(g + values[1]);
                values[1] = g; values[2] = T(g + values[2]);
            }
            for (unsigned c = 0; c < C; ++c) store<T>(output + (size_t(x) * C + c) * sizeof(T), values[c]);
        }
        std::swap(previous, current);
    }
}
template<bool Decode, class T, unsigned C>
void color_dispatch(const uint8_t* source, size_t stride, uint8_t* output, const Format& f) {
    if constexpr(Decode) {
        if constexpr(sizeof(T) == 1 && C >= 3) decode_impl<T, C, true>(source, output, stride, f);
        else decode_impl<T, C, false>(source, output, stride, f);
    } else {
        if constexpr(sizeof(T) == 1 && C >= 3) encode_impl<T, C, true>(source, stride, output, f);
        else encode_impl<T, C, false>(source, stride, output, f);
    }
}
template<bool Decode, class T>
void dispatch(const uint8_t* source, size_t stride, uint8_t* output, const Format& f) {
    switch (f.channels) {
        case 1: color_dispatch<Decode, T, 1>(source, stride, output, f); break;
        case 2: color_dispatch<Decode, T, 2>(source, stride, output, f); break;
        case 3: color_dispatch<Decode, T, 3>(source, stride, output, f); break;
        case 4: color_dispatch<Decode, T, 4>(source, stride, output, f); break;
    }
}
#undef ZLM_RESTRICT
} // namespace detail
inline void encode(const uint8_t* pixels, size_t stride, uint8_t* packed, const Format& f) {
    if (!supported(f) || !pixels || !packed || stride < size_t(f.width) * f.channels * (f.bits / 8))
        throw std::invalid_argument("Invalid MED input");
    if (f.bits == 8) detail::dispatch<false, uint8_t>(pixels, stride, packed, f);
    else detail::dispatch<false, uint16_t>(pixels, stride, packed, f);
}
inline void decode(const uint8_t* packed, uint8_t* pixels, size_t stride, const Format& f) {
    if (!supported(f) || !pixels || !packed || stride < size_t(f.width) * f.channels * (f.bits / 8))
        throw std::invalid_argument("Invalid MED output");
    if (f.bits == 8) detail::dispatch<true, uint8_t>(packed, stride, pixels, f);
    else detail::dispatch<true, uint16_t>(packed, stride, pixels, f);
}
} // namespace zlpng_med
#endif
