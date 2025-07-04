use std::{ffi::CString, sync::Arc};

use dlopen2::wrapper::Container;

use crate::{Error, MxlApi, MxlFlowReader, Result};

/// This struct stores the context that is shared by all objects.
/// It is separated out from `MxlInstance` so that it can be cloned
/// and other objects' lifetimes be decoupled from the MxlInstance
/// itself.
pub(crate) struct InstanceContext {
    pub(crate) api: Container<MxlApi>,
    pub(crate) instance: mxl_sys::mxlInstance,
}

// Allow sharing the context across threads and tasks freely.
unsafe impl Send for InstanceContext {}
unsafe impl Sync for InstanceContext {}

impl InstanceContext {
    /// This function forces the destruction of the MXL instance.
    /// It is not safe as other objects may still be using it.
    /// It is meant for testing purposes only.
    ///
    /// # Safety
    ///
    /// The caller must ensure that no other objects are using the MXL instance when this function is called.
    /// Calling this function while other references exist may lead to undefined behavior.
    pub unsafe fn destroy(&self) -> Result<()> {
        unsafe { self.api.mxl_destroy_instance(self.instance) };
        Ok(())
    }
}

impl Drop for InstanceContext {
    fn drop(&mut self) {
        if !self.instance.is_null() {
            unsafe { self.api.mxl_destroy_instance(self.instance) };
        }
    }
}

pub struct MxlInstance {
    context: Arc<InstanceContext>,
}

impl MxlInstance {
    pub fn new(api: Container<MxlApi>, domain: &str, options: &str) -> Result<Self> {
        let instance = unsafe {
            api.mxl_create_instance(
                CString::new(domain)?.as_ptr(),
                CString::new(options)?.as_ptr(),
            )
        };
        if instance.is_null() {
            Err(Error::Other("Failed to create MXL instance.".to_string()))
        } else {
            let context = Arc::new(InstanceContext { api, instance });
            Ok(Self { context })
        }
    }

    pub fn create_flow_reader(&self, flow_id: &str) -> Result<MxlFlowReader> {
        let flow_id = CString::new(flow_id)?;
        let options = CString::new("")?;
        let mut reader: mxl_sys::mxlFlowReader = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.mxl_create_flow_reader(
                self.context.instance,
                flow_id.as_ptr(),
                options.as_ptr(),
                &mut reader,
            ))?;
        }
        if reader.is_null() {
            return Err(Error::Other("Failed to create flow reader.".to_string()));
        }
        Ok(MxlFlowReader::new(self.context.clone(), reader))
    }

    pub fn get_current_head_index(&self, rational: &mxl_sys::Rational) -> u64 {
        unsafe { self.context.api.mxl_get_current_head_index(rational) }
    }

    /// This function forces the destruction of the MXL instance.
    /// It is not safe as other objects may still be using it.
    /// It is meant for testing purposes only.
    ///
    /// # Safety
    ///
    /// The caller must ensure that no other objects are using the MXL instance when this function is called.
    /// Calling this function while other references exist may lead to undefined behavior.
    pub unsafe fn destroy(self) -> Result<()> {
        unsafe { self.context.destroy() }
    }
}
