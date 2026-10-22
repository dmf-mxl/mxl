// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! MXL `video/v210a` grains vs GStreamer `A422_10LE` frames.
//!
//! A `video/v210a` grain holds a v210 fill followed by a key plane, as described in
//! `docs/Architecture.md`. GStreamer has no v210 variant with alpha, so `mxlsrc` and
//! `mxlsink` exchange these grains as `A422_10LE`: planar 4:2:2 Y, U, V and A, each
//! 10-bit sample in the low bits of a little-endian 16-bit word.
//!
//! Fill samples are copied unchanged in both directions. Key samples change range:
//! `docs/Architecture.md` gives the key the same range as the fill. A v210 fill may
//! be narrow or full range, but an MXL flow definition cannot yet say which, so the
//! fill is taken to be narrow range, the SMPTE ST 2110-20 default, and the key runs
//! from 64 (transparent) to 940 (opaque). GStreamer reads alpha as full range, 0 to
//! 1023, whatever the colorimetry. Each of the 877 key values maps to its own alpha
//! value, so a grain converted to a frame and back is identical to the original
//! apart from padding bytes, which are written as zero. A frame converted to a grain
//! keeps 877 of its 1024 alpha levels.

const KEY_TRANSPARENT: u32 = 64;
const KEY_OPAQUE: u32 = 940;
const ALPHA_OPAQUE: u32 = 1023;

/// GStreamer alpha for each 10-bit MXL key sample. Values outside the video range
/// are clamped to it first.
const GST_ALPHA_FROM_KEY: [u16; 1024] = {
    let mut table = [0u16; 1024];
    let mut key = 0;
    while key < 1024 {
        let k = if key < KEY_TRANSPARENT {
            KEY_TRANSPARENT
        } else if key > KEY_OPAQUE {
            KEY_OPAQUE
        } else {
            key
        };
        let span = KEY_OPAQUE - KEY_TRANSPARENT;
        table[key as usize] = (((k - KEY_TRANSPARENT) * ALPHA_OPAQUE + span / 2) / span) as u16;
        key += 1;
    }
    table
};

/// MXL key sample for each 10-bit GStreamer alpha value.
const MXL_KEY_FROM_ALPHA: [u16; 1024] = {
    let mut table = [0u16; 1024];
    let mut alpha = 0;
    while alpha < 1024 {
        let span = KEY_OPAQUE - KEY_TRANSPARENT;
        table[alpha as usize] =
            (KEY_TRANSPARENT + (alpha * span + ALPHA_OPAQUE / 2) / ALPHA_OPAQUE) as u16;
        alpha += 1;
    }
    table
};

/// GStreamer alpha (full range) for an MXL key sample (video range).
pub fn gst_alpha_from_mxl_key(key: u16) -> u16 {
    GST_ALPHA_FROM_KEY[usize::from(key & SAMPLE_MASK as u16)]
}

/// MXL key sample (video range) for a GStreamer alpha value (full range).
pub fn mxl_key_from_gst_alpha(alpha: u16) -> u16 {
    MXL_KEY_FROM_ALPHA[usize::from(alpha & SAMPLE_MASK as u16)]
}

pub const V210_MEDIA_TYPE: &str = "video/v210";
pub const V210A_MEDIA_TYPE: &str = "video/v210a";
pub const GST_V210: &str = "v210";
pub const GST_A422_10LE: &str = "A422_10LE";

pub fn media_type_for_gst_format(format: &str) -> Option<&'static str> {
    match format {
        GST_V210 => Some(V210_MEDIA_TYPE),
        GST_A422_10LE => Some(V210A_MEDIA_TYPE),
        _ => None,
    }
}

/// Pixels in one v210 group of four 32-bit words.
const V210_GROUP_PIXELS: usize = 6;
const V210_GROUP_BYTES: usize = 16;
const ALPHA_SAMPLES_PER_WORD: usize = 3;
const SAMPLE_MASK: u32 = 0x3ff;
/// Plane names in GStreamer's `A422_10LE` order, for error messages.
const PLANE_NAMES: [&str; 4] = ["Y plane", "U plane", "V plane", "A plane"];

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum VideoLayoutError {
    #[error("{what} has {actual} bytes, at least {expected} are needed")]
    TooShort {
        what: &'static str,
        actual: usize,
        expected: usize,
    },
    #[error("a {width}x{height} frame does not fit in memory")]
    TooLarge { width: usize, height: usize },
}

/// Bytes in one line of a v210 picture, including the padding to 128 bytes.
pub fn v210_line_length(width: usize) -> usize {
    width.div_ceil(48) * 128
}

/// Bytes in one line of a `video/v210a` key plane, including the padding to 4 bytes.
pub fn alpha_line_length(width: usize) -> usize {
    width.div_ceil(ALPHA_SAMPLES_PER_WORD) * 4
}

/// Bytes in a `video/v210a` grain of `lines` lines, or `None` on overflow.
pub fn v210a_grain_size(width: usize, lines: usize) -> Option<usize> {
    v210_line_length(width)
        .checked_add(alpha_line_length(width))?
        .checked_mul(lines)
}

#[derive(Clone, Copy)]
pub struct Plane<'a> {
    pub data: &'a [u8],
    pub stride: usize,
}

pub struct PlaneMut<'a> {
    pub data: &'a mut [u8],
    pub stride: usize,
}

/// Copies a `video/v210a` grain into the Y, U, V and A planes of an `A422_10LE` frame.
pub fn gst_a422_10le_from_mxl_v210a_grain(
    grain: &[u8],
    width: usize,
    height: usize,
    planes: &mut [PlaneMut<'_>; 4],
) -> Result<(), VideoLayoutError> {
    let fill_line = v210_line_length(width);
    let key_line = alpha_line_length(width);
    check_grain(grain.len(), width, height)?;
    check_planes(
        planes.each_ref().map(|p| (p.data.len(), p.stride)),
        width,
        height,
    )?;
    let (fill, key) = grain.split_at(fill_line * height);
    let [y, u, v, a] = planes;
    let cw = chroma_width(width);

    for line in 0..height {
        unpack_v210_line(
            &fill[line * fill_line..][..fill_line],
            width,
            &mut y.data[line * y.stride..][..width * 2],
            &mut u.data[line * u.stride..][..cw * 2],
            &mut v.data[line * v.stride..][..cw * 2],
        );
        unpack_alpha_line(
            &key[line * key_line..][..key_line],
            width,
            &mut a.data[line * a.stride..][..width * 2],
        );
    }
    Ok(())
}

/// Copies the Y, U, V and A planes of an `A422_10LE` frame into a `video/v210a` grain.
pub fn mxl_v210a_grain_from_gst_a422_10le(
    planes: &[Plane<'_>; 4],
    width: usize,
    height: usize,
    grain: &mut [u8],
) -> Result<(), VideoLayoutError> {
    let fill_line = v210_line_length(width);
    let key_line = alpha_line_length(width);
    check_grain(grain.len(), width, height)?;
    check_planes(planes.map(|p| (p.data.len(), p.stride)), width, height)?;
    let (fill, key) = grain.split_at_mut(fill_line * height);
    let [y, u, v, a] = planes;
    let cw = chroma_width(width);

    let mut scratch = vec![0u16; 2 * width + 2 * cw];
    let (y16, rest) = scratch.split_at_mut(width);
    let (u16s, rest) = rest.split_at_mut(cw);
    let (v16, a16) = rest.split_at_mut(cw);

    for line in 0..height {
        decode_samples(&y.data[line * y.stride..][..width * 2], y16);
        decode_samples(&u.data[line * u.stride..][..cw * 2], u16s);
        decode_samples(&v.data[line * v.stride..][..cw * 2], v16);
        decode_samples(&a.data[line * a.stride..][..width * 2], a16);
        pack_v210_line(
            y16,
            u16s,
            v16,
            width,
            &mut fill[line * fill_line..][..fill_line],
        );
        pack_alpha_line(a16, width, &mut key[line * key_line..][..key_line]);
    }
    Ok(())
}

/// Reads little-endian 16-bit samples from `src` into `dst`, keeping the low 10 bits.
fn decode_samples(src: &[u8], dst: &mut [u16]) {
    let mask = SAMPLE_MASK as u16;
    for (out, bytes) in dst.chunks_exact_mut(8).zip(src.chunks_exact(16)) {
        let (Ok(out), Ok(bytes)) = (<&mut [u16; 8]>::try_from(out), <&[u8; 16]>::try_from(bytes))
        else {
            continue;
        };
        for (i, sample) in out.iter_mut().enumerate() {
            *sample = u16::from_le_bytes([bytes[2 * i], bytes[2 * i + 1]]) & mask;
        }
    }
    let done = dst.len() / 8 * 8;
    let tail = dst.iter_mut().zip(src.chunks_exact(2)).skip(done);
    for (sample, b) in tail {
        *sample = u16::from_le_bytes([b[0], b[1]]) & mask;
    }
}

fn chroma_width(width: usize) -> usize {
    width.div_ceil(2)
}

fn too_short(what: &'static str, actual: usize, expected: usize) -> VideoLayoutError {
    VideoLayoutError::TooShort {
        what,
        actual,
        expected,
    }
}

fn check_grain(len: usize, width: usize, height: usize) -> Result<(), VideoLayoutError> {
    let expected =
        v210a_grain_size(width, height).ok_or(VideoLayoutError::TooLarge { width, height })?;
    if len < expected {
        return Err(too_short("v210a grain", len, expected));
    }
    Ok(())
}

/// Checks that each plane, given as its length and stride, holds `height` lines of
/// samples. The last line only needs to be as wide as its samples.
fn check_planes(
    planes: [(usize, usize); 4],
    width: usize,
    height: usize,
) -> Result<(), VideoLayoutError> {
    let samples = [width, chroma_width(width), chroma_width(width), width];
    for (i, (len, stride)) in planes.into_iter().enumerate() {
        let line_bytes = samples[i] * 2;
        if stride < line_bytes {
            return Err(too_short(PLANE_NAMES[i], stride, line_bytes));
        }
        let needed = match height {
            0 => 0,
            h => stride
                .checked_mul(h - 1)
                .and_then(|n| n.checked_add(line_bytes))
                .ok_or(VideoLayoutError::TooLarge { width, height })?,
        };
        if len < needed {
            return Err(too_short(PLANE_NAMES[i], len, needed));
        }
    }
    Ok(())
}

fn read_word(src: &[u8], word: usize) -> u32 {
    let b = &src[word * 4..][..4];
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
}

fn write_word(dst: &mut [u8], word: usize, value: u32) {
    dst[word * 4..][..4].copy_from_slice(&value.to_le_bytes());
}

fn write_sample(dst: &mut [u8], index: usize, value: u32) {
    let sample = (value & SAMPLE_MASK) as u16;
    dst[index * 2..][..2].copy_from_slice(&sample.to_le_bytes());
}

/// The v210 sample order along a line is Cb Y Cr Y, repeated for each pair of
/// pixels, packed three samples to a 32-bit word. Sample `n` of that sequence is
/// the chroma or luma sample this returns, with its index in its own plane.
enum V210Sample {
    Cb(usize),
    Y(usize),
    Cr(usize),
}

fn v210_sample(n: usize) -> V210Sample {
    let pair = n / 4;
    match n % 4 {
        0 => V210Sample::Cb(pair),
        1 => V210Sample::Y(pair * 2),
        2 => V210Sample::Cr(pair),
        _ => V210Sample::Y(pair * 2 + 1),
    }
}

/// Number of samples a line of `width` pixels has in v210 order. An odd width
/// ends on a Cr sample with no second luma sample.
fn v210_sample_count(width: usize) -> usize {
    width + 2 * chroma_width(width)
}

fn put_samples<const B: usize, const N: usize>(dst: &mut [u8; B], samples: [u32; N]) {
    for (b, sample) in dst.chunks_exact_mut(2).zip(samples) {
        b.copy_from_slice(&((sample & SAMPLE_MASK) as u16).to_le_bytes());
    }
}

fn get_words<const B: usize, const N: usize>(src: &[u8; B]) -> [u32; N] {
    std::array::from_fn(|i| {
        u32::from_le_bytes([src[4 * i], src[4 * i + 1], src[4 * i + 2], src[4 * i + 3]])
    })
}

fn put_words<const B: usize, const N: usize>(dst: &mut [u8; B], words: [u32; N]) {
    for (b, word) in dst.chunks_exact_mut(4).zip(words) {
        b.copy_from_slice(&word.to_le_bytes());
    }
}

/// Three 10-bit samples in one 32-bit word, first sample in the low bits.
fn pack3(a: u32, b: u32, c: u32) -> u32 {
    a | (b << 10) | (c << 20)
}

/// Bytes of a plane that one v210 group covers when unpacking: 6 luma or 3 chroma samples.
const GROUP_LUMA_BYTES: usize = 12;
const GROUP_CHROMA_BYTES: usize = 6;

fn unpack_v210_line(src: &[u8], width: usize, y: &mut [u8], u: &mut [u8], v: &mut [u8]) {
    let full_groups = width / V210_GROUP_PIXELS;
    let groups = src
        .chunks_exact(V210_GROUP_BYTES)
        .zip(y.chunks_exact_mut(GROUP_LUMA_BYTES))
        .zip(u.chunks_exact_mut(GROUP_CHROMA_BYTES))
        .zip(v.chunks_exact_mut(GROUP_CHROMA_BYTES))
        .take(full_groups);
    for (((group, ys), us), vs) in groups {
        let (Ok(group), Ok(ys), Ok(us), Ok(vs)) = (
            <&[u8; V210_GROUP_BYTES]>::try_from(group),
            <&mut [u8; GROUP_LUMA_BYTES]>::try_from(ys),
            <&mut [u8; GROUP_CHROMA_BYTES]>::try_from(us),
            <&mut [u8; GROUP_CHROMA_BYTES]>::try_from(vs),
        ) else {
            continue;
        };
        let [w0, w1, w2, w3] = get_words(group);
        put_samples(ys, [w0 >> 10, w1, w1 >> 20, w2 >> 10, w3, w3 >> 20]);
        put_samples(us, [w0, w1 >> 10, w2 >> 20]);
        put_samples(vs, [w0 >> 20, w2, w3 >> 10]);
    }

    // A width that is not a multiple of 6 leaves a partial group at the end.
    let start = full_groups * V210_GROUP_PIXELS * 2;
    for n in start..v210_sample_count(width) {
        let value = read_word(src, n / 3) >> (10 * (n % 3));
        match v210_sample(n) {
            V210Sample::Cb(i) => write_sample(u, i, value),
            V210Sample::Y(i) => write_sample(y, i, value),
            V210Sample::Cr(i) => write_sample(v, i, value),
        }
    }
}

fn pack_v210_line(y: &[u16], u: &[u16], v: &[u16], width: usize, dst: &mut [u8]) {
    let full_groups = width / V210_GROUP_PIXELS;
    let groups = dst
        .chunks_exact_mut(V210_GROUP_BYTES)
        .zip(y.chunks_exact(6))
        .zip(u.chunks_exact(3))
        .zip(v.chunks_exact(3))
        .take(full_groups);
    for (((group, ys), us), vs) in groups {
        let (Ok(group), Ok(ys), Ok(us), Ok(vs)) = (
            <&mut [u8; V210_GROUP_BYTES]>::try_from(group),
            <&[u16; 6]>::try_from(ys),
            <&[u16; 3]>::try_from(us),
            <&[u16; 3]>::try_from(vs),
        ) else {
            continue;
        };
        let s = |samples: &[u16], i: usize| u32::from(samples[i]);
        put_words(
            group,
            [
                pack3(s(us, 0), s(ys, 0), s(vs, 0)),
                pack3(s(ys, 1), s(us, 1), s(ys, 2)),
                pack3(s(vs, 1), s(ys, 3), s(us, 2)),
                pack3(s(ys, 4), s(vs, 2), s(ys, 5)),
            ],
        );
    }

    // The partial group and the line padding are built up from zero.
    dst[full_groups * V210_GROUP_BYTES..].fill(0);
    let start = full_groups * V210_GROUP_PIXELS * 2;
    for n in start..v210_sample_count(width) {
        let value = u32::from(match v210_sample(n) {
            V210Sample::Cb(i) => u[i],
            V210Sample::Y(i) => y[i],
            V210Sample::Cr(i) => v[i],
        });
        let packed = read_word(dst, n / 3) | (value << (10 * (n % 3)));
        write_word(dst, n / 3, packed);
    }
}

/// GStreamer alpha for the key sample in the low 10 bits of `word`.
fn alpha_of(word: u32) -> u32 {
    u32::from(gst_alpha_from_mxl_key((word & SAMPLE_MASK) as u16))
}

fn key_of(alpha: u16) -> u32 {
    u32::from(mxl_key_from_gst_alpha(alpha))
}

fn unpack_alpha_line(src: &[u8], width: usize, a: &mut [u8]) {
    let full_words = width / ALPHA_SAMPLES_PER_WORD;
    let words = src
        .chunks_exact(4)
        .zip(a.chunks_exact_mut(ALPHA_SAMPLES_PER_WORD * 2))
        .take(full_words);
    for (word, samples) in words {
        let (Ok(word), Ok(samples)) = (
            <&[u8; 4]>::try_from(word),
            <&mut [u8; ALPHA_SAMPLES_PER_WORD * 2]>::try_from(samples),
        ) else {
            continue;
        };
        let [w] = get_words(word);
        put_samples(samples, [alpha_of(w), alpha_of(w >> 10), alpha_of(w >> 20)]);
    }
    for n in full_words * ALPHA_SAMPLES_PER_WORD..width {
        let shift = 10 * (n % ALPHA_SAMPLES_PER_WORD);
        write_sample(
            a,
            n,
            alpha_of(read_word(src, n / ALPHA_SAMPLES_PER_WORD) >> shift),
        );
    }
}

fn pack_alpha_line(a: &[u16], width: usize, dst: &mut [u8]) {
    let full_words = width / ALPHA_SAMPLES_PER_WORD;
    let words = dst
        .chunks_exact_mut(4)
        .zip(a.chunks_exact(ALPHA_SAMPLES_PER_WORD))
        .take(full_words);
    for (word, s) in words {
        let (Ok(word), Ok([s0, s1, s2])) = (
            <&mut [u8; 4]>::try_from(word),
            <[u16; ALPHA_SAMPLES_PER_WORD]>::try_from(s),
        ) else {
            continue;
        };
        put_words(word, [pack3(key_of(s0), key_of(s1), key_of(s2))]);
    }

    // The last word may be partial; it and any padding are built up from zero.
    dst[full_words * 4..].fill(0);
    let tail = a
        .iter()
        .enumerate()
        .take(width)
        .skip(full_words * ALPHA_SAMPLES_PER_WORD);
    for (n, &sample) in tail {
        let index = n / ALPHA_SAMPLES_PER_WORD;
        let shift = 10 * (n % ALPHA_SAMPLES_PER_WORD);
        write_word(
            dst,
            index,
            read_word(dst, index) | (key_of(sample) << shift),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Deterministic pseudo-random 10-bit samples (xorshift), so a failure
    /// reproduces exactly.
    fn samples(count: usize, seed: u32) -> Vec<u8> {
        let mut state = seed.max(1);
        let mut out = Vec::with_capacity(count * 2);
        for _ in 0..count {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            out.extend_from_slice(&((state & SAMPLE_MASK) as u16).to_le_bytes());
        }
        out
    }

    /// An `A422_10LE` frame held as four separate planes, with strides wider than
    /// the samples, as GStreamer may allocate, so stride handling is exercised.
    struct Frame {
        planes: [Vec<u8>; 4],
        strides: [usize; 4],
    }

    impl Frame {
        fn random(width: usize, height: usize, seed: u32) -> Self {
            let luma = width * 2 + 8;
            let chroma = chroma_width(width) * 2 + 4;
            let strides = [luma, chroma, chroma, luma];
            let planes = std::array::from_fn(|i| samples(strides[i] / 2 * height, seed + i as u32));
            Frame { planes, strides }
        }

        fn zeroed_like(other: &Frame) -> Self {
            Frame {
                planes: other.planes.each_ref().map(|p| vec![0; p.len()]),
                strides: other.strides,
            }
        }

        fn planes(&self) -> [Plane<'_>; 4] {
            std::array::from_fn(|i| Plane {
                data: &self.planes[i],
                stride: self.strides[i],
            })
        }

        fn planes_mut(&mut self) -> [PlaneMut<'_>; 4] {
            let [sy, su, sv, sa] = self.strides;
            let [y, u, v, a] = &mut self.planes;
            [(y, sy), (u, su), (v, sv), (a, sa)].map(|(data, stride)| PlaneMut {
                data: data.as_mut_slice(),
                stride,
            })
        }

        /// This frame with each alpha sample replaced by what survives a trip
        /// through a video-range key.
        fn alpha_through_key(&self) -> Self {
            let mut out = Frame {
                planes: self.planes.clone(),
                strides: self.strides,
            };
            for b in out.planes[3].chunks_exact_mut(2) {
                let alpha = u16::from_le_bytes([b[0], b[1]]);
                let back = gst_alpha_from_mxl_key(mxl_key_from_gst_alpha(alpha));
                b.copy_from_slice(&back.to_le_bytes());
            }
            out
        }

        /// The sample bytes of each line, without the stride padding.
        fn lines(&self, width: usize, height: usize) -> Vec<Vec<u8>> {
            let samples = [width, chroma_width(width), chroma_width(width), width];
            let mut out = Vec::new();
            for line in 0..height {
                for ((plane, stride), count) in self.planes.iter().zip(self.strides).zip(samples) {
                    out.push(plane[line * stride..][..count * 2].to_vec());
                }
            }
            out
        }
    }

    fn grain_size(width: usize, height: usize) -> usize {
        v210a_grain_size(width, height).unwrap()
    }

    #[test]
    fn line_lengths_match_the_architecture_document() {
        assert_eq!(v210_line_length(1280), 3456);
        assert_eq!(v210_line_length(1920), 5120);
        assert_eq!(alpha_line_length(1280), 1708);
        assert_eq!(alpha_line_length(1920), 2560);
        assert_eq!(grain_size(1920, 1080), (5120 + 2560) * 1080);
        assert_eq!(v210a_grain_size(usize::MAX / 2, 3), None);
    }

    #[test]
    fn frame_survives_a_round_trip_through_a_grain() {
        // Widths that end on a full group, on each partial group, and on an odd pixel.
        for &width in &[2usize, 6, 8, 10, 12, 46, 48, 50, 1280, 1920, 7] {
            let height = 3;
            let frame = Frame::random(width, height, width as u32 * 7 + 1);
            let mut grain = vec![0xaa; grain_size(width, height)];
            mxl_v210a_grain_from_gst_a422_10le(&frame.planes(), width, height, &mut grain).unwrap();

            let mut back = Frame::zeroed_like(&frame);
            gst_a422_10le_from_mxl_v210a_grain(&grain, width, height, &mut back.planes_mut())
                .unwrap();
            assert_eq!(
                back.lines(width, height),
                frame.alpha_through_key().lines(width, height),
                "width {width}"
            );
        }
    }

    #[test]
    fn grain_survives_a_round_trip_through_a_frame() {
        let (width, height) = (1280, 4);
        let frame = Frame::random(width, height, 99);
        let mut grain = vec![0; grain_size(width, height)];
        mxl_v210a_grain_from_gst_a422_10le(&frame.planes(), width, height, &mut grain).unwrap();

        let mut middle = Frame::zeroed_like(&frame);
        gst_a422_10le_from_mxl_v210a_grain(&grain, width, height, &mut middle.planes_mut())
            .unwrap();
        let mut again = vec![0xff; grain.len()];
        mxl_v210a_grain_from_gst_a422_10le(&middle.planes(), width, height, &mut again).unwrap();
        assert_eq!(again, grain);
    }

    #[test]
    fn samples_land_where_the_architecture_document_puts_them() {
        // One group of six pixels with distinct values in every sample.
        let width = 6;
        let bytes = |s: &[u16]| s.iter().flat_map(|v| v.to_le_bytes()).collect::<Vec<u8>>();
        let (y, u, v, a) = (
            bytes(&[1, 2, 3, 4, 5, 6]),
            bytes(&[11, 12, 13]),
            bytes(&[21, 22, 23]),
            bytes(&[31, 32, 33, 34, 35, 36]),
        );
        let planes = [
            Plane {
                data: &y,
                stride: 12,
            },
            Plane {
                data: &u,
                stride: 6,
            },
            Plane {
                data: &v,
                stride: 6,
            },
            Plane {
                data: &a,
                stride: 12,
            },
        ];
        let mut grain = vec![0; grain_size(width, 1)];
        mxl_v210a_grain_from_gst_a422_10le(&planes, width, 1, &mut grain).unwrap();

        let word = |n: usize| read_word(&grain, n);
        let pack = |a: u32, b: u32, c: u32| a | (b << 10) | (c << 20);
        assert_eq!(word(0), pack(11, 1, 21), "Cb0 Y0 Cr0");
        assert_eq!(word(1), pack(2, 12, 3), "Y1 Cb1 Y2");
        assert_eq!(word(2), pack(22, 4, 13), "Cr1 Y3 Cb2");
        assert_eq!(word(3), pack(5, 23, 6), "Y4 Cr2 Y5");
        assert!(grain[16..128].iter().all(|&b| b == 0), "fill line padding");

        let key = 128 / 4;
        let k = |alpha: u16| u32::from(mxl_key_from_gst_alpha(alpha));
        assert_eq!(word(key), pack(k(31), k(32), k(33)));
        assert_eq!(word(key + 1), pack(k(34), k(35), k(36)));
    }

    #[test]
    fn key_padding_is_zero_when_width_is_not_a_multiple_of_three() {
        let (width, height) = (1280, 1);
        let mut frame = Frame::random(width, height, 5);
        frame.planes[3].iter_mut().for_each(|b| *b = 0xff);
        let mut grain = vec![0xaa; grain_size(width, height)];
        mxl_v210a_grain_from_gst_a422_10le(&frame.planes(), width, height, &mut grain).unwrap();

        // 1280 = 426 * 3 + 2: the last word holds two samples and zero in bits 20 to 31.
        let last = read_word(&grain, grain.len() / 4 - 1);
        assert_eq!(
            last,
            940 | (940 << 10),
            "opaque is 940 in a video-range key"
        );
    }

    #[test]
    fn key_range_maps_to_full_range_alpha() {
        assert_eq!(gst_alpha_from_mxl_key(64), 0, "transparent");
        assert_eq!(gst_alpha_from_mxl_key(940), 1023, "opaque");
        assert_eq!(mxl_key_from_gst_alpha(0), 64);
        assert_eq!(mxl_key_from_gst_alpha(1023), 940);
        // Out of range keys are clamped, and only the low 10 bits are read.
        assert_eq!(gst_alpha_from_mxl_key(4), 0);
        assert_eq!(gst_alpha_from_mxl_key(1000), 1023);
        assert_eq!(gst_alpha_from_mxl_key(0xffff), 1023);
        // Every key value survives a trip through alpha, and the map is monotonic.
        let mut previous = 0;
        for key in 64..=940u16 {
            let alpha = gst_alpha_from_mxl_key(key);
            assert_eq!(mxl_key_from_gst_alpha(alpha), key, "key {key}");
            assert!(key == 64 || alpha > previous, "key {key}");
            previous = alpha;
        }
    }

    #[test]
    fn short_buffers_are_rejected() {
        let (width, height) = (48, 2);
        let frame = Frame::random(width, height, 3);
        let mut grain = vec![0; grain_size(width, height) - 1];
        assert!(matches!(
            mxl_v210a_grain_from_gst_a422_10le(&frame.planes(), width, height, &mut grain),
            Err(VideoLayoutError::TooShort {
                what: "v210a grain",
                ..
            })
        ));

        let grain = vec![0; grain_size(width, height)];
        let mut small = Frame::random(width, height - 1, 3);
        assert!(matches!(
            gst_a422_10le_from_mxl_v210a_grain(&grain, width, height, &mut small.planes_mut()),
            Err(VideoLayoutError::TooShort { .. })
        ));

        let mut narrow = Frame::random(width, height, 3);
        narrow.strides[1] = 2;
        assert!(matches!(
            gst_a422_10le_from_mxl_v210a_grain(&grain, width, height, &mut narrow.planes_mut()),
            Err(VideoLayoutError::TooShort {
                what: "U plane",
                ..
            })
        ));
    }
}
