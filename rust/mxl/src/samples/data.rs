// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Sample data structures for zero-copy audio access.

use std::marker::PhantomData;

use crate::Error;

/// Zero-copy view of multi-channel audio sample data.
///
/// Provides read-only access to audio samples stored in MXL's shared memory
/// ring buffer. Each channel is stored separately, and data may be split into
/// two fragments if the ring wraps around.
///
/// The lifetime `'a` is tied to the [`crate::SamplesReader`] that produced it.
///
/// # Channel Data Layout
///
/// Audio samples are stored in an interleaved-per-fragment layout:
/// - Each channel has its own contiguous buffer
/// - If the ring buffer wraps, a channel's data splits into two fragments
///
/// # Examples
///
/// ```no_run
/// # use mxl::SamplesData;
/// # fn example(samples: SamplesData) -> Result<(), mxl::Error> {
/// println!("Channels: {}", samples.num_of_channels());
///
/// for ch in 0..samples.num_of_channels() {
///     let (frag1, frag2) = samples.channel_data(ch)?;
///     println!("Channel {}: {} + {} bytes", ch, frag1.len(), frag2.len());
/// }
/// # Ok(())
/// # }
/// ```
pub struct SamplesData<'a> {
    buffer_slice: mxl_sys::WrappedMultiBufferSlice,
    phantom: PhantomData<&'a ()>,
}

impl<'a> SamplesData<'a> {
    /// Creates a new samples data view (internal use only).
    pub(crate) fn new(buffer_slice: mxl_sys::WrappedMultiBufferSlice) -> Self {
        Self {
            buffer_slice,
            phantom: Default::default(),
        }
    }

    /// Returns the number of audio channels.
    pub fn num_of_channels(&self) -> usize {
        self.buffer_slice.count
    }

    /// Returns zero-copy access to a specific channel's sample data.
    ///
    /// Each channel's data is returned as two byte slices (fragments). If the
    /// ring buffer doesn't wrap, the second fragment will be empty.
    ///
    /// **Note**: The returned slices are raw bytes. For float32 audio (typical
    /// in MXL), you'll need to cast or interpret these bytes as `&[f32]`.
    ///
    /// # Arguments
    ///
    /// * `channel` - Channel index (0-based)
    ///
    /// # Returns
    ///
    /// A tuple `(fragment1, fragment2)` where:
    /// - `fragment1`: First contiguous chunk of samples
    /// - `fragment2`: Second chunk (empty if no wrap), completing the ring
    ///
    /// # Errors
    ///
    /// Returns [`Error::InvalidArg`] if `channel >= num_of_channels()`.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::SamplesData;
    /// # fn example(samples: SamplesData) -> Result<(), mxl::Error> {
    /// let (frag1, frag2) = samples.channel_data(0)?;
    ///
    /// // Interpret as f32 samples (assuming float32 format)
    /// let samples_f32 = bytemuck::cast_slice::<u8, f32>(frag1);
    /// # Ok(())
    /// # }
    /// ```
    pub fn channel_data(&self, channel: usize) -> crate::Result<(&[u8], &[u8])> {
        if channel >= self.buffer_slice.count {
            return Err(Error::InvalidArg);
        }
        unsafe {
            let ptr_1 = (self.buffer_slice.base.fragments[0].pointer as *const u8)
                .add(self.buffer_slice.stride * channel);
            let size_1 = self.buffer_slice.base.fragments[0].size;
            let ptr_2 = (self.buffer_slice.base.fragments[1].pointer as *const u8)
                .add(self.buffer_slice.stride * channel);
            let size_2 = self.buffer_slice.base.fragments[1].size;
            Ok((
                std::slice::from_raw_parts(ptr_1, size_1),
                std::slice::from_raw_parts(ptr_2, size_2),
            ))
        }
    }

    /// Creates an owned copy of this sample data.
    ///
    /// Allocates vectors and copies all channel data. Use this when you need
    /// to store the samples beyond the reader's lifetime.
    pub fn to_owned(&self) -> OwnedSamplesData {
        self.into()
    }
}

impl<'a> AsRef<SamplesData<'a>> for SamplesData<'a> {
    fn as_ref(&self) -> &SamplesData<'a> {
        self
    }
}

/// Owned copy of multi-channel sample data.
///
/// Unlike [`SamplesData`], this owns its data and can outlive the reader.
/// Each channel is stored as a contiguous `Vec<u8>` (fragments are joined).
pub struct OwnedSamplesData {
    /// Per-channel sample data (raw bytes).
    ///
    /// Each inner `Vec<u8>` contains the complete samples for one channel,
    /// with both fragments concatenated.
    pub payload: Vec<Vec<u8>>,
}

impl<'a> From<&SamplesData<'a>> for OwnedSamplesData {
    /// Creates an owned copy by cloning and joining fragments for each channel.
    fn from(value: &SamplesData<'a>) -> Self {
        let mut payload = Vec::with_capacity(value.buffer_slice.count);
        for channel in 0..value.buffer_slice.count {
            // Safe unwrap: channel index is always valid
            let (data_1, data_2) = value.channel_data(channel).unwrap();
            let mut channel_payload = Vec::with_capacity(data_1.len() + data_2.len());
            channel_payload.extend(data_1);
            channel_payload.extend(data_2);
            payload.push(channel_payload);
        }
        Self { payload }
    }
}

impl<'a> From<SamplesData<'a>> for OwnedSamplesData {
    /// Creates an owned copy by cloning and joining fragments for each channel.
    fn from(value: SamplesData<'a>) -> Self {
        value.as_ref().into()
    }
}
