// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! SMPTE 291 ancillary packing and `GstAncillaryMeta` validity helpers.

use glib;
use gstreamer as gst;
use gstreamer_video as gst_video;

/// Extend an 8-bit value to a 10-bit ST 291 word with even/odd parity.
///
/// Per SMPTE ST 291: b8 is even parity over b7 to b0 (so b0 to b8 have an even
/// number of 1s), and b9 = NOT b8. Matches `SET_WITH_PARITY` in
/// gst-plugins-base `video-anc.c`: odd popcount gives `0x100 | v`, even gives
/// `0x200 | v`.
fn extend_with_even_odd_parity(v: u8) -> u16 {
    if v.count_ones() & 1 != 0 {
        // Odd number of ones in v: b8 = 1, b9 = 0
        0x1_00 | (v as u16)
    } else {
        // Even number of ones in v: b8 = 0, b9 = 1
        0x2_00 | (v as u16)
    }
}

fn has_even_odd_parity(w: u16) -> bool {
    w == extend_with_even_odd_parity((w & 0xff) as u8)
}

/// SMPTE 291 checksum over the 10-bit DID, SDID, DC and UDW words.
fn ancillary_checksum(did: u16, sdid: u16, dc: u16, data: &[u16]) -> u16 {
    let mut checksum = 0u16;
    checksum = checksum.wrapping_add(did & 0x1ff);
    checksum = checksum.wrapping_add(sdid & 0x1ff);
    checksum = checksum.wrapping_add(dc & 0x1ff);
    for &w in data {
        checksum = checksum.wrapping_add(w & 0x1ff);
    }
    checksum &= 0x1ff;
    // b9 = NOT b8 (ST 291 checksum word)
    if checksum & 0x1_00 == 0 {
        checksum |= 0x2_00;
    }
    checksum
}

/// Attach one `GstAncillaryMeta` carrying 8-bit `payload` under `did`/`sdid` on
/// `line`/`offset`, as 10-bit even/odd-parity words plus checksum.
pub fn add_ancillary_meta(
    buffer: &mut gst::BufferRef,
    line: u16,
    offset: u16,
    did: u8,
    sdid: u8,
    payload: &[u8],
) {
    let mut meta = gst_video::video_meta::AncillaryMeta::add(buffer);
    meta.set_c_not_y_channel(false);
    meta.set_line(line);
    meta.set_offset(offset);
    let did_10bit = extend_with_even_odd_parity(did);
    let sdid_10bit = extend_with_even_odd_parity(sdid);
    let dc_10bit = extend_with_even_odd_parity(payload.len() as u8);
    meta.set_did(did_10bit);
    meta.set_sdid_block_number(sdid_10bit);
    let data: Vec<u16> = payload
        .iter()
        .copied()
        .map(extend_with_even_odd_parity)
        .collect();
    meta.set_data(glib::Slice::from(data));
    // set_data writes only the length into data_count's low 8 bits and clears
    // the upper two; restore even/odd parity so DC is a valid 10-bit word.
    meta.set_data_count_upper_two_bits((dc_10bit >> 8) as u8);
    let checksum = ancillary_checksum(
        meta.did(),
        meta.sdid_block_number(),
        meta.data_count(),
        meta.data(),
    );
    meta.set_checksum(checksum);
}

/// Whether `w` is a permitted 10-bit word under ST 291-1 Section 9.1.
///
/// Prohibited TRS/sync codes are `0x000`-`0x003` and `0x3FC`-`0x3FF`. Values
/// above `0x3FF` are also rejected (not a 10-bit word). DID/SDID/DC with
/// even/odd parity already avoid the prohibited codes.
fn is_permitted_word(w: u16) -> bool {
    0x003 < w && w < 0x3fc
}

/// Whether one `GstAncillaryMeta` is a consistent SMPTE 291 packet: DID, SDID
/// and DC carry even/odd parity, each UDW is a permitted 10-bit word (ST 291-1
/// Section 9.1), and the stored checksum matches the sum over the stored
/// 10-bit DID/SDID/DC/UDW words. UDWs are not required to use 8-bit + parity
/// packing (ST 291 allows full 10-bit user data).
pub fn is_valid_ancillary_meta(meta: &gst_video::video_meta::AncillaryMeta) -> bool {
    let did = meta.did();
    let sdid = meta.sdid_block_number();
    let dc = meta.data_count();
    let data = meta.data();
    if !has_even_odd_parity(did) || !has_even_odd_parity(sdid) || !has_even_odd_parity(dc) {
        return false;
    }
    if !data.iter().all(|&w| is_permitted_word(w)) {
        return false;
    }
    meta.checksum() == ancillary_checksum(did, sdid, dc, data)
}

/// Whether every `GstAncillaryMeta` on `buffer` passes [`is_valid_ancillary_meta`].
pub fn has_valid_ancillary_metas(buffer: &gst::BufferRef) -> bool {
    buffer
        .iter_meta::<gst_video::video_meta::AncillaryMeta>()
        .all(|m| is_valid_ancillary_meta(&m))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn even_odd_parity_matches_st291_and_gst_vbi_encoder() {
        // Odd popcount gives b8=1, b9=0 (0x100 | v), as SET_WITH_PARITY in video-anc.c
        assert_eq!(extend_with_even_odd_parity(0x00), 0x200); // 0 ones, even
        assert_eq!(extend_with_even_odd_parity(0x01), 0x101); // 1 one, odd
        assert_eq!(extend_with_even_odd_parity(0x03), 0x203); // 2 ones, even
        assert_eq!(extend_with_even_odd_parity(0x61), 0x161); // 3 ones, odd (CEA-708 DID)

        for v in 0u8..=255 {
            let w = extend_with_even_odd_parity(v);
            assert_eq!(w & 0xff, u16::from(v));
            // Even parity over b0..b8: popcount of those 9 bits must be even
            let popcount_0_to_8 = (w & 0x1ff).count_ones();
            assert!(
                popcount_0_to_8.is_multiple_of(2),
                "b0..b8 must have even popcount for {v:#04x}, got {w:#05x}"
            );
            let b8 = (w >> 8) & 1;
            let b9 = (w >> 9) & 1;
            assert_eq!(b9, 1 - b8, "b9 must be NOT b8 for {v:#04x}");
        }
    }

    #[test]
    fn add_ancillary_meta_writes_parity_extended_adf_words() {
        gst::init().unwrap();

        let mut buf = gst::Buffer::new();
        {
            let buf = buf.make_mut();
            // DC=3 (even popcount) and CEA-708 DID/SDID so values are fixed.
            add_ancillary_meta(buf, 9, 0, 0x61, 0x01, &[0x41, 0x42, 0x43]);
        }

        let meta = buf
            .meta::<gst_video::video_meta::AncillaryMeta>()
            .expect("missing AncillaryMeta");
        assert!(
            is_valid_ancillary_meta(&meta),
            "DID={:#x} SDID={:#x} DC={:#x} CS={:#x}",
            meta.did(),
            meta.sdid_block_number(),
            meta.data_count(),
            meta.checksum()
        );
        // Direct ST 291 checks so validity is not only circular against a
        // buggy extend_with_even_odd_parity.
        assert_eq!(meta.did(), 0x161);
        assert_eq!(meta.sdid_block_number(), 0x101);
        assert_eq!(meta.data_count(), 0x203); // DC=3, even popcount => 0x203
    }
}
