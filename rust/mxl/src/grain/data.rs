// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

pub struct GrainData<'a> {
    /// The grain payload. This may be a partial payload if the grain is not complete.
    /// The length of this slice is given by `commitedSize` in `mxlGrainInfo`.
    pub payload: &'a [u8],

    /// The total size of the grain payload, which may be larger than `payload.len()` if the grain is partial.
    pub total_size: usize,

    pub flags: u32,
}

impl<'a> GrainData<'a> {
    pub fn to_owned(&self) -> OwnedGrainData {
        self.into()
    }
}

impl<'a> AsRef<GrainData<'a>> for GrainData<'a> {
    fn as_ref(&self) -> &GrainData<'a> {
        self
    }
}

pub struct OwnedGrainData {
    pub payload: Vec<u8>,
}

impl<'a> From<&GrainData<'a>> for OwnedGrainData {
    fn from(value: &GrainData<'a>) -> Self {
        Self {
            payload: value.payload.to_vec(),
        }
    }
}

impl<'a> From<GrainData<'a>> for OwnedGrainData {
    fn from(value: GrainData<'a>) -> Self {
        value.as_ref().into()
    }
}
