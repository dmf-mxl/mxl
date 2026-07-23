// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::{
    Error, Result,
    fabrics::{TargetInfo, initiator::Grain, initiator::Initiator},
};

impl Initiator<Grain> {
    /// Add a target to the initiator. This will allow the initiator to send data to the target in subsequent calls to
    /// mxlFabricsInitiatorTransferGrain(). This function is always non-blocking. If additional connection setup is required
    /// by the underlying implementation, it will only happen during a call to make_progress*().
    pub fn add_target(&self, target: &TargetInfo) -> Result<()> {
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_initiator_add_target(self.instance.inner, target.inner)
        })
    }

    /// Remove a target from the initiator. This function is always non-blocking. If any additional communication for a graceful shutdown is
    /// required it will happend during a call to make_progress*(). It is guaranteed that no new grain transfer operations will
    /// be queued for this target during calls to transfer() after the target was removed, but it is only guaranteed that
    /// the connection shutdown has completed after make_progress*() no longer returns Error::NotReady.
    pub fn remove_target(&self, target: &TargetInfo) -> Result<()> {
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_initiator_remove_target(self.instance.inner, target.inner)
        })
    }

    /// This function must be called regularly for the initiator to make progress on queued transfer operations, connection establishment
    /// operations and connection shutdown operations.
    pub fn make_progress_non_blocking(&self) -> Result<()> {
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_initiator_make_progress_non_blocking(self.instance.inner)
        })
    }

    /// This function must be called regularly for the initiator to make progress on queued transfer operations, connection establishment
    /// operations and connection shutdown operations.
    pub fn make_progress(&self, timeout: Duration) -> Result<()> {
        Error::from_status(unsafe {
            self.instance
                .ctx
                .api()
                .fabrics_initiator_make_progress_blocking(
                    self.instance.inner,
                    timeout.as_millis() as u16,
                )
        })
    }

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
