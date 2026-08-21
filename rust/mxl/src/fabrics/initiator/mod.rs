// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod config;
mod grain;
mod samples;

use crate::{
    error::{Error, Result},
    fabrics::instance::FabricsInstanceContext,
};

pub use config::Config;
use mxl_sys::fabrics::FabricsInitiatorConfig;

use std::{marker::PhantomData, sync::Arc};

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

    pub trait InitiatorState {}
}

/// Wrapper class that holds a reference count to the Fabrics Instance and the actual initiator instance.
struct InitiatorInstance {
    ctx: Arc<FabricsInstanceContext>,
    inner: mxl_sys::fabrics::FabricsInitiator,
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
        initiator: mxl_sys::fabrics::FabricsInitiator,
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

/// Create a new initiator
#[doc(hidden)]
pub(crate) fn create_initiator(
    ctx: Arc<FabricsInstanceContext>,
) -> Result<Initiator<Initializing>> {
    let mut initiator = mxl_sys::fabrics::FabricsInitiator::default();
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
