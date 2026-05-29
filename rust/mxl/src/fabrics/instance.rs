use std::rc::Rc;
use std::sync::Arc;

use crate::{
    api::MxlFabricsAPiHandle,
    error::{Error, Result},
    fabrics::{
        initiator::{self, Initiator, create_initiator},
        provider::Provider,
        target::{self, Target, create_target},
        target_info::TargetInfo,
    },
    instance::InstanceContext,
};

pub(crate) fn create_instance(
    ctx: &Arc<InstanceContext>,
    fabrics_api: &MxlFabricsAPiHandle,
) -> Result<FabricsInstance> {
    let mut inst = std::ptr::null_mut();
    unsafe {
        Error::from_status(fabrics_api.fabrics_create_instance(
            std::mem::transmute::<*mut mxl_sys::Instance_t, *mut mxl_sys::fabrics::Instance_t>(
                ctx.instance,
            ),
            std::ptr::null(), // Unused for now
            &mut inst,
        ))?;
    }
    if inst.is_null() {
        return Err(Error::Other(
            "Failed to create fabrics instance.".to_string(),
        ));
    }

    let ctx = Rc::new(FabricsInstanceContext {
        _parent_ctx: ctx.clone(),
        api: fabrics_api.clone(),
        inner: inst,
    });

    Ok(FabricsInstance::new(ctx))
}

pub(crate) struct FabricsInstanceContext {
    _parent_ctx: Arc<InstanceContext>,
    api: MxlFabricsAPiHandle,
    pub(crate) inner: mxl_sys::fabrics::FabricsInstance,
}

impl FabricsInstanceContext {
    pub(crate) fn api(&self) -> &MxlFabricsAPiHandle {
        &self.api
    }
}

impl Drop for FabricsInstanceContext {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe {
                let _ = self.api.fabrics_destroy_instance(self.inner);
            }
        }
    }
}

/// This is just a factory type for creating Fabrics related objects such as Targets, Initiators, etc.
/// The fabrics instance and its pointer are held in the `FabricsInstanceContext`` object.
/// This is created via an [MxlInstance](crate::MxlInstance).
pub struct FabricsInstance {
    ctx: Rc<FabricsInstanceContext>,
}
impl FabricsInstance {
    fn new(ctx: Rc<FabricsInstanceContext>) -> Self {
        Self { ctx }
    }

    /// Create a fabrics target. The target is the receiver of write operations from an initiator.
    pub fn create_target(&self) -> Result<Target<target::states::Initializing>> {
        create_target(&self.ctx)
    }

    /// Create a fabrics initiator instance.
    pub fn create_initiator(&self) -> Result<Initiator<initiator::states::Initializing>> {
        create_initiator(&self.ctx)
    }

    pub fn provider_from_str(&self, provider: &str) -> Result<Provider> {
        Provider::from_str(self.ctx.clone(), provider)
    }

    pub fn target_info_from_str(&self, target_info: &str) -> Result<TargetInfo> {
        TargetInfo::from_str(self.ctx.clone(), target_info)
    }
}
