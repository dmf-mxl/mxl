// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod config;
mod grain;
mod samples;

use crate::{
    FlowConfigInfo,
    error::{Error, Result},
    fabrics::{initiator::config::OwnedInitiatorConfig, instance::FabricsInstanceContext},
};

pub use config::Config;

use std::{marker::PhantomData, sync::Arc};

use states::*;

pub mod states {
    /// Used to create a new initiator
    pub struct New {}

    /// Waiting for the initiator to be initialized with the setup function
    pub struct Initializing {}

    /// The setup function has been called, but the initiator has not yet been specialized into a
    /// grain or samples initiator
    pub struct Specializing {}

    /// The initiator has been specialized into a grain initiator. It can only transfer grains to
    /// targets.
    pub struct Grain {}

    /// The initiator has been specialized into a samples initiator. It can only transfer samples to
    pub struct Samples {}

    impl InitiatorState for New {}
    impl InitiatorState for Initializing {}
    impl InitiatorState for Specializing {}
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

pub enum Either {
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
    pub fn setup(self, config: &Config) -> Result<Initiator<Specializing>> {
        let config = OwnedInitiatorConfig::new(config)?;
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_initiator_setup(
                self.instance.inner,
                config.as_ffi(),
                std::ptr::null(), // Unused for now
            )
        })?;
        Ok(Initiator {
            instance: self.instance,
            _marker: PhantomData,
        })
    }
}

impl Initiator<Specializing> {
    /// Specialize the initator into a concrete grain or samples initator
    pub fn specialize(self, flow_config: &FlowConfigInfo) -> Either {
        if flow_config.is_discrete_flow() {
            Either::Grain(Initiator {
                instance: self.instance,
                _marker: PhantomData,
            })
        } else {
            Either::Samples(Initiator {
                instance: self.instance,
                _marker: PhantomData,
            })
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
