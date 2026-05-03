// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! MXL `video/smpte291` data grains vs GStreamer `meta/x-st-2038` buffers.
//!
//! MXL `video/smpte291` data grains store bytes from the RFC 8331 *Length* field onward only
//! (see `lib/tests/test_flows.cpp`). That slice is also not the same as a bare ST 2038 packet:
//! RFC 8331 includes `S` (Data Stream Flag) and `StreamNum`, which ST 2038 omits. GStreamer
//! `rtpsmpte291pay` and `rtpsmpte291depay` perform a similar translation; see
//! <https://centricular.com/devlog/2025-11/st291-anc-rtp/>.

use bitstream_io::{BigEndian, BitRead, BitReader, BitWrite, BitWriter};
use std::io::Cursor;

/// Discrete data-format grain payload size in bytes (`DATA_FORMAT_GRAIN_SIZE` in
/// `lib/internal/src/FlowParser.cpp`).
pub const MXL_DATA_FORMAT_GRAIN_SIZE: usize = 4096;

#[derive(Debug, thiserror::Error)]
pub enum AncillaryMapError {
    #[error("invalid ancillary data: {0}")]
    Invalid(&'static str),
    #[error("bitstream I/O error: {0}")]
    Io(#[from] std::io::Error),
}

/// RFC 8331 ANC from `start_pos` occupies a multiple of 4 bytes. `w` must be byte-aligned.
fn writer_word_aligned(w: &mut BitWriter<Cursor<&mut Vec<u8>>, BigEndian>, start_pos: u64) -> bool {
    (w.writer().expect("byte_aligned").position() - start_pos).is_multiple_of(4)
}

/// RFC 8331 ANC from `start_pos` occupies a multiple of 4 bytes. `r` must be byte-aligned.
fn reader_word_aligned(r: &mut BitReader<Cursor<&[u8]>, BigEndian>, start_pos: u64) -> bool {
    (r.reader().expect("byte_aligned").position() - start_pos).is_multiple_of(4)
}

/// Read one GStreamer ST 2038 ANC packet (including ST 2038 end padding) and
/// write one RFC 8331 ANC packet into `smpte291`. Advances `st2038` past the consumed bytes.
/// Compare to `RtpSmpte291Pay::convert_next_packet` in gst-plugins-rs.
pub fn smpte291_from_st2038_anc_packet(
    smpte291: &mut Vec<u8>,
    st2038: &mut &[u8],
) -> Result<(), AncillaryMapError> {
    let r_cursor = Cursor::new(*st2038);
    let mut r = BitReader::endian(r_cursor, BigEndian);

    let zeroes = r
        .read::<6, u8>()
        .map_err(|_| AncillaryMapError::Invalid("read zero bits"))?;
    if zeroes != 0 {
        return Err(AncillaryMapError::Invalid(
            "ST 2038 leading zero bits must be zero",
        ));
    }
    let c_not_y_channel_flag = r
        .read_bit()
        .map_err(|_| AncillaryMapError::Invalid("read C flag"))?;
    let line_number = r
        .read::<11, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read line number"))?;
    let horizontal_offset = r
        .read::<12, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read horizontal offset"))?;
    let did = r
        .read::<10, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read DID"))?;
    let sdid = r
        .read::<10, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read SDID"))?;
    let data_count = r
        .read::<10, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read data count"))?;
    let data_count8 = (data_count & 0xff) as u8;

    let w_start_pos = smpte291.len() as u64;
    let mut w_cursor: Cursor<&mut Vec<u8>> = Cursor::new(smpte291);
    w_cursor.set_position(w_start_pos);
    let mut w = BitWriter::endian(w_cursor, BigEndian);

    w.write_bit(c_not_y_channel_flag)
        .map_err(|_| AncillaryMapError::Invalid("write C"))?;
    w.write::<11, u16>(line_number)
        .map_err(|_| AncillaryMapError::Invalid("write line"))?;
    w.write::<12, u16>(horizontal_offset)
        .map_err(|_| AncillaryMapError::Invalid("write horizontal offset"))?;

    // ST 2038 has no S or StreamNum here; RFC 8331 inserts them
    // (cf. `RtpSmpte291Pay::convert_next_packet`).
    w.write_bit(false)
        .map_err(|_| AncillaryMapError::Invalid("write S"))?;
    w.write::<7, u8>(0u8)
        .map_err(|_| AncillaryMapError::Invalid("write StreamNum"))?;

    w.write::<10, u16>(did)
        .map_err(|_| AncillaryMapError::Invalid("write DID"))?;
    w.write::<10, u16>(sdid)
        .map_err(|_| AncillaryMapError::Invalid("write SDID"))?;
    w.write::<10, u16>(data_count)
        .map_err(|_| AncillaryMapError::Invalid("write data_count"))?;

    for _ in 0..data_count8 {
        let udw = r
            .read::<10, u16>()
            .map_err(|_| AncillaryMapError::Invalid("read user data word"))?;
        w.write::<10, u16>(udw)
            .map_err(|_| AncillaryMapError::Invalid("write user data word"))?;
    }
    let csum = r
        .read::<10, u16>()
        .map_err(|_| AncillaryMapError::Invalid("read checksum"))?;
    w.write::<10, u16>(csum)
        .map_err(|_| AncillaryMapError::Invalid("write checksum"))?;

    // ST 2038 pads to a byte boundary with 1 bits.
    while !r.byte_aligned() {
        let one = r
            .read_bit()
            .map_err(|_| AncillaryMapError::Invalid("read padding bit"))?;
        if !one {
            return Err(AncillaryMapError::Invalid(
                "ST 2038 padding bits must be ones",
            ));
        }
    }

    // Byte-align and then pad with zero bits to a 4-byte boundary.
    w.byte_align()
        .map_err(|_| AncillaryMapError::Invalid("write word_align (bits)"))?;
    while !writer_word_aligned(&mut w, w_start_pos) {
        w.write::<8, u8>(0u8)
            .map_err(|_| AncillaryMapError::Invalid("write word_align (bytes)"))?;
    }

    w.flush().map_err(AncillaryMapError::Io)?;
    let _ = w.into_writer();

    let consumed = r.into_reader().position() as usize;
    *st2038 = &st2038[consumed..];
    Ok(())
}

/// Read one RFC 8331 ANC packet from `smpte291` (including word-align padding) and
/// write one ST 2038 ANC packet into `st2038`. Advances `smpte291` past the consumed bytes.
pub fn st2038_from_smpte291_anc_packet(
    st2038: &mut Vec<u8>,
    smpte291: &mut &[u8],
) -> Result<(), AncillaryMapError> {
    let r_cursor = Cursor::new(*smpte291);
    let r_start_pos = r_cursor.position();
    let mut r = BitReader::endian(r_cursor, BigEndian);

    let c_not_y_channel_flag = r.read_bit().map_err(AncillaryMapError::Io)?;
    let line_number = r.read::<11, u16>().map_err(AncillaryMapError::Io)?;
    let horizontal_offset = r.read::<12, u16>().map_err(AncillaryMapError::Io)?;
    let _s = r.read::<1, u8>().map_err(AncillaryMapError::Io)?;
    let _stream_num = r.read::<7, u8>().map_err(AncillaryMapError::Io)?;
    let did = r.read::<10, u16>().map_err(AncillaryMapError::Io)?;
    let sdid = r.read::<10, u16>().map_err(AncillaryMapError::Io)?;
    let data_count = r.read::<10, u16>().map_err(AncillaryMapError::Io)?;
    let data_count8 = (data_count & 0xff) as u8;

    let w_start_pos = st2038.len() as u64;
    let mut w_cursor: Cursor<&mut Vec<u8>> = Cursor::new(st2038);
    w_cursor.set_position(w_start_pos);
    let mut w = BitWriter::endian(w_cursor, BigEndian);

    w.write::<6, u8>(0).map_err(AncillaryMapError::Io)?;
    w.write_bit(c_not_y_channel_flag)
        .map_err(AncillaryMapError::Io)?;
    w.write::<11, u16>(line_number)
        .map_err(AncillaryMapError::Io)?;
    w.write::<12, u16>(horizontal_offset)
        .map_err(AncillaryMapError::Io)?;
    w.write::<10, u16>(did).map_err(AncillaryMapError::Io)?;
    w.write::<10, u16>(sdid).map_err(AncillaryMapError::Io)?;
    w.write::<10, u16>(data_count)
        .map_err(AncillaryMapError::Io)?;
    for _ in 0..data_count8 {
        let udw = r.read::<10, u16>().map_err(AncillaryMapError::Io)?;
        w.write::<10, u16>(udw).map_err(AncillaryMapError::Io)?;
    }
    let csum = r.read::<10, u16>().map_err(AncillaryMapError::Io)?;
    w.write::<10, u16>(csum).map_err(AncillaryMapError::Io)?;

    // RFC 8331 pads to 4-byte boundary with zero bits.
    while !r.byte_aligned() {
        let zero = r.read_bit().map_err(AncillaryMapError::Io)?;
        if zero {
            return Err(AncillaryMapError::Invalid(
                "RFC 8331 padding bits must be zero",
            ));
        }
    }
    while !reader_word_aligned(&mut r, r_start_pos) {
        let zero = r.read::<8, u8>().map_err(AncillaryMapError::Io)?;
        if zero != 0 {
            return Err(AncillaryMapError::Invalid(
                "RFC 8331 padding bytes must be zero",
            ));
        }
    }

    while !w.byte_aligned() {
        w.write_bit(true).map_err(AncillaryMapError::Io)?;
    }
    w.flush().map_err(AncillaryMapError::Io)?;
    let _ = w.into_writer();

    let consumed = r.into_reader().position() as usize;
    *smpte291 = &smpte291[consumed..];
    Ok(())
}

/// Make an MXL `video/smpte291` data grain from a GStreamer `meta/x-st-2038` buffer.
pub fn mxl_smpte291_grain_from_gst_st2038(st2038: &[u8]) -> Result<Vec<u8>, AncillaryMapError> {
    let mut smpte291 = Vec::new();
    {
        let mut w = BitWriter::endian(&mut smpte291, BigEndian);
        w.write_from::<u16>(0u16)?; // Length (updated below)
        w.write_from::<u8>(0u8)?; // ANC_Count (updated below)
        w.write::<2, u8>(0u8)?; // F
        w.write::<22, u32>(0u32)?; // reserved
        w.flush()?;
    }
    let header_len = smpte291.len();

    let mut anc_count: u8 = 0;
    let mut remaining = st2038;
    while !remaining.is_empty() {
        if anc_count == u8::MAX {
            return Err(AncillaryMapError::Invalid(
                "more than 255 ST 2038 ANC packets in one buffer",
            ));
        }
        smpte291_from_st2038_anc_packet(&mut smpte291, &mut remaining)?;
        anc_count += 1;
    }

    let length = u16::try_from(smpte291.len() - header_len)
        .map_err(|_| AncillaryMapError::Invalid("RFC 8331 Length overflow"))?;
    smpte291[0..2].copy_from_slice(&length.to_be_bytes());
    smpte291[2] = anc_count;
    pad_grain(&mut smpte291)?;
    Ok(smpte291)
}

/// Make a GStreamer `meta/x-st-2038` buffer from an MXL `video/smpte291` data grain.
pub fn gst_st2038_from_mxl_smpte291_grain(smpte291: &[u8]) -> Result<Vec<u8>, AncillaryMapError> {
    let mut r = BitReader::endian(Cursor::new(smpte291), BigEndian);
    let length = r.read_to::<u16>().map_err(AncillaryMapError::Io)? as usize;
    let anc_count = r.read_to::<u8>().map_err(AncillaryMapError::Io)? as usize;
    let _f = r.read::<2, u8>().map_err(AncillaryMapError::Io)?;
    let _reserved = r.read::<22, u32>().map_err(AncillaryMapError::Io)?;
    assert!(r.byte_aligned(), "RFC 8331 header is byte-aligned");
    let header_len = r.into_reader().position() as usize;
    let mut remaining =
        smpte291
            .get(header_len..header_len + length)
            .ok_or(AncillaryMapError::Invalid(
                "RFC 8331 Length exceeds MXL data grain size",
            ))?;

    let mut st2038 = Vec::new();
    for _ in 0..anc_count {
        st2038_from_smpte291_anc_packet(&mut st2038, &mut remaining)?;
    }
    if !remaining.is_empty() {
        return Err(AncillaryMapError::Invalid(
            "RFC 8331 Length does not match ANC_Count",
        ));
    }
    Ok(st2038)
}

fn pad_grain(buf: &mut Vec<u8>) -> Result<(), AncillaryMapError> {
    if buf.len() > MXL_DATA_FORMAT_GRAIN_SIZE {
        return Err(AncillaryMapError::Invalid(
            "RFC 8331 payload exceeds MXL data grain size",
        ));
    }
    buf.resize(MXL_DATA_FORMAT_GRAIN_SIZE, 0);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Same bytes as `gst-plugin-rtp` `smpte291::tests::packets[0]` (100 octets).
    fn st2038_test_packet() -> Vec<u8> {
        vec![
            0x00, 0x02, 0x40, 0x02, 0x61, 0x80, 0x64, 0x96, 0x59, 0x69, 0x92, 0x64, 0xf9, 0x0e,
            0x02, 0x8f, 0x57, 0x2b, 0xd1, 0xfc, 0xa0, 0x28, 0x0b, 0xf6, 0x80, 0xa0, 0x1f, 0xa4,
            0x01, 0x00, 0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f,
            0xa4, 0x01, 0x00, 0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40,
            0x1f, 0xa4, 0x01, 0x00, 0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00,
            0x40, 0x1f, 0xa4, 0x01, 0x00, 0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9,
            0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00, 0x7e, 0x90, 0x04, 0x01, 0x74, 0x80, 0xa3, 0xd5,
            0x06, 0xab,
        ]
    }

    #[test]
    fn smpte291_from_st2038_anc_packet_round_trip() {
        let expected = st2038_test_packet();
        let mut smpte291 = Vec::new();
        let mut remaining_st2038 = expected.as_slice();
        smpte291_from_st2038_anc_packet(&mut smpte291, &mut remaining_st2038).unwrap();
        assert!(remaining_st2038.is_empty());
        let mut remaining_smpte291 = smpte291.as_slice();
        let mut actual = Vec::new();
        st2038_from_smpte291_anc_packet(&mut actual, &mut remaining_smpte291)
            .expect("st2038_from_smpte291");
        assert!(remaining_smpte291.is_empty());
        assert_eq!(actual, expected);
    }

    #[test]
    fn smpte291_from_st2038_anc_packet_rejects_invalid_leading_zero_bits() {
        let bad = [0xffu8, 0xff, 0xff];
        let mut smpte291 = Vec::new();
        let mut remaining = bad.as_slice();
        match smpte291_from_st2038_anc_packet(&mut smpte291, &mut remaining) {
            Err(AncillaryMapError::Invalid(msg)) => {
                assert_eq!(msg, "ST 2038 leading zero bits must be zero");
            }
            Ok(_) => panic!("expected error"),
            Err(e) => panic!("unexpected error: {e:?}"),
        }
    }

    #[test]
    fn mxl_smpte291_grain_from_gst_st2038_single_anc_round_trip() {
        let expected = st2038_test_packet();
        let grain = mxl_smpte291_grain_from_gst_st2038(&expected).unwrap();
        let actual = gst_st2038_from_mxl_smpte291_grain(&grain).unwrap();
        assert_eq!(actual, expected);
    }

    #[test]
    fn mxl_smpte291_grain_from_gst_st2038_empty_round_trip() {
        let grain = mxl_smpte291_grain_from_gst_st2038(&[]).unwrap();
        let actual = gst_st2038_from_mxl_smpte291_grain(&grain).unwrap();
        assert!(actual.is_empty());
    }

    #[test]
    fn gst_st2038_from_mxl_smpte291_grain_rejects_length_past_buffer() {
        let mut grain = mxl_smpte291_grain_from_gst_st2038(&st2038_test_packet()).unwrap();
        grain[0] = 0xff;
        grain[1] = 0xff;
        match gst_st2038_from_mxl_smpte291_grain(&grain) {
            Err(AncillaryMapError::Invalid(msg)) => {
                assert_eq!(msg, "RFC 8331 Length exceeds MXL data grain size");
            }
            Ok(_) => panic!("expected error"),
            Err(e) => panic!("unexpected error: {e:?}"),
        }
    }

    #[test]
    fn gst_st2038_from_mxl_smpte291_grain_rejects_anc_count_zero_with_payload() {
        let mut grain = mxl_smpte291_grain_from_gst_st2038(&st2038_test_packet()).unwrap();
        grain[2] = 0;
        match gst_st2038_from_mxl_smpte291_grain(&grain) {
            Err(AncillaryMapError::Invalid(msg)) => {
                assert_eq!(msg, "RFC 8331 Length does not match ANC_Count");
            }
            Ok(_) => panic!("expected error"),
            Err(e) => panic!("unexpected error: {e:?}"),
        }
    }

    #[test]
    fn gst_st2038_from_mxl_smpte291_grain_rejects_anc_count_above_rfc_payload() {
        let a = st2038_test_packet();
        let b = st2038_test_packet();
        let expected = [a.as_slice(), b.as_slice()].concat();
        let mut grain = mxl_smpte291_grain_from_gst_st2038(&expected).unwrap();
        grain[2] = 3;
        match gst_st2038_from_mxl_smpte291_grain(&grain) {
            Err(AncillaryMapError::Io(e)) => {
                assert_eq!(e.kind(), std::io::ErrorKind::UnexpectedEof);
            }
            Ok(_) => panic!("expected error"),
            Err(e) => panic!("unexpected error: {e:?}"),
        }
    }

    #[test]
    fn mxl_smpte291_grain_from_gst_st2038_two_anc_round_trip() {
        let a = st2038_test_packet();
        let b = st2038_test_packet();
        let expected = [a.as_slice(), b.as_slice()].concat();
        let grain = mxl_smpte291_grain_from_gst_st2038(&expected).unwrap();
        let actual = gst_st2038_from_mxl_smpte291_grain(&grain).unwrap();
        assert_eq!(actual, expected);
    }

    #[test]
    fn mxl_smpte291_grain_from_gst_st2038_empty_st2038_zero_fills_grain() {
        let actual = mxl_smpte291_grain_from_gst_st2038(&[]).unwrap();
        assert_eq!(actual.len(), MXL_DATA_FORMAT_GRAIN_SIZE);
        assert!(actual.iter().all(|&b| b == 0));
    }
}
