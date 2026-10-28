// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// use std::time::Duration;
//
use crate::{
    Error, Result,
    fabrics::target::{Target, states::Grain},
};
use std::time::Duration;

/// Returned value from calling read* methods.
pub struct GrainReadResult {
    pub grain_index: u64,
}

impl Target<Grain> {
    /// Blocking accessor for a new grain.
    pub fn read(&self, timeout: Duration) -> Result<GrainReadResult> {
        let mut grain_index = 0;
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_target_read_grain(
                self.instance.inner,
                u16::try_from(timeout.as_millis()).map_err(|_| Error::InvalidArg)?,
                &mut grain_index,
            )
        })?;
        Ok(GrainReadResult { grain_index })
    }

    /// Non-blocking accessor for a new grain.
    pub fn read_non_blocking(&self) -> Result<GrainReadResult> {
        let mut grain_index = 0;
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_target_read_grain_non_blocking(self.instance.inner, &mut grain_index)
        })?;
        Ok(GrainReadResult { grain_index })
    }
}
