// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::marker::PhantomData;

use crate::Error;

pub struct SamplesData<'a> {
    buffer_slice: mxl_sys::mxlWrappedMultiBufferSlice,
    phantom: PhantomData<&'a ()>,
}

impl<'a> SamplesData<'a> {
    pub(crate) fn new(buffer_slice: mxl_sys::mxlWrappedMultiBufferSlice) -> Self {
        Self {
            buffer_slice,
            phantom: Default::default(),
        }
    }

    pub fn num_of_channels(&self) -> usize {
        self.buffer_slice.count
    }

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

    pub fn to_owned(&self) -> OwnedSamplesData {
        self.into()
    }
}

impl<'a> AsRef<SamplesData<'a>> for SamplesData<'a> {
    fn as_ref(&self) -> &SamplesData<'a> {
        self
    }
}

pub struct OwnedSamplesData {
    /// Data belonging to each of the channels.
    pub payload: Vec<Vec<u8>>,
}

impl<'a> From<&SamplesData<'a>> for OwnedSamplesData {
    fn from(value: &SamplesData<'a>) -> Self {
        let mut payload = Vec::with_capacity(value.buffer_slice.count);
        for channel in 0..value.buffer_slice.count {
            // The following unwrap is safe because the channel index always stays in the valid range.
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
    fn from(value: SamplesData<'a>) -> Self {
        value.as_ref().into()
    }
}
