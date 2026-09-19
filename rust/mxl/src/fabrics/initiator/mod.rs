// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod config;
mod grain;
mod samples;

use crate::{
    error::{Error, Result},
    fabrics::{TargetInfo, instance::FabricsInstanceContext},
};

pub use config::Config;
use mxl_sys::types::{FabricsInitiator, FabricsInitiatorConfig};

use std::{marker::PhantomData, sync::Arc, time::Duration};

use states::*;

pub mod states {
    /// Used to create a new initiator
    pub struct New {}

    /// Waiting for the initiator to be initialized with the setup function
    pub struct Initializing {}

    /// The initiator has been specialized into a grain initiator. It can only transfer grains to
    /// targets.
    pub struct Grain {}

    /// The initiator has been specialized into a samples initiator. It can only transfer samples to
    pub struct Samples {}

    impl InitiatorState for New {}
    impl InitiatorState for Initializing {}
    impl InitiatorState for Grain {}
    impl InitiatorState for Samples {}

    /// State of the initiator. This is used to ensure that the initiator is in a valid state before calling certain functions.
    pub trait InitiatorState {}

    /// In this state, the initiator can add/remove targets and make progress.
    pub trait InitiatorOperational: InitiatorState {}

    impl InitiatorOperational for Grain {}
    impl InitiatorOperational for Samples {}
}

/// Wrapper class that holds a reference count to the Fabrics Instance and the actual initiator instance.
struct InitiatorInstance {
    ctx: Arc<FabricsInstanceContext>,
    inner: FabricsInitiator,
}
unsafe impl Send for InitiatorInstance {}

impl Drop for InitiatorInstance {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe {
                self.ctx
                    .api()
                    .fabrics_destroy_initiator(self.ctx.inner, self.inner);
            }
        }
    }
}

pub struct Initiator<S: InitiatorState> {
    instance: InitiatorInstance,
    _marker: std::marker::PhantomData<S>,
}
//SAFETY: An initiator is safe to be sent across threads, but it's not thread-safe to use its API functions.
unsafe impl<S: InitiatorState> Send for Initiator<S> {}

pub enum InitiatorFlavor {
    Grain(Initiator<Grain>),
    Samples(Initiator<Samples>),
}

impl Initiator<New> {
    /// Create a new initiator
    pub(crate) fn new(
        ctx: Arc<FabricsInstanceContext>,
        initiator: FabricsInitiator,
    ) -> Initiator<Initializing> {
        let instance = InitiatorInstance {
            ctx,
            inner: initiator,
        };
        Initiator {
            instance,
            _marker: std::marker::PhantomData,
        }
    }
}

impl Initiator<Initializing> {
    ///  Configure the initiator.
    pub fn setup(self, config: &Config) -> Result<InitiatorFlavor> {
        let initiator_config = FabricsInitiatorConfig::try_from(config)?;
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_initiator_setup(
                self.instance.inner,
                // Pointer does not need to outlive the call, so we can safely pass a pointer to the stack variable.
                &initiator_config as *const _,
                std::ptr::null(), // Unused for now
            )
        })?;

        let flow_info = config.flow_reader.get_info()?;
        if flow_info.config.is_discrete_flow() {
            Ok(InitiatorFlavor::Grain(Initiator {
                instance: self.instance,
                _marker: PhantomData,
            }))
        } else {
            Ok(InitiatorFlavor::Samples(Initiator {
                instance: self.instance,
                _marker: PhantomData,
            }))
        }
    }
}

impl<S: InitiatorOperational> Initiator<S> {
    /// Add a target to the initiator. This will allow the initiator to send data to the target in subsequent calls.
    /// This function is always non-blocking. If additional connection setup is required
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
    /// required it will happen during a call to make_progress*(). It is guaranteed that no new grain/samples transfer operations will
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
                    u16::try_from(timeout.as_millis()).map_err(|_| Error::InvalidArg)?,
                )
        })
    }
}

/// Create a new initiator
#[doc(hidden)]
pub(crate) fn create_initiator(
    ctx: Arc<FabricsInstanceContext>,
) -> Result<Initiator<Initializing>> {
    let mut initiator = FabricsInitiator::default();
    unsafe {
        Error::from_status(
            ctx.api()
                .fabrics_create_initiator(ctx.inner, &mut initiator),
        )?
    }
    if initiator.is_null() {
        return Err(Error::Other(
            "Failed to create fabrics initiator.".to_string(),
        ));
    }
    Ok(Initiator::new(ctx.clone(), initiator))
}
