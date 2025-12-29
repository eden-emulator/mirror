// SPDX-License-Identifier: MPL-2.0
// Copyright 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright 2019 The SwiftShader Authors. All Rights Reserved.

// This BCn Decoder is directly derivative of Swiftshader's BCn Decoder found at: https://github.com/google/swiftshader/blob/d070309f7d154d6764cbd514b1a5c8bfcef61d06/src/Device/BC_Decoder.cpp
// This file does not follow the Skyline code conventions but has certain Skyline specific code
// There are a lot of implicit and narrowing conversions in this file due to this (Warnings are disabled as a result)

#include <array>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <bit>

namespace {
    constexpr int32_t BlockWidth = 4;
    constexpr int32_t BlockHeight = 4;

    struct BC_color {
        void decode(uint8_t *dst, size_t x, size_t y, size_t dstW, size_t dstH, size_t dstPitch, size_t dstBpp, bool hasAlphaChannel, bool hasSeparateAlpha) const {
            Color c[4];
            c[0].extract565(c0);
            c[1].extract565(c1);
            if (hasSeparateAlpha || (c0 > c1)) {
                c[2] = ((c[0] * 2) + c[1]) / 3;
                c[3] = ((c[1] * 2) + c[0]) / 3;
            } else {
                c[2] = (c[0] + c[1]) >> 1;
                if (hasAlphaChannel)
                    c[3].clearAlpha();
            }

            for (int32_t j = 0; j < BlockHeight && (y + j) < dstH; j++) {
                size_t dstOffset = j * dstPitch, idxOffset = j * BlockHeight;
                for (size_t i = 0; i < BlockWidth && (x + i) < dstW; i++, idxOffset++, dstOffset += dstBpp) {
                    *reinterpret_cast<uint32_t *>(dst + dstOffset) = c[getIdx(idxOffset)].pack8888();
                }
            }
        }

      private:
        struct Color {
            Color() {
                c[0] = c[1] = c[2] = 0;
                c[3] = 0xFF000000;
            }

            void extract565(const uint32_t c565) {
                c[0] = ((c565 & 0x0000001F) << 3) | ((c565 & 0x0000001C) >> 2);
                c[1] = ((c565 & 0x000007E0) >> 3) | ((c565 & 0x00000600) >> 9);
                c[2] = ((c565 & 0x0000F800) >> 8) | ((c565 & 0x0000E000) >> 13);
            }

            uint32_t pack8888() const {
                return ((c[0] & 0xFF) << 16) | ((c[1] & 0xFF) << 8) | (c[2] & 0xFF) | c[3];
            }

            void clearAlpha() {
                c[3] = 0;
            }

            Color operator*(int32_t factor) const {
                Color res;
                for (int32_t i = 0; i < 4; ++i) {
                    res.c[i] = c[i] * factor;
                }
                return res;
            }

            Color operator/(int32_t factor) const {
                Color res;
                for (int32_t i = 0; i < 4; ++i)
                    res.c[i] = c[i] / factor;
                return res;
            }

            Color operator>>(int32_t shift) const {
                Color res;
                for (int32_t i = 0; i < 4; ++i)
                    res.c[i] = c[i] >> shift;
                return res;
            }

            Color operator+(Color const &obj) const {
                Color res;
                for (int32_t i = 0; i < 4; ++i)
                    res.c[i] = c[i] + obj.c[i];
                return res;
            }
        private:
            int32_t c[4];
        };

        size_t getIdx(int32_t i) const {
            size_t offset = i << 1;  // 2 bytes per index
            return (idx & (0x3 << offset)) >> offset;
        }

        unsigned short c0;
        unsigned short c1;
        uint32_t idx;
    };
    static_assert(sizeof(BC_color) == 8, "BC_color must be 8 bytes");

    struct BC_channel {
        void decode(uint8_t *dst, size_t x, size_t y, size_t dstW, size_t dstH, size_t dstPitch, size_t dstBpp, size_t channel, bool isSigned) const {
            int32_t c[8] = {0};

            if (isSigned) {
                c[0] = int8_t(data & 0xFF);
                c[1] = int8_t((data & 0xFF00) >> 8);
            } else {
                c[0] = uint8_t(data & 0xFF);
                c[1] = uint8_t((data & 0xFF00) >> 8);
            }

            if (c[0] > c[1]) {
                for (int32_t i = 2; i < 8; ++i)
                    c[i] = ((8 - i) * c[0] + (i - 1) * c[1]) / 7;
            } else {
                for (int32_t i = 2; i < 6; ++i)
                    c[i] = ((6 - i) * c[0] + (i - 1) * c[1]) / 5;
                c[6] = isSigned ? -128 : 0;
                c[7] = isSigned ? 127 : 255;
            }

            for (size_t j = 0; j < BlockHeight && (y + j) < dstH; j++)
                for (size_t i = 0; i < BlockWidth && (x + i) < dstW; i++)
                    dst[channel + (i * dstBpp) + (j * dstPitch)] = uint8_t(c[getIdx((j * BlockHeight) + i)]);
        }

      private:
        uint8_t getIdx(int32_t i) const {
            int32_t offset = i * 3 + 16;
            return uint8_t((data & (0x7ull << offset)) >> offset);
        }

        uint64_t data;
    };
    static_assert(sizeof(BC_channel) == 8, "BC_channel must be 8 bytes");

    struct BC_alpha {
        void decode(uint8_t *dst, size_t x, size_t y, size_t dstW, size_t dstH, size_t dstPitch, size_t dstBpp) const {
            dst += 3;  // Write only to alpha (channel 3)
            for (size_t j = 0; j < BlockHeight && (y + j) < dstH; j++, dst += dstPitch) {
                uint8_t *dstRow = dst;
                for (size_t i = 0; i < BlockWidth && (x + i) < dstW; i++, dstRow += dstBpp)
                    *dstRow = getAlpha(j * BlockHeight + i);
            }
        }

      private:
        uint8_t getAlpha(int32_t i) const {
            int32_t offset = i << 2;
            int32_t alpha = (data & (0xFull << offset)) >> offset;
            return uint8_t(alpha | (alpha << 4));
        }

        uint64_t data;
    };
    static_assert(sizeof(BC_alpha) == 8, "BC_alpha must be 8 bytes");

    namespace BC6H {
        static constexpr int32_t MaxPartitions = 64;

        // 1.0f in half-precision floating point format
        static constexpr uint16_t halfFloat1 = 0x3C00;
        union Color {
            struct RGBA {
                uint16_t r = 0;
                uint16_t g = 0;
                uint16_t b = 0;
                uint16_t a = halfFloat1;

                RGBA(uint16_t r, uint16_t g, uint16_t b)
                    : r(r), g(g), b(b) {
                }

                RGBA &operator=(const RGBA &other) {
                    this->r = other.r;
                    this->g = other.g;
                    this->b = other.b;
                    this->a = halfFloat1;

                    return *this;
                }
            };

            Color(uint16_t r, uint16_t g, uint16_t b)
                : rgba(r, g, b) {
            }

            Color(int32_t r, int32_t g, int32_t b)
                : rgba((uint16_t) r, (uint16_t) g, (uint16_t) b) {
            }

            Color() {}

            Color(const Color &other) {
                this->rgba = other.rgba;
            }

            Color &operator=(const Color &other) {
                this->rgba = other.rgba;

                return *this;
            }

            RGBA rgba;
            uint16_t channel[4];
        };
        static_assert(sizeof(Color) == 8, "BC6h::Color must be 8 bytes long");

        inline int32_t extendSign(int32_t val, size_t size) {
            // Suppose we have a 2-bit integer being stored in 4 bit variable:
            //    x = 0b00AB
            //
            // In order to sign extend x, we need to turn the 0s into A's:
            //    x_extend = 0bAAAB
            //
            // We can do that by flipping A in x then subtracting 0b0010 from x.
            // Suppose A is 1:
            //    x       = 0b001B
            //    x_flip  = 0b000B
            //    x_minus = 0b111B
            // Since A is flipped to 0, subtracting the mask sets it and all the bits above it to 1.
            // And if A is 0:
            //    x       = 0b000B
            //    x_flip  = 0b001B
            //    x_minus = 0b000B
            // We unset the bit we flipped, and touch no other bit
            uint16_t mask = 1u << (size - 1);
            return (val ^ mask) - mask;
        }

        static int32_t constexpr RGBfChannels = 3;
        struct RGBf {
            uint16_t channel[RGBfChannels];
            size_t size[RGBfChannels];
            bool isSigned;

            RGBf() {
                static_assert(RGBfChannels == 3, "RGBf must have exactly 3 channels");
                static_assert(sizeof(channel) / sizeof(channel[0]) == RGBfChannels, "RGBf must have exactly 3 channels");
                static_assert(sizeof(channel) / sizeof(channel[0]) == sizeof(size) / sizeof(size[0]), "RGBf requires equally sized arrays for channels and channel sizes");

                for (int32_t i = 0; i < RGBfChannels; i++) {
                    channel[i] = 0;
                    size[i] = 0;
                }

                isSigned = false;
            }

            void extendSign() {
                for (int32_t i = 0; i < RGBfChannels; i++) {
                    channel[i] = BC6H::extendSign(channel[i], size[i]);
                }
            }

            // Assuming this is the delta, take the base-endpoint and transform this into
            // a proper endpoint.
            //
            // The final computed endpoint is truncated to the base-endpoint's size;
            void resolveDelta(RGBf base) {
                for (int32_t i = 0; i < RGBfChannels; i++) {
                    size[i] = base.size[i];
                    channel[i] = (base.channel[i] + channel[i]) & ((1 << base.size[i]) - 1);
                }

                // Per the spec:
                // "For signed formats, the results of the delta calculation must be sign
                // extended as well."
                if (isSigned) {
                    extendSign();
                }
            }

            void unquantize() {
                if (isSigned) {
                    unquantizeSigned();
                } else {
                    unquantizeUnsigned();
                }
            }

            void unquantizeUnsigned() {
                for (int32_t i = 0; i < RGBfChannels; i++) {
                    if (size[i] >= 15 || channel[i] == 0) {
                        continue;
                    } else if (channel[i] == ((1u << size[i]) - 1)) {
                        channel[i] = 0xFFFFu;
                    } else {
                        // Need 32 bits to avoid overflow
                        uint32_t tmp = channel[i];
                        channel[i] = (uint16_t) (((tmp << 16) + 0x8000) >> size[i]);
                    }
                    size[i] = 16;
                }
            }

            void unquantizeSigned() {
                for (int32_t i = 0; i < RGBfChannels; i++) {
                    if (size[i] >= 16 || channel[i] == 0) {
                        continue;
                    }

                    int16_t value = (int16_t)channel[i];
                    int32_t result = value;
                    bool signBit = value < 0;
                    if (signBit) {
                        value = -value;
                    }

                    if (value >= ((1 << (size[i] - 1)) - 1)) {
                        result = 0x7FFF;
                    } else {
                        // Need 32 bits to avoid overflow
                        int32_t tmp = value;
                        result = (((tmp << 15) + 0x4000) >> (size[i] - 1));
                    }

                    if (signBit) {
                        result = -result;
                    }

                    channel[i] = (uint16_t) result;
                    size[i] = 16;
                }
            }
        };

        struct Data {
            uint64_t low64;
            uint64_t high64;

            Data() = default;

            Data(uint64_t low64, uint64_t high64)
                : low64(low64), high64(high64) {
            }

            // Consumes the lowest N bits from from low64 and high64 where N is:
            //      abs(MSB - LSB)
            // MSB and LSB come from the block description of the BC6h spec and specify
            // the location of the bits in the returned bitstring.
            //
            // If MSB < LSB, then the bits are reversed. Otherwise, the bitstring is read and
            // shifted without further modification.
            //
            uint32_t consumeBits(uint32_t MSB, uint32_t LSB) {
                bool reversed = MSB < LSB;
                if (reversed) {
                    std::swap(MSB, LSB);
                }
                assert(MSB - LSB + 1 < sizeof(uint32_t) * 8);

                uint32_t num_bits = MSB - LSB + 1;
                uint32_t mask = (1 << num_bits) - 1;
                // Read the low N bits
                uint32_t bits = (low64 & mask);

                low64 >>= num_bits;
                // Put the low N bits of high64 into the high 64-N bits of low64
                low64 |= (high64 & mask) << (sizeof(high64) * 8 - num_bits);
                high64 >>= num_bits;

                if (reversed) {
                    uint32_t tmp = 0;
                    for (uint32_t numSwaps = 0; numSwaps < num_bits; numSwaps++) {
                        tmp <<= 1;
                        tmp |= (bits & 1);
                        bits >>= 1;
                    }

                    bits = tmp;
                }

                return bits << LSB;
            }
        };

        struct IndexInfo {
            uint64_t value;
            int64_t num_bits;
        };

// Interpolates between two endpoints, then does a final unquantization step
        Color interpolate(RGBf e0, RGBf e1, const IndexInfo &index, bool isSigned) {
            static constexpr uint32_t weights3[] = {0, 9, 18, 27, 37, 46, 55, 64};
            static constexpr uint32_t weights4[] = {0, 4, 9, 13, 17, 21, 26, 30,
                                                    34, 38, 43, 47, 51, 55, 60, 64};
            static constexpr uint32_t const *weightsN[] = {
                nullptr, nullptr, nullptr, weights3, weights4
            };
            auto weights = weightsN[index.num_bits];
            assert(weights != nullptr);
            Color color;
            uint32_t e0Weight = 64 - weights[index.value];
            uint32_t e1Weight = weights[index.value];

            for (int32_t i = 0; i < RGBfChannels; i++) {
                int32_t e0Channel = e0.channel[i];
                int32_t e1Channel = e1.channel[i];

                if (isSigned) {
                    e0Channel = extendSign(e0Channel, 16);
                    e1Channel = extendSign(e1Channel, 16);
                }

                int32_t e0Value = e0Channel * e0Weight;
                int32_t e1Value = e1Channel * e1Weight;

                uint32_t tmp = ((e0Value + e1Value + 32) >> 6);

                // Need to unquantize value to limit it to the legal range of half-precision
                // floats. We do this by scaling by 31/32 or 31/64 depending on if the value
                // is signed or unsigned.
                if (isSigned) {
                    tmp = ((tmp & 0x80000000) != 0) ? (((~tmp + 1) * 31) >> 5) | 0x8000 : (tmp * 31) >> 5;
                    // Don't return -0.0f, just normalize it to 0.0f.
                    if (tmp == 0x8000)
                        tmp = 0;
                } else {
                    tmp = (tmp * 31) >> 6;
                }

                color.channel[i] = (uint16_t) tmp;
            }

            return color;
        }

        enum DataType {
            // Endpoints
            EP0 = 0,
            EP1 = 1,
            EP2 = 2,
            EP3 = 3,
            Mode,
            Partition,
            End,
        };

        enum Channel {
            R = 0,
            G = 1,
            B = 2,
            None,
        };

        struct DeltaBits {
            size_t channel[3];

            constexpr DeltaBits()
                : channel{0, 0, 0} {
            }

            constexpr DeltaBits(size_t r, size_t g, size_t b)
                : channel{r, g, b} {
            }
        };

        struct ModeDesc {
            int32_t number;
            bool hasDelta;
            int32_t partitionCount;
            int32_t endpointBits;
            DeltaBits deltaBits;

            constexpr ModeDesc()
                : number(-1), hasDelta(false), partitionCount(0), endpointBits(0) {
            }

            constexpr ModeDesc(int32_t number, bool hasDelta, int32_t partitionCount, int32_t endpointBits, DeltaBits deltaBits)
                : number(number), hasDelta(hasDelta), partitionCount(partitionCount), endpointBits(endpointBits), deltaBits(deltaBits) {
            }
        };

        struct BlockDesc {
            DataType type;
            Channel channel;
            int32_t MSB;
            int32_t LSB;
            ModeDesc modeDesc;

            constexpr BlockDesc()
                : type(End), channel(None), MSB(0), LSB(0), modeDesc() {
            }

            constexpr BlockDesc(const DataType type, Channel channel, int32_t MSB, int32_t LSB, ModeDesc modeDesc)
                : type(type), channel(channel), MSB(MSB), LSB(LSB), modeDesc(modeDesc) {
            }

            constexpr BlockDesc(DataType type, Channel channel, int32_t MSB, int32_t LSB)
                : type(type), channel(channel), MSB(MSB), LSB(LSB), modeDesc() {
            }
        };

// Turns a legal mode into an index into the BlockDesc table.
// Illegal or reserved modes return -1.
        static int32_t modeToIndex(uint8_t mode) {
            if (mode <= 3) {
                return mode;
            } else if ((mode & 0x2) != 0) {
                if (mode <= 18) {
// Turns 6 into 4, 7 into 5, 10 into 6, etc.
                    return (mode / 2) + 1 + (mode & 0x1);
                } else if (mode == 22 || mode == 26 || mode == 30) {
// Turns 22 into 11, 26 into 12, etc.
                    return mode / 4 + 6;
                }
            }

            return -1;
        }

// Returns a description of the bitfields for each mode from the LSB
// to the MSB before the index data starts.
//
// The numbers come from the BC6h block description. Each BlockDesc in the
//   {Type, Channel, MSB, LSB}
//   * Type describes which endpoint this is, or if this is a mode, a partition
//     number, or the end of the block description.
//   * Channel describes one of the 3 color channels within an endpoint
//   * MSB and LSB specificy:
//      * The size of the bitfield being read
//      * The position of the bitfield within the variable it is being read to
//      * If the bitfield is stored in reverse bit order
//     If MSB < LSB then the bitfield is stored in reverse order. The size of
//     the bitfield is abs(MSB-LSB+1). And the position of the bitfield within
//     the variable is min(LSB, MSB).
//
// Invalid or reserved modes return an empty list.
        static constexpr int32_t NumBlocks = 14;
// The largest number of descriptions within a block.
        static constexpr int32_t MaxBlockDescIndex = 26;
        static constexpr BlockDesc blockDescs[NumBlocks][MaxBlockDescIndex] = {
// @fmt:off
// Mode 0, Index 0
{
{ Mode, None, 1, 0, { 0, true, 2, 10, { 5, 5, 5 } } },
{ EP2, G, 4, 4 }, { EP2, B, 4, 4 }, { EP3, B, 4, 4 },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 4, 0 }, { EP3, G, 4, 4 }, { EP2, G, 3, 0 },
{ EP1, G, 4, 0 }, { EP3, B, 0, 0 }, { EP3, G, 3, 0 },
{ EP1, B, 4, 0 }, { EP3, B, 1, 1 }, { EP2, B, 3, 0 },
{ EP2, R, 4, 0 }, { EP3, B, 2, 2 }, { EP3, R, 4, 0 },
{ EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 1, Index 1
{
{ Mode, None, 1, 0, { 1, true, 2, 7, { 6, 6, 6 } } },
{ EP2, G, 5, 5 }, { EP3, G, 5, 4 }, { EP0, R, 6, 0 },
{ EP3, B, 1, 0 }, { EP2, B, 4, 4 }, { EP0, G, 6, 0 },
{ EP2, B, 5, 5 }, { EP3, B, 2, 2 }, { EP2, G, 4, 4 },
{ EP0, B, 6, 0 }, { EP3, B, 3, 3 }, { EP3, B, 5, 5 },
{ EP3, B, 4, 4 }, { EP1, R, 5, 0 }, { EP2, G, 3, 0 },
{ EP1, G, 5, 0 }, { EP3, G, 3, 0 }, { EP1, B, 5, 0 },
{ EP2, B, 3, 0 }, { EP2, R, 5, 0 }, { EP3, R, 5, 0 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 2, Index 2
{
{ Mode, None, 4, 0, { 2, true, 2, 11, { 5, 4, 4 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 4, 0 }, { EP0, R, 10, 10 }, { EP2, G, 3, 0 },
{ EP1, G, 3, 0 }, { EP0, G, 10, 10 }, { EP3, B, 0, 0 },
{ EP3, G, 3, 0 }, { EP1, B, 3, 0 }, { EP0, B, 10, 10 },
{ EP3, B, 1, 1 }, { EP2, B, 3, 0 }, { EP2, R, 4, 0 },
{ EP3, B, 2, 2 }, { EP3, R, 4, 0 }, { EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 3, Index 3
{
{ Mode, None, 4, 0, { 3, false, 1, 10, { 0, 0, 0 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 9, 0 }, { EP1, G, 9, 0 }, { EP1, B, 9, 0 },
{ End, None, 0, 0},
},
// Mode 6, Index 4
{
{ Mode, None, 4, 0, { 6, true, 2, 11, { 4, 5, 4 } } }, // 1 1
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 3, 0 }, { EP0, R, 10, 10 }, { EP3, G, 4, 4 },
{ EP2, G, 3, 0 }, { EP1, G, 4, 0 }, { EP0, G, 10, 10 },
{ EP3, G, 3, 0 }, { EP1, B, 3, 0 }, { EP0, B, 10, 10 },
{ EP3, B, 1, 1 }, { EP2, B, 3, 0 }, { EP2, R, 3, 0 },
{ EP3, B, 0, 0 }, { EP3, B, 2, 2 }, { EP3, R, 3, 0 }, // 18 19
{ EP2, G, 4, 4 }, { EP3, B, 3, 3 }, // 2 21
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 7, Index 5
{
{ Mode, None, 4, 0, { 7, true, 1, 11, { 9, 9, 9 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 8, 0 }, { EP0, R, 10, 10 }, { EP1, G, 8, 0 },
{ EP0, G, 10, 10 }, { EP1, B, 8, 0 }, { EP0, B, 10, 10 },
{ End, None, 0, 0},
},
// Mode 10, Index 6
{
{ Mode, None, 4, 0, { 10, true, 2, 11, { 4, 4, 5 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 3, 0 }, { EP0, R, 10, 10 }, { EP2, B, 4, 4 },
{ EP2, G, 3, 0 }, { EP1, G, 3, 0 }, { EP0, G, 10, 10 },
{ EP3, B, 0, 0 }, { EP3, G, 3, 0 }, { EP1, B, 4, 0 },
{ EP0, B, 10, 10 }, { EP2, B, 3, 0 }, { EP2, R, 3, 0 },
{ EP3, B, 1, 1 }, { EP3, B, 2, 2 }, { EP3, R, 3, 0 },
{ EP3, B, 4, 4 }, { EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 11, Index 7
{
{ Mode, None, 4, 0, { 11, true, 1, 12, { 8, 8, 8 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 7, 0 }, { EP0, R, 10, 11 }, { EP1, G, 7, 0 },
{ EP0, G, 10, 11 }, { EP1, B, 7, 0 }, { EP0, B, 10, 11 },
{ End, None, 0, 0},
},
// Mode 14, Index 8
{
{ Mode, None, 4, 0, { 14, true, 2, 9, { 5, 5, 5 } } },
{ EP0, R, 8, 0 }, { EP2, B, 4, 4 }, { EP0, G, 8, 0 },
{ EP2, G, 4, 4 }, { EP0, B, 8, 0 }, { EP3, B, 4, 4 },
{ EP1, R, 4, 0 }, { EP3, G, 4, 4 }, { EP2, G, 3, 0 },
{ EP1, G, 4, 0 }, { EP3, B, 0, 0 }, { EP3, G, 3, 0 },
{ EP1, B, 4, 0 }, { EP3, B, 1, 1 }, { EP2, B, 3, 0 },
{ EP2, R, 4, 0 }, { EP3, B, 2, 2 }, { EP3, R, 4, 0 },
{ EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 15, Index 9
{
{ Mode, None, 4, 0, { 15, true, 1, 16, { 4, 4, 4 } } },
{ EP0, R, 9, 0 }, { EP0, G, 9, 0 }, { EP0, B, 9, 0 },
{ EP1, R, 3, 0 }, { EP0, R, 10, 15 }, { EP1, G, 3, 0 },
{ EP0, G, 10, 15 }, { EP1, B, 3, 0 }, { EP0, B, 10, 15 },
{ End, None, 0, 0},
},
// Mode 18, Index 10
{
{ Mode, None, 4, 0, { 18, true, 2, 8, { 6, 5, 5 } } },
{ EP0, R, 7, 0 }, { EP3, G, 4, 4 }, { EP2, B, 4, 4 },
{ EP0, G, 7, 0 }, { EP3, B, 2, 2 }, { EP2, G, 4, 4 },
{ EP0, B, 7, 0 }, { EP3, B, 3, 3 }, { EP3, B, 4, 4 },
{ EP1, R, 5, 0 }, { EP2, G, 3, 0 }, { EP1, G, 4, 0 },
{ EP3, B, 0, 0 }, { EP3, G, 3, 0 }, { EP1, B, 4, 0 },
{ EP3, B, 1, 1 }, { EP2, B, 3, 0 }, { EP2, R, 5, 0 },
{ EP3, R, 5, 0 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 22, Index 11
{
{ Mode, None, 4, 0, { 22, true, 2, 8, { 5, 6, 5 } } },
{ EP0, R, 7, 0 }, { EP3, B, 0, 0 }, { EP2, B, 4, 4 },
{ EP0, G, 7, 0 }, { EP2, G, 5, 5 }, { EP2, G, 4, 4 },
{ EP0, B, 7, 0 }, { EP3, G, 5, 5 }, { EP3, B, 4, 4 },
{ EP1, R, 4, 0 }, { EP3, G, 4, 4 }, { EP2, G, 3, 0 },
{ EP1, G, 5, 0 }, { EP3, G, 3, 0 }, { EP1, B, 4, 0 },
{ EP3, B, 1, 1 }, { EP2, B, 3, 0 }, { EP2, R, 4, 0 },
{ EP3, B, 2, 2 }, { EP3, R, 4, 0 }, { EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 26, Index 12
{
{ Mode, None, 4, 0, { 26, true, 2, 8, { 5, 5, 6 } } },
{ EP0, R, 7, 0 }, { EP3, B, 1, 1 }, { EP2, B, 4, 4 },
{ EP0, G, 7, 0 }, { EP2, B, 5, 5 }, { EP2, G, 4, 4 },
{ EP0, B, 7, 0 }, { EP3, B, 5, 5 }, { EP3, B, 4, 4 },
{ EP1, R, 4, 0 }, { EP3, G, 4, 4 }, { EP2, G, 3, 0 },
{ EP1, G, 4, 0 }, { EP3, B, 0, 0 }, { EP3, G, 3, 0 },
{ EP1, B, 5, 0 }, { EP2, B, 3, 0 }, { EP2, R, 4, 0 },
{ EP3, B, 2, 2 }, { EP3, R, 4, 0 }, { EP3, B, 3, 3 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
},
// Mode 30, Index 13
{
{ Mode, None, 4, 0, { 30, false, 2, 6, { 0, 0, 0 } } },
{ EP0, R, 5, 0 }, { EP3, G, 4, 4 }, { EP3, B, 0, 0 },
{ EP3, B, 1, 1 }, { EP2, B, 4, 4 }, { EP0, G, 5, 0 },
{ EP2, G, 5, 5 }, { EP2, B, 5, 5 }, { EP3, B, 2, 2 },
{ EP2, G, 4, 4 }, { EP0, B, 5, 0 }, { EP3, G, 5, 5 },
{ EP3, B, 3, 3 }, { EP3, B, 5, 5 }, { EP3, B, 4, 4 },
{ EP1, R, 5, 0 }, { EP2, G, 3, 0 }, { EP1, G, 5, 0 },
{ EP3, G, 3, 0 }, { EP1, B, 5, 0 }, { EP2, B, 3, 0 },
{ EP2, R, 5, 0 }, { EP3, R, 5, 0 },
{ Partition, None, 4, 0 },
{ End, None, 0, 0},
}
// @fmt:on
        };

        struct Block {
            uint64_t low64;
            uint64_t high64;

            void decode(uint8_t *dst, size_t dstX, size_t dstY, size_t dstWidth, size_t dstHeight, size_t dstPitch, size_t dstBpp, bool isSigned) const {
                // @fmt:off
                static const uint32_t p_table[MaxPartitions] = {
                    0b01010000010100000101000001010000, 0b01000000010000000100000001000000,
                    0b01010100010101000101010001010100, 0b01010100010100000101000001000000,
                    0b01010000010000000100000000000000, 0b01010101010101000101010001010000,
                    0b01010101010101000101000001000000, 0b01010100010100000100000000000000,
                    0b01010000010000000000000000000000, 0b01010101010101010101010001010000,
                    0b01010101010101000100000000000000, 0b01010100010000000000000000000000,
                    0b01010101010101010101010001000000, 0b01010101010101010000000000000000,
                    0b01010101010101010101010100000000, 0b01010101000000000000000000000000,
                    0b01010101000101010000000100000000, 0b00000000000000000100000001010100,
                    0b00010101000000010000000000000000, 0b00000000010000000101000001010100,
                    0b00000000000000000100000001010000, 0b00010101000001010000000100000000,
                    0b00000101000000010000000000000000, 0b01000000010100000101000001010100,
                    0b00000000010000000100000001010000, 0b00000101000000010000000100000000,
                    0b00010100000101000001010000010100, 0b00000101000101000001010001010000,
                    0b00000001000101010101010001000000, 0b00000000010101010101010100000000,
                    0b00010101000000010100000001010100, 0b00000101010000010100000101010000,
                    0b01000100010001000100010001000100, 0b01010101000000000101010100000000,
                    0b00010001010001000001000101000100, 0b00000101000001010101000001010000,
                    0b00000101010100000000010101010000, 0b00010001000100010100010001000100,
                    0b01000001000101000100000100010100, 0b01000100000100010001000101000100,
                    0b00010101000001010101000001010100, 0b00000001000001010101000001000000,
                    0b00000101000001000001000001010000, 0b00000101010001010101000101010000,
                    0b00010100010000010100000100010100, 0b01010000000001010000010101010000,
                    0b01000001010000010001010000010100, 0b00000000000101000001010000000000,
                    0b00000000000001000001010100000100, 0b00000000000100000101010000010000,
                    0b00010000010101000001000000000000, 0b00000100000101010000010000000000,
                    0b01010000010000010000010100010100, 0b01000001000001010001010001010000,
                    0b00000101010000010101000000010100, 0b00010100000001010100000101010000,
                    0b01000001000001010000010100010100, 0b01000001010100000101000000010100,
                    0b01000000000000010001010101010100, 0b01010100000101010000000101000000,
                    0b01010000010100000101010100000000, 0b00000000010101010101000001010000,
                    0b00010101000101010001000000010000, 0b01010100010101000000010000000100,
                };
                static const uint32_t a_table[MaxPartitions / 8] = {
                    0xffffffff, 0xffffffff, 0xf882282f, 0x22882282,
                    0xff8286ff, 0x6ff22282, 0x22ff8626, 0xf22fffff,
                };
                // @fmt:on

                Data data(low64, high64);
                assert(dstBpp == sizeof(Color));
                uint8_t mode = data.consumeBits((data.low64 & 0x2) == 0 ? 1 : 4, 0);

                int32_t blockIndex = modeToIndex(mode);
                // Handle illegal or reserved mode
                if (blockIndex == -1) {
                    for (int32_t y = 0; y < 4 && y + dstY < dstHeight; y++) {
                        for (int32_t x = 0; x < 4 && x + dstX < dstWidth; x++) {
                            auto out = reinterpret_cast<Color *>(dst + sizeof(Color) * x + dstPitch * y);
                            out->rgba = {0, 0, 0};
                        }
                    }
                    return;
                }
                const BlockDesc *blockDesc = blockDescs[blockIndex];

                RGBf e[4];
                e[0].isSigned = e[1].isSigned = e[2].isSigned = e[3].isSigned = isSigned;

                int32_t partition = 0;
                ModeDesc modeDesc;
                for (int32_t index = 0; blockDesc[index].type != End; index++) {
                    const BlockDesc desc = blockDesc[index];

                    switch (desc.type) {
                        case Mode:
                            modeDesc = desc.modeDesc;
                            assert(modeDesc.number == mode);

                            e[0].size[0] = e[0].size[1] = e[0].size[2] = modeDesc.endpointBits;
                            for (int32_t i = 0; i < RGBfChannels; i++) {
                                if (modeDesc.hasDelta) {
                                    e[1].size[i] = e[2].size[i] = e[3].size[i] = modeDesc.deltaBits.channel[i];
                                } else {
                                    e[1].size[i] = e[2].size[i] = e[3].size[i] = modeDesc.endpointBits;
                                }
                            }
                            break;
                        case Partition:
                            partition |= data.consumeBits(desc.MSB, desc.LSB);
                            break;
                        case EP0:
                        case EP1:
                        case EP2:
                        case EP3:
                            e[desc.type].channel[desc.channel] |= data.consumeBits(desc.MSB, desc.LSB);
                            break;
                        default:
                            assert(false);
                            return;
                    }
                }

                // Sign extension
                if (isSigned) {
                    for (int32_t ep = 0; ep < modeDesc.partitionCount * 2; ep++) {
                        e[ep].extendSign();
                    }
                } else if (modeDesc.hasDelta) {
                    // Don't sign-extend the base endpoint in an unsigned format.
                    for (int32_t ep = 1; ep < modeDesc.partitionCount * 2; ep++) {
                        e[ep].extendSign();
                    }
                }

                // Turn the deltas into endpoints
                if (modeDesc.hasDelta) {
                    for (int32_t ep = 1; ep < modeDesc.partitionCount * 2; ep++) {
                        e[ep].resolveDelta(e[0]);
                    }
                }

                for (int32_t ep = 0; ep < modeDesc.partitionCount * 2; ep++) {
                    e[ep].unquantize();
                }

                // Get the indices, calculate final colors, and output
                for (uint32_t y = 0; y < 4; y++) {
                    for (uint32_t x = 0; x < 4; x++) {
                        uint32_t pixelNum = x + y * 4, firstEndpoint = 0;
                        IndexInfo idx;
                        bool isAnchor = false;
                        // Bc6H can have either 1 or 2 petitions depending on the mode.
                        // The number of petitions affects the number of indices with implicit
                        // leading 0 bits and the number of bits per index.
                        if (modeDesc.partitionCount == 1) {
                            idx.num_bits = 4;
                            // There's an implicit leading 0 bit for the first idx
                            isAnchor = (pixelNum == 0);
                        } else {
                            idx.num_bits = 3;
                            // There are 2 indices with implicit leading 0-bits.
                            uint32_t anchor_value = (a_table[partition / 8] >> (partition * 4)) & 0x0f;
                            isAnchor = ((pixelNum == 0) || (pixelNum == anchor_value));
                            firstEndpoint = ((p_table[partition] >> pixelNum) & 0x03) * 2;
                        }

                        idx.value = data.consumeBits(idx.num_bits - isAnchor - 1, 0);

                        // Don't exit the loop early, we need to consume these index bits regardless if
                        // we actually output them or not.
                        if ((y + dstY >= dstHeight) || (x + dstX >= dstWidth)) {
                            continue;
                        }

                        Color color = interpolate(e[firstEndpoint], e[firstEndpoint + 1], idx, isSigned);
                        auto out = reinterpret_cast<Color *>(dst + dstBpp * x + dstPitch * y);
                        *out = color;
                    }
                }
            }
        };

    }  // namespace BC6H

    namespace BC7 {
// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_texture_compression_bptc.txt
// https://docs.microsoft.com/en-us/windows/win32/direct3d11/bc7-format

        struct Bitfield {
            uint32_t offset;
            uint32_t count;

            constexpr Bitfield Then(const uint32_t bits) { return {offset + count, bits}; }

            constexpr bool operator==(const Bitfield &rhs) {
                return offset == rhs.offset && count == rhs.count;
            }
        };

        struct Mode {
            const int32_t IDX;   // Mode index
            const uint32_t NS;   // Number of subsets in each partition
            const uint32_t PB;   // Partition bits
            const uint32_t RB;   // Rotation bits
            const uint32_t ISB;  // Index selection bits
            const uint32_t CB;   // Color bits
            const uint32_t AB;   // Alpha bits
            const uint32_t EPB;  // Endpoint P-bits
            const uint32_t SPB;  // Shared P-bits
            const uint32_t IB;   // Primary index bits per element
            const uint32_t IBC;  // Primary index bits total
            const uint32_t IB2;  // Secondary index bits per element

            constexpr uint32_t NumColors() const { return NS * 2; }

            constexpr Bitfield Partition() const { return {uint32_t(IDX) + 1, PB}; }

            constexpr Bitfield Rotation() const { return Partition().Then(RB); }

            constexpr Bitfield IndexSelection() const { return Rotation().Then(ISB); }

            constexpr Bitfield Red(uint32_t idx) const {
                return IndexSelection().Then(CB * idx).Then(CB);
            }

            constexpr Bitfield Green(uint32_t idx) const {
                return Red(NumColors() - 1).Then(CB * idx).Then(CB);
            }

            constexpr Bitfield Blue(uint32_t idx) const {
                return Green(NumColors() - 1).Then(CB * idx).Then(CB);
            }

            constexpr Bitfield Alpha(uint32_t idx) const {
                return Blue(NumColors() - 1).Then(AB * idx).Then(AB);
            }

            constexpr Bitfield EndpointPBit(uint32_t idx) const {
                return Alpha(NumColors() - 1).Then(EPB * idx).Then(EPB);
            }

            constexpr Bitfield SharedPBit0() const {
                return EndpointPBit(NumColors() - 1).Then(SPB);
            }

            constexpr Bitfield SharedPBit1() const {
                return SharedPBit0().Then(SPB);
            }

            constexpr Bitfield PrimaryIndex(uint32_t offset, uint32_t count) const {
                return SharedPBit1().Then(offset).Then(count);
            }

            constexpr Bitfield SecondaryIndex(uint32_t offset, uint32_t count) const {
                return SharedPBit1().Then(IBC + offset).Then(count);
            }
        };

        struct Color {
            struct RGB {
                inline RGB() = default;
                inline RGB(uint8_t r, uint8_t g, uint8_t b) : b(b), g(g), r(r) {}
                inline RGB operator<<(uint32_t shift) const noexcept {
                    return {uint8_t(r << shift), uint8_t(g << shift), uint8_t(b << shift)};
                }
                inline RGB operator>>(uint32_t shift) const noexcept {
                    return {uint8_t(r >> shift), uint8_t(g >> shift), uint8_t(b >> shift)};
                }
                inline RGB operator|(uint32_t bits) const noexcept {
                    return {uint8_t(r | bits), uint8_t(g | bits), uint8_t(b | bits)};
                }
                inline RGB operator|(RGB const& rhs) const noexcept {
                    return {uint8_t(r | rhs.r), uint8_t(g | rhs.g), uint8_t(b | rhs.b)};
                }
                inline RGB operator+(RGB const& rhs) const noexcept {
                    return {uint8_t(r + rhs.r), uint8_t(g + rhs.g), uint8_t(b + rhs.b)};
                }
                uint8_t b;
                uint8_t g;
                uint8_t r;
            };
            RGB rgb;
            uint8_t a;
        };
        static_assert(sizeof(Color) == 4, "Color size must be 4 bytes");

        struct Block {
            constexpr uint64_t Get(const Bitfield &bf) const {
                uint64_t mask = (1ULL << bf.count) - 1;
                if (bf.offset + bf.count <= 64)
                    return (low >> bf.offset) & mask;
                if (bf.offset >= 64)
                    return (high >> (bf.offset - 64)) & mask;
                return ((low >> bf.offset) | (high << (64 - bf.offset))) & mask;
            }

            Mode const& mode() const {
                static const Mode m_table[8 + 1] = {
                    //     IDX  NS   PB   RB   ISB  CB   AB   EPB  SPB  IB   IBC, IB2
                    /**/ {0x0, 0x3, 0x4, 0x0, 0x0, 0x4, 0x0, 0x1, 0x0, 0x3, 0x2d, 0x0},
                    /**/ {0x1, 0x2, 0x6, 0x0, 0x0, 0x6, 0x0, 0x0, 0x1, 0x3, 0x2e, 0x0},
                    /**/ {0x2, 0x3, 0x6, 0x0, 0x0, 0x5, 0x0, 0x0, 0x0, 0x2, 0x1d, 0x0},
                    /**/ {0x3, 0x2, 0x6, 0x0, 0x0, 0x7, 0x0, 0x1, 0x0, 0x2, 0x1e, 0x0},
                    /**/ {0x4, 0x1, 0x0, 0x2, 0x1, 0x5, 0x6, 0x0, 0x0, 0x2, 0x1f, 0x3},
                    /**/ {0x5, 0x1, 0x0, 0x2, 0x0, 0x7, 0x8, 0x0, 0x0, 0x2, 0x1f, 0x2},
                    /**/ {0x6, 0x1, 0x0, 0x0, 0x0, 0x7, 0x7, 0x1, 0x0, 0x4, 0x3f, 0x0},
                    /**/ {0x7, 0x2, 0x6, 0x0, 0x0, 0x5, 0x5, 0x1, 0x0, 0x2, 0x1e, 0x0},
                    /**/ {-1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x00, 0x0},
                };
                // Fun historical fact: this is basically ffs(), however, windows does NOT have ffs()
                // This is because ffs() comes from VAX which had an instruction for ffs(), but
                // to the surprise of absolutely nobody, VAX ended up dying, alongside ffs()... or so
                // I tought. It turns out only *NIX world kept ffs() while windows, not bound by POSIX
                // standards, just dropped it altogether. Even through Windows had a VAX port at once time
                // there isn't much one can do other than scream.
                if ((low & 0b00000001) != 0) return m_table[0];
                else if ((low & 0b00000010) != 0) return m_table[1];
                else if ((low & 0b00000100) != 0) return m_table[2];
                else if ((low & 0b00001000) != 0) return m_table[3];
                else if ((low & 0b00010000) != 0) return m_table[4];
                else if ((low & 0b00100000) != 0) return m_table[5];
                else if ((low & 0b01000000) != 0) return m_table[6];
                else if ((low & 0b10000000) != 0) return m_table[7];
                return m_table[8]; //invalid but pretend it's fine
            }

            struct IndexInfo {
                uint64_t value;
                uint32_t num_bits;
            };

            uint8_t interpolate(uint8_t e0, uint8_t e1, const IndexInfo &index) const {
                static constexpr uint16_t weights2[] = {0, 21, 43, 64};
                static constexpr uint16_t weights3[] = {0, 9, 18, 27, 37, 46, 55, 64};
                static constexpr uint16_t weights4[] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
                static constexpr uint16_t const *weightsN[] = {
                    nullptr, nullptr, weights2, weights3, weights4
                };
                auto weights = weightsN[index.num_bits];
                assert(weights != nullptr);
                return (uint8_t) (((64 - weights[index.value]) * uint16_t(e0) + weights[index.value] * uint16_t(e1) + 32) >> 6);
            }

            void decode(uint8_t *dst, size_t dstX, size_t dstY, size_t dstWidth, size_t dstHeight, size_t dstPitch) const {
                assert(low <= 0b11111111);
                auto const& mode = this->mode();
                if (mode.IDX < 0)  {
                    for (size_t y = 0; y < 4 && y + dstY < dstHeight; y++) {
                        for (size_t x = 0; x < 4 && x + dstX < dstWidth; x++) {
                            auto out = reinterpret_cast<Color *>(dst + sizeof(Color) * x + dstPitch * y);
                            out->rgb = {0, 0, 0};
                            out->a = 0;
                        }
                    }
                    return;
                }

                std::array<std::array<Color, 2>, 3> subsets;
                for (size_t i = 0; i < mode.NS; i++) {
                    auto& subset = subsets[i];
                    subset[0].rgb.r = Get(mode.Red(i * 2 + 0));
                    subset[0].rgb.g = Get(mode.Green(i * 2 + 0));
                    subset[0].rgb.b = Get(mode.Blue(i * 2 + 0));
                    subset[0].a = (mode.AB > 0) ? Get(mode.Alpha(i * 2 + 0)) : 255;

                    subset[1].rgb.r = Get(mode.Red(i * 2 + 1));
                    subset[1].rgb.g = Get(mode.Green(i * 2 + 1));
                    subset[1].rgb.b = Get(mode.Blue(i * 2 + 1));
                    subset[1].a = (mode.AB > 0) ? Get(mode.Alpha(i * 2 + 1)) : 255;
                }

                if (mode.SPB > 0) {
                    auto pbit0 = Get(mode.SharedPBit0());
                    auto pbit1 = Get(mode.SharedPBit1());
                    subsets[0][0].rgb = (subsets[0][0].rgb << 1) | pbit0;
                    subsets[0][1].rgb = (subsets[0][1].rgb << 1) | pbit0;
                    subsets[1][0].rgb = (subsets[1][0].rgb << 1) | pbit1;
                    subsets[1][1].rgb = (subsets[1][1].rgb << 1) | pbit1;
                }

                if (mode.EPB > 0) {
                    for (size_t i = 0; i < mode.NS; i++) {
                        auto &subset = subsets[i];
                        auto pbit0 = Get(mode.EndpointPBit(i * 2 + 0));
                        auto pbit1 = Get(mode.EndpointPBit(i * 2 + 1));
                        subset[0].rgb = (subset[0].rgb << 1) | pbit0;
                        subset[1].rgb = (subset[1].rgb << 1) | pbit1;
                        if (mode.AB > 0) {
                            subset[0].a = (subset[0].a << 1) | pbit0;
                            subset[1].a = (subset[1].a << 1) | pbit1;
                        }
                    }
                }

                auto const colorBits = mode.CB + mode.SPB + mode.EPB;
                auto const alphaBits = mode.AB + mode.SPB + mode.EPB;

                for (size_t i = 0; i < mode.NS; i++) {
                    auto &subset = subsets[i];
                    subset[0].rgb = subset[0].rgb << (8 - colorBits);
                    subset[1].rgb = subset[1].rgb << (8 - colorBits);
                    subset[0].rgb = subset[0].rgb | (subset[0].rgb >> colorBits);
                    subset[1].rgb = subset[1].rgb | (subset[1].rgb >> colorBits);

                    if (mode.AB > 0) {
                        subset[0].a = subset[0].a << (8 - alphaBits);
                        subset[1].a = subset[1].a << (8 - alphaBits);
                        subset[0].a = subset[0].a | (subset[0].a >> alphaBits);
                        subset[1].a = subset[1].a | (subset[1].a >> alphaBits);
                    }
                }

                int32_t colorIndexBitOffset = 0, alphaIndexBitOffset = 0;
                for (int32_t y = 0; y < 4; y++) {
                    for (int32_t x = 0; x < 4; x++) {
                        auto const texelIdx = y * 4 + x;
                        auto const partitionIdx = Get(mode.Partition());
                        assert(partitionIdx < MaxPartitions);
                        auto const subsetIdx = subsetIndex(mode.NS, partitionIdx, texelIdx);
                        assert(subsetIdx < 3);
                        auto const& subset = subsets[subsetIdx];
                        auto const anchorIdx = anchorIndex(mode.NS, partitionIdx, subsetIdx);
                        auto const isAnchor = anchorIdx == texelIdx;
                        auto const colorIdx = colorIndex(mode, isAnchor, colorIndexBitOffset);
                        auto const alphaIdx = alphaIndex(mode, isAnchor, alphaIndexBitOffset);
                        if (y + dstY >= dstHeight || x + dstX >= dstWidth) {
                            // Don't be tempted to skip early at the loops:
                            // The calls to colorIndex() and alphaIndex() adjust bit
                            // offsets that need to be carefully tracked.
                            continue;
                        }
                        // Note: We flip r and b channels past this point as the texture storage is BGR while the output is RGB
                        uint8_t output[4] = {
                            interpolate(subset[0].rgb.b, subset[1].rgb.b, colorIdx),
                            interpolate(subset[0].rgb.g, subset[1].rgb.g, colorIdx),
                            interpolate(subset[0].rgb.r, subset[1].rgb.r, colorIdx),
                            interpolate(subset[0].a, subset[1].a, alphaIdx)
                        };
                        std::swap(output[3], output[3 - Get(mode.Rotation())]);
                        reinterpret_cast<Color*>(dst + dstPitch * y)[x] = *reinterpret_cast<Color const*>(&output);
                    }
                }
            }

            uint32_t subsetIndex(uint32_t ns, uint32_t p_index, uint32_t t_index) const {
                // ns := either 0,1,2, p_index %= 64, t_index %= 16
                // pad the bits out so we have an homogenous operation
                alignas(64) static const uint32_t p_table[2][64] = {
                    { // before: 64*16, after: 64*2
                        0b01010000010100000101000001010000, 0b01000000010000000100000001000000,
                        0b01010100010101000101010001010100, 0b01010100010100000101000001000000,
                        0b01010000010000000100000000000000, 0b01010101010101000101010001010000,
                        0b01010101010101000101000001000000, 0b01010100010100000100000000000000,
                        0b01010000010000000000000000000000, 0b01010101010101010101010001010000,
                        0b01010101010101000100000000000000, 0b01010100010000000000000000000000,
                        0b01010101010101010101010001000000, 0b01010101010101010000000000000000,
                        0b01010101010101010101010100000000, 0b01010101000000000000000000000000,
                        0b01010101000101010000000100000000, 0b00000000000000000100000001010100,
                        0b00010101000000010000000000000000, 0b00000000010000000101000001010100,
                        0b00000000000000000100000001010000, 0b00010101000001010000000100000000,
                        0b00000101000000010000000000000000, 0b01000000010100000101000001010100,
                        0b00000000010000000100000001010000, 0b00000101000000010000000100000000,
                        0b00010100000101000001010000010100, 0b00000101000101000001010001010000,
                        0b00000001000101010101010001000000, 0b00000000010101010101010100000000,
                        0b00010101000000010100000001010100, 0b00000101010000010100000101010000,
                        0b01000100010001000100010001000100, 0b01010101000000000101010100000000,
                        0b00010001010001000001000101000100, 0b00000101000001010101000001010000,
                        0b00000101010100000000010101010000, 0b00010001000100010100010001000100,
                        0b01000001000101000100000100010100, 0b01000100000100010001000101000100,
                        0b00010101000001010101000001010100, 0b00000001000001010101000001000000,
                        0b00000101000001000001000001010000, 0b00000101010001010101000101010000,
                        0b00010100010000010100000100010100, 0b01010000000001010000010101010000,
                        0b01000001010000010001010000010100, 0b00000000000101000001010000000000,
                        0b00000000000001000001010100000100, 0b00000000000100000101010000010000,
                        0b00010000010101000001000000000000, 0b00000100000101010000010000000000,
                        0b01010000010000010000010100010100, 0b01000001000001010001010001010000,
                        0b00000101010000010101000000010100, 0b00010100000001010100000101010000,
                        0b01000001000001010000010100010100, 0b01000001010100000101000000010100,
                        0b01000000000000010001010101010100, 0b01010100000101010000000101000000,
                        0b01010000010100000101010100000000, 0b00000000010101010101000001010000,
                        0b00010101000101010001000000010000, 0b01010100010101000000010000000100,
                    }, { // before: 64*16, after: 64*4
                        0b10101010011010000101000001010000, 0b01101010010110100101000001000000,
                        0b01011010010110100100001000000000, 0b01010100010100001010000010101000,
                        0b10100101101001010000000000000000, 0b10100000101000000101000001010000,
                        0b01010101010101011010000010100000, 0b01011010010110100101000001010000,
                        0b10101010010101010000000000000000, 0b10101010010101010101010100000000,
                        0b10101010101010100101010100000000, 0b10010000100100001001000010010000,
                        0b10010100100101001001010010010100, 0b10100100101001001010010010100100,
                        0b10101001101001011001010001010000, 0b00101010000010100100001001010000,
                        0b10100101100101000101000001000000, 0b00001010010000100101000001010100,
                        0b10100101101001011010010100000000, 0b01010101101000001010000010100000,
                        0b10101000101010000101010001010100, 0b01101010011010100100000001000000,
                        0b10100100101001000101000000000000, 0b00011010000110100000010100000000,
                        0b00000000010100001010010010100100, 0b10101010101001011001000010010000,
                        0b00010100011010010110100100010100, 0b01101001011010010001010000000000,
                        0b10100000100001011000010110100000, 0b10101010100000100001010000010100,
                        0b01010000101001001010010001010000, 0b01101010010110100000001000000000,
                        0b10101001101001011000000000000000, 0b01010000100100001010000010101000,
                        0b10101000101000001001000001010000, 0b00100100001001000010010000100100,
                        0b00000000101010100101010100000000, 0b00100100100100100100100100100100,
                        0b00100100010010011001001000100100, 0b01010000101001010000101001010000,
                        0b01010000000010101010010101010000, 0b10101010101010100100010001000100,
                        0b01100110011001100000000000000000, 0b10100101101000001010010110100000,
                        0b01010000101000000101000010100000, 0b01101001001010000110100100101000,
                        0b01000100101010101010101001000100, 0b01100110011001100110011000000000,
                        0b10101010010001000100010001000100, 0b01010100101010000101010010101000,
                        0b10010101100000001001010110000000, 0b10010110100101101001011000000000,
                        0b10101000010101000101010010101000, 0b10000000100101011001010110000000,
                        0b10101010000101000001010000010100, 0b10010110100101100000000000000000,
                        0b10101010101010100001010000010100, 0b10100000010100000101000010100000,
                        0b10100000101001011010010110100000, 0b10010110000000000000000000000000,
                        0b01000000100000000100000010000000, 0b10101001101010001010100110101000,
                        0b10101010101010101010101001000100, 0b00101010010010100101001001010100,
                    }
                };
                uint32_t const mask = (0x03010000 >> (ns * 8)) & 0xff;
                return (p_table[ns & 0x01][p_index] >> (t_index << 1)) & mask;
            }

            uint32_t anchorIndex(uint32_t ns, uint32_t p_index, uint32_t s_index)const {
                // ARB_texture_compression_bptc states: "In partition zero, the anchor index is always index zero.
                // In other partitions, the anchor index is specified by tables
                // Table.A2 and Table.A3.""
                // Note: This is really confusing - I believe they meant subset instead of partition here.
                // s_index >= 0 && s_index <= 2
                alignas(64) static const uint32_t a_table[3][64 / 8] = {
                    { 0xffffffff, 0xffffffff, 0xf882282f, 0x22882282, 0xff8286ff, 0x6ff22282, 0x22ff8626, 0xf22fffff },
                    { 0xff38ff33, 0x33566688, 0xa633f833, 0xff586885, 0xf8a653f8, 0xffff5f3f, 0xa58555f3, 0x33cfd8a5 },
                    { 0x83ff388f, 0x8fffffff, 0x8f8f3f8f, 0x8affa6f3, 0xa98aaf3f, 0x8663f8f6, 0xffffff3f, 0x8ff3ffff },
                };
                uint64_t const g0 = (a_table[0][p_index / 8] >> (p_index * 4)) & 0x0f; // reading all faster because ternary logic is good
                uint64_t const g1 = (a_table[1][p_index / 8] >> (p_index * 4)) & 0x0f;
                uint64_t const g2 = (a_table[2][p_index / 8] >> (p_index * 4)) & 0x0f;
                uint64_t const lookup_table = 0x0000
                    | ((g1 << 16) | (g1 << 20) | (g0 << 24) | (g1 << 28))
                    | ((g2 << 32) | (g2 << 36) | (g2 << 40) | (g2 << 44));
                return (lookup_table >> (((s_index * 4) + ns) * 4)) & 0x0f;
            }

            IndexInfo colorIndex(const Mode &mode, bool isAnchor, int32_t &indexBitOffset) const {
                // ARB_texture_compression_bptc states:
                // "The index value for interpolating color comes from the secondary
                // index for the texel if the format has an index selection bit and its
                // value is one and from the primary index otherwise.""
                auto idx = Get(mode.IndexSelection());
                assert(idx <= 1);
                bool secondary = idx == 1;
                auto num_bits = secondary ? mode.IB2 : mode.IB;
                auto numReadBits = num_bits - (isAnchor ? 1 : 0);
                auto index = Get(secondary ? mode.SecondaryIndex(indexBitOffset, numReadBits) : mode.PrimaryIndex(indexBitOffset, numReadBits));
                indexBitOffset += numReadBits;
                return {index, num_bits};
            }

            IndexInfo alphaIndex(const Mode &mode, bool isAnchor, int32_t &indexBitOffset) const {
                // ARB_texture_compression_bptc states:
                // "The alpha index comes from the secondary index if the block has a
                // secondary index and the block either doesn't have an index selection
                // bit or that bit is zero and the primary index otherwise."
                auto idx = Get(mode.IndexSelection());
                assert(idx <= 1);
                bool secondary = (mode.IB2 != 0) && (idx == 0);
                auto num_bits = secondary ? mode.IB2 : mode.IB;
                auto numReadBits = num_bits - (isAnchor ? 1 : 0);
                auto index = Get(secondary ? mode.SecondaryIndex(indexBitOffset, numReadBits) : mode.PrimaryIndex(indexBitOffset, numReadBits));
                indexBitOffset += numReadBits;
                return {index, num_bits};
            }

            // Assumes little-endian
            uint64_t low;
            uint64_t high;
        };

    }  // namespace BC7
}  // anonymous namespace

namespace bcn {
    constexpr size_t R8Bpp{1}; //!< The amount of bytes per pixel in R8
    constexpr size_t R8g8Bpp{2}; //!< The amount of bytes per pixel in R8G8
    constexpr size_t R8g8b8a8Bpp{4}; //!< The amount of bytes per pixel in R8G8B8A8
    constexpr size_t R16g16b16a16Bpp{8}; //!< The amount of bytes per pixel in R16G16B16

    void DecodeBc1(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *color{reinterpret_cast<const BC_color *>(src)};
        size_t pitch{R8g8b8a8Bpp * width};
        color->decode(dst, x, y, width, height, pitch, R8g8b8a8Bpp, true, false);
    }

    void DecodeBc2(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *alpha{reinterpret_cast<const BC_alpha *>(src)};
        const auto *color{reinterpret_cast<const BC_color *>(src + 8)};
        size_t pitch{R8g8b8a8Bpp * width};
        color->decode(dst, x, y, width, height, pitch, R8g8b8a8Bpp, false, true);
        alpha->decode(dst, x, y, width, height, pitch, R8g8b8a8Bpp);
    }

    void DecodeBc3(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *alpha{reinterpret_cast<const BC_channel *>(src)};
        const auto *color{reinterpret_cast<const BC_color *>(src + 8)};
        size_t pitch{R8g8b8a8Bpp * width};
        color->decode(dst, x, y, width, height, pitch, R8g8b8a8Bpp, false, true);
        alpha->decode(dst, x, y, width, height, pitch, R8g8b8a8Bpp, 3, false);
    }

    void DecodeBc4(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *red{reinterpret_cast<const BC_channel *>(src)};
        size_t pitch{R8Bpp * width};
        red->decode(dst, x, y, width, height, pitch, R8Bpp, 0, isSigned);
    }

    void DecodeBc5(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *red{reinterpret_cast<const BC_channel *>(src)};
        const auto *green{reinterpret_cast<const BC_channel *>(src + 8)};
        size_t pitch{R8g8Bpp * width};
        red->decode(dst, x, y, width, height, pitch, R8g8Bpp, 0, isSigned);
        green->decode(dst, x, y, width, height, pitch, R8g8Bpp, 1, isSigned);
    }

    void DecodeBc6(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *block{reinterpret_cast<const BC6H::Block *>(src)};
        size_t pitch{R16g16b16a16Bpp * width};
        block->decode(dst, x, y, width, height, pitch, R16g16b16a16Bpp, isSigned);
    }

    void DecodeBc7(const uint8_t *src, uint8_t *dst, size_t x, size_t y, size_t width, size_t height, bool isSigned) {
        const auto *block{reinterpret_cast<const BC7::Block *>(src)};
        size_t pitch{R8g8b8a8Bpp * width};
        block->decode(dst, x, y, width, height, pitch);
    }
}
