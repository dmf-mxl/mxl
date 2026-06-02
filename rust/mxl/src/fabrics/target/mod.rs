mod config;
mod grain;
mod samples;

use std::{marker::PhantomData, sync::Arc};

use crate::{
    FlowConfigInfo,
    error::{Error, Result},
    fabrics::{instance::FabricsInstanceContext, target_info::TargetInfo},
};
pub use config::Config;

use states::*;

pub mod states {
    /// Used to create a new target
    pub struct New {}

    /// Waiting for the target to be initialized with the setup function
    pub struct Initializing {}

    /// The setup function has been called, but the target has not yet been specialized into a
    /// grain or samples target
    pub struct Specializing {}

    /// The target has been specialized into a grain target. It can only receive grains
    pub struct Grain {}

    /// The target has been specialized into a samples target. It can only receive samples
    pub struct Sample {}

    impl TargetState for New {}
    impl TargetState for Initializing {}
    impl TargetState for Specializing {}
    impl TargetState for Grain {}
    impl TargetState for Sample {}

    pub trait TargetState {}
}

/// Wrapper class that holds a reference count to the Fabrics Instance and the actual target
/// instance.
pub struct TargetInstance {
    ctx: Arc<FabricsInstanceContext>,
    inner: mxl_sys::fabrics::FabricsTarget,
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

pub enum Either {
    Grain(Target<Grain>),
    Sample(Target<Sample>),
}

impl Target<New> {
    pub(crate) fn new(
        ctx: Arc<FabricsInstanceContext>,
        target: mxl_sys::fabrics::FabricsTarget,
    ) -> Target<Initializing> {
        let instance = TargetInstance { ctx, inner: target };
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
    pub fn setup(self, config: &Config) -> Result<(Target<Specializing>, TargetInfo)> {
        let mut info = mxl_sys::fabrics::FabricsTargetInfo::default();
        Error::from_status(unsafe {
            self.instance.ctx.api().fabrics_target_setup(
                self.instance.inner,
                &config.try_into()?,
                std::ptr::null(),
                &mut info,
            )
        })?;

        let ctx = self.instance.ctx.clone();

        Ok((
            Target {
                instance: self.instance,
                _marker: PhantomData,
            },
            TargetInfo::new(ctx, info),
        ))
    }
}

impl Target<Specializing> {
    /// Specialize the target into a concrete grain or samples target
    pub fn specialize(self, flow_config: &FlowConfigInfo) -> Either {
        if flow_config.is_discrete_flow() {
            Either::Grain(Target {
                instance: self.instance,
                _marker: PhantomData,
            })
        } else {
            Either::Sample(Target {
                instance: self.instance,
                _marker: PhantomData,
            })
        }
    }
}

/// Create a new target.
#[doc(hidden)]
pub(crate) fn create_target(ctx: Arc<FabricsInstanceContext>) -> Result<Target<Initializing>> {
    let mut target = mxl_sys::fabrics::FabricsTarget::default();
    unsafe {
        Error::from_status(ctx.api().fabrics_create_target(ctx.inner, &mut target))?;
    }
    if target.is_null() {
        return Err(Error::Other("Failed to create fabrics target.".to_string()));
    }

    Ok(Target::new(ctx.clone(), target))
}
