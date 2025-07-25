use std::{ffi::CString, sync::Arc};

use dlopen2::wrapper::Container;

use crate::{Error, FlowInfo, MxlApi, MxlFlowReader, MxlFlowWriter, Result};

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
    /// It is meant mainly for testing purposes.
    pub fn destroy(mut self) -> Result<()> {
        unsafe {
            let mut instance = std::ptr::null_mut();
            std::mem::swap(&mut self.instance, &mut instance);
            self.api.mxl_destroy_instance(self.instance)
        };
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

pub(crate) fn create_flow_reader(
    context: &Arc<InstanceContext>,
    flow_id: &str,
) -> Result<MxlFlowReader> {
    let flow_id = CString::new(flow_id)?;
    let options = CString::new("")?;
    let mut reader: mxl_sys::mxlFlowReader = std::ptr::null_mut();
    unsafe {
        Error::from_status(context.api.mxl_create_flow_reader(
            context.instance,
            flow_id.as_ptr(),
            options.as_ptr(),
            &mut reader,
        ))?;
    }
    if reader.is_null() {
        return Err(Error::Other("Failed to create flow reader.".to_string()));
    }
    Ok(MxlFlowReader::new(context.clone(), reader))
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
        create_flow_reader(&self.context, flow_id)
    }

    pub fn create_flow_writer(&self, flow_id: &str) -> Result<MxlFlowWriter> {
        let uuid = uuid::Uuid::parse_str(flow_id)
            .map_err(|_| Error::Other("Invalid flow ID format.".to_string()))?;
        let flow_id = CString::new(flow_id)?;
        let options = CString::new("")?;
        let mut writer: mxl_sys::mxlFlowWriter = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.mxl_create_flow_writer(
                self.context.instance,
                flow_id.as_ptr(),
                options.as_ptr(),
                &mut writer,
            ))?;
        }
        if writer.is_null() {
            return Err(Error::Other("Failed to create flow writer.".to_string()));
        }
        Ok(MxlFlowWriter::new(self.context.clone(), writer, uuid))
    }

    /// For now, we provide direct access to the MXL API for creating and
    /// destroying flows. Maybe it would be worth to provide RAII wrapper...
    /// Instead? As well?
    pub fn create_flow(&self, flow_def: &str, options: Option<&str>) -> Result<FlowInfo> {
        let flow_def = CString::new(flow_def)?;
        let options = CString::new(options.unwrap_or(""))?;
        let mut info = std::mem::MaybeUninit::<mxl_sys::FlowInfo>::uninit();

        unsafe {
            Error::from_status(self.context.api.mxl_create_flow(
                self.context.instance,
                flow_def.as_ptr(),
                options.as_ptr(),
                info.as_mut_ptr(),
            ))?;
        }

        let info = unsafe { info.assume_init() };
        Ok(FlowInfo { value: info })
    }

    /// See `create_flow` for more info.
    pub fn destroy_flow(&self, flow_id: &str) -> Result<()> {
        let flow_id = CString::new(flow_id)?;
        unsafe {
            Error::from_status(
                self.context
                    .api
                    .mxl_destroy_flow(self.context.instance, flow_id.as_ptr()),
            )?;
        }
        Ok(())
    }

    pub fn get_current_index(&self, rational: &mxl_sys::Rational) -> u64 {
        unsafe { self.context.api.mxl_get_current_index(rational) }
    }

    pub fn get_duration_until_index(
        &self,
        index: u64,
        rate: &mxl_sys::Rational,
    ) -> Result<std::time::Duration> {
        let duration_ns = unsafe { self.context.api.mxl_get_ns_until_index(index, rate) };
        if duration_ns == u64::MAX {
            Err(Error::Other(format!(
                "Failed to get duration until index, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(std::time::Duration::from_nanos(duration_ns))
        }
    }

    /// TODO: Make timestamp a strong type.
    pub fn timestamp_to_index(&self, timestamp: u64, rate: &mxl_sys::Rational) -> Result<u64> {
        let index = unsafe { self.context.api.mxl_timestamp_to_index(rate, timestamp) };
        if index == u64::MAX {
            Err(Error::Other(format!(
                "Failed to convert timestamp to index, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(index)
        }
    }

    pub fn index_to_timestamp(&self, index: u64, rate: &mxl_sys::Rational) -> Result<u64> {
        let timestamp = unsafe { self.context.api.mxl_index_to_timestamp(rate, index) };
        if timestamp == u64::MAX {
            Err(Error::Other(format!(
                "Failed to convert index to timestamp, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(timestamp)
        }
    }

    pub fn sleep_for(&self, duration: std::time::Duration) {
        unsafe {
            self.context
                .api
                .mxl_sleep_for_ns(duration.as_nanos() as u64)
        }
    }

    pub fn get_time(&self) -> u64 {
        unsafe { self.context.api.mxl_get_time() }
    }

    /// This function forces the destruction of the MXL instance.
    /// It is meant mainly for testing purposes.
    /// The caller must ensure that no other objects are using the MXL instance when this function
    /// is called.
    pub fn destroy(self) -> Result<()> {
        let context = Arc::into_inner(self.context)
            .ok_or_else(|| Error::Other("Instance is still in use.".to_string()))?;
        context.destroy()
    }
}
