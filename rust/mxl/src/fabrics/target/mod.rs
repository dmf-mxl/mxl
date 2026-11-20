// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod config;
mod grain;
mod samples;

use std::{
    marker::PhantomData,
    sync::{Arc, OnceLock},
};

use crate::{
    FlowConfigInfo,
    error::{Error, Result},
    fabrics::{instance::FabricsInstanceContext, target_info::TargetInfo},
    writer::FlowWriterResourceKeepAlive,
};
pub use config::Config;

use mxl_sys::types::{FabricsTarget, FabricsTargetConfig, FabricsTargetInfo};
use states::*;

pub mod states {
    /// Used to create a new target
    pub struct New {}

    /// Waiting for the target to be initialized with the setup function
    pub struct Initializing {}

    /// The target has been specialized into a grain target. It can only receive grains
    pub struct Grain {}

    /// The target has been specialized into a samples target. It can only receive samples
    pub struct Samples {}

    impl TargetState for New {}
    impl TargetState for Initializing {}
    impl TargetState for Grain {}
    impl TargetState for Samples {}

    pub trait TargetState {}
}

/// Wrapper class that holds a reference count to the Fabrics Instance and the actual target
/// instance.
pub struct TargetInstance {
    ctx: Arc<FabricsInstanceContext>,
    inner: FabricsTarget,
    // Filled in setup(), once the flow writer is known. Keeps the writer's resources alive for as
    // long as the target may reference them.
    _flow_keep_alive: OnceLock<FlowWriterResourceKeepAlive>,
}
unsafe impl Send for TargetInstance {}

impl Drop for TargetInstance {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe {
                self.ctx
                    .api()
                    .fabrics_destroy_target(self.ctx.inner, self.inner);
            }
        }
    }
}

pub struct Target<S: TargetState> {
    instance: TargetInstance,
    _marker: PhantomData<S>,
}
//SAFETY: A target is safe to be sent across threads, but it's not thread-safe to use its API functions.
unsafe impl<S: TargetState> Send for Target<S> {}

pub enum TargetFlavor {
    Grain(Target<Grain>),
    Samples(Target<Samples>),
}

impl Target<New> {
    pub(crate) fn new(
        ctx: Arc<FabricsInstanceContext>,
        target: FabricsTarget,
    ) -> Target<Initializing> {
        let instance = TargetInstance {
            ctx,
            inner: target,
            _flow_keep_alive: OnceLock::new(),
        };
        Target {
            instance,
            _marker: PhantomData,
        }
    }
}

impl Target<Initializing> {
    /// Configure the target. After the target has been configured, it is ready to receive transfers from an initiator.
    /// If additional connection setup is required by the underlying implementation it might not happen during the call to
    /// setup(), but be deferred until the first call to mxlFabricsTargetTryNewGrain().
    pub fn setup(
        self,
        config: &Config,
        flow_config_info: &FlowConfigInfo,
    ) -> Result<(TargetFlavor, TargetInfo)> {
        let mut info = FabricsTargetInfo::default();
        let target_config = FabricsTargetConfig::try_from(config)?;
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_target_setup(
                self.instance.inner,
                // Pointer does not need to outlive the call, so we can safely pass a pointer to the stack variable.
                &target_config as *const _,
                std::ptr::null(),
                &mut info,
            )
        })?;

        // SAFETY: set() can only fail if a value was already stored, and this instance is only
        // accessible through self, which setup() consumes exactly once.
        self.instance
            ._flow_keep_alive
            .set(config.flow_writer.keep_alive())
            .ok();

        let ctx = self.instance.ctx.clone();

        if flow_config_info.is_discrete_flow() {
            Ok((
                TargetFlavor::Grain(Target {
                    instance: self.instance,
                    _marker: PhantomData,
                }),
                TargetInfo::new(ctx, info),
            ))
        } else {
            Ok((
                TargetFlavor::Samples(Target {
                    instance: self.instance,
                    _marker: PhantomData,
                }),
                TargetInfo::new(ctx, info),
            ))
        }
    }
}

/// Create a new target.
#[doc(hidden)]
pub(crate) fn create_target(ctx: Arc<FabricsInstanceContext>) -> Result<Target<Initializing>> {
    let mut target = FabricsTarget::default();
    unsafe {
        Error::from_status(ctx.api().fabrics_create_target(ctx.inner, &mut target))?;
    }
    if target.is_null() {
        return Err(Error::Other("Failed to create fabrics target.".to_string()));
    }

    Ok(Target::new(ctx.clone(), target))
}
