// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{
    Error, Result,
    fabrics::{initiator::Grain, initiator::Initiator},
};

impl Initiator<Grain> {
    /// Enqueue a transfer operation to all added targets. This function is always non-blocking. The transfer operation might be started right
    /// away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns Error::NotReady.
    pub fn transfer(&self, grain_index: u64, start_slice: u16, end_slice: u16) -> Result<()> {
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_initiator_transfer_grain(
                self.instance.inner,
                grain_index,
                start_slice,
                end_slice,
            )
        })
    }
}
