// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Grain data structures for zero-copy media access.

/// Zero-copy view of a grain's payload data.
///
/// Provides read-only access to a grain stored in MXL's shared memory ring buffer.
/// The lifetime `'a` is tied to the [`crate::GrainReader`] that produced it.
///
/// For partial grains (not yet fully written), `payload.len()` may be less than
/// [`Self::total_size`].
///
/// # Examples
///
/// ```no_run
/// # use mxl::{GrainReader, GrainData};
/// # use std::time::Duration;
/// # fn example(reader: GrainReader) -> Result<(), mxl::Error> {
/// let grain: GrainData = reader.get_complete_grain(0, Duration::from_secs(1))?;
/// println!("Grain size: {} bytes", grain.payload.len());
/// // Process grain.payload as &[u8]
/// # Ok(())
/// # }
/// ```
pub struct GrainData<'a> {
    /// The grain payload bytes (may be partial if grain is incomplete).
    ///
    /// This is a zero-copy view into shared memory. The data remains valid for
    /// the lifetime `'a` but may be overwritten by new writes after this grain
    /// is released.
    pub payload: &'a [u8],

    /// Total expected size of the complete grain payload.
    ///
    /// For partial grains, this is larger than `payload.len()`. For complete
    /// grains, this equals `payload.len()`.
    pub total_size: usize,

    /// Grain metadata flags from the MXL API.
    pub flags: u32,
}

impl<'a> GrainData<'a> {
    /// Creates an owned copy of this grain's payload.
    ///
    /// Allocates a `Vec` and copies the payload bytes. Use this when you need
    /// to store the grain data beyond the reader's lifetime.
    pub fn to_owned(&self) -> OwnedGrainData {
        self.into()
    }
}

impl<'a> AsRef<GrainData<'a>> for GrainData<'a> {
    fn as_ref(&self) -> &GrainData<'a> {
        self
    }
}

/// Owned copy of grain payload data.
///
/// Unlike [`GrainData`], this owns its data and can outlive the reader.
pub struct OwnedGrainData {
    /// Owned copy of the grain payload bytes.
    pub payload: Vec<u8>,
}

impl<'a> From<&GrainData<'a>> for OwnedGrainData {
    /// Creates an owned copy by cloning the payload.
    fn from(value: &GrainData<'a>) -> Self {
        Self {
            payload: value.payload.to_vec(),
        }
    }
}

impl<'a> From<GrainData<'a>> for OwnedGrainData {
    /// Creates an owned copy by cloning the payload.
    fn from(value: GrainData<'a>) -> Self {
        value.as_ref().into()
    }
}
