// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::{
    Error,
    error::Result,
    fabrics::target::{Target, states::Sample},
};

pub struct SampleReadResult {
    pub head_index: u64,
    pub count: usize,
}

impl Target<Sample> {
    ///Blocking accessor for a new grain.
    pub fn read(&self, timeout: Duration) -> Result<SampleReadResult> {
        let mut head_index = 0;
        let mut count = 0;
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_target_read_samples(
                self.instance.inner,
                u16::try_from(timeout.as_millis()).map_err(|_| Error::InvalidArg)?,
                &mut head_index,
                &mut count,
            )
        })?;
        Ok(SampleReadResult { head_index, count })
    }
    /// Non-blocking accessor for a new grain.
    pub fn read_non_blocking(&self) -> Result<SampleReadResult> {
        let mut head_index = 0;
        let mut count = 0;
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_target_read_samples_non_blocking(
                    self.instance.inner,
                    &mut head_index,
                    &mut count,
                )
        })?;
        Ok(SampleReadResult { head_index, count })
    }
}
