// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{ffi::CString, ptr::NonNull, sync::Arc};

use crate::{Error, FlowConfigInfo, GrainWriter, Result, SamplesWriter, instance::InstanceContext};

/// A wrapper around the MXL FlowWriter instance to manage its lifetime and ensure proper cleanup.
pub(crate) struct FlowWriterInstance {
    context: Arc<InstanceContext>,
    inner: NonNull<mxl_sys::FlowWriter_t>,
}
impl FlowWriterInstance {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        flow_def: &str,
        options: Option<&str>,
    ) -> Result<(Self, FlowConfigInfo, bool)> {
        let flow_def = CString::new(flow_def)?;
        let options = options.map(CString::new).transpose()?;
        let mut writer: mxl_sys::FlowWriter = std::ptr::null_mut();
        let mut info_unsafe = std::mem::MaybeUninit::<mxl_sys::FlowConfigInfo>::uninit();
        let mut was_created = false;

        Error::from_status(unsafe {
            context.api.create_flow_writer(
                context.instance,
                flow_def.as_ptr(),
                options.map(|cs| cs.as_ptr()).unwrap_or(std::ptr::null()),
                &mut writer,
                info_unsafe.as_mut_ptr(),
                &mut was_created,
            )
        })?;

        Ok((
            Self {
                context,
                inner: NonNull::new(writer)
                    .ok_or_else(|| Error::Other("Failed to create flow writer.".to_string()))?,
            },
            FlowConfigInfo {
                //SAFETY: The MXL API guarantees that the info is initialized if the function returns success.
                value: unsafe { info_unsafe.assume_init() },
            },
            was_created,
        ))
    }
    pub(crate) fn as_ptr(&self) -> mxl_sys::FlowWriter {
        self.inner.as_ptr()
    }
}
impl Drop for FlowWriterInstance {
    fn drop(&mut self) {
        if let Err(err) = Error::from_status(unsafe {
            self.context
                .api
                .release_flow_writer(self.context.instance, self.inner.as_ptr())
        }) {
            tracing::error!("Failed to release MXL flow writer: {:?}", err);
        }
    }
}

// SAFETY: The native writer may be moved between threads, but must not be accessed
// concurrently. Do not implement Sync for this type.
unsafe impl Send for FlowWriterInstance {}

/// Generic MXL Flow Writer, which can be further used to build either the "discrete" (grain-based
/// data like video frames or meta) or "continuous" (audio samples) flow writers in MXL terminology.
pub struct FlowWriter {
    context: Arc<InstanceContext>,
    writer: Arc<FlowWriterInstance>,
    info: FlowConfigInfo,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for FlowWriter {}

impl FlowWriter {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: FlowWriterInstance,
        info: FlowConfigInfo,
    ) -> Self {
        Self {
            context,
            // Arc keeps write accesses alive without making the native writer safe
            // for concurrent access.
            #[allow(clippy::arc_with_non_send_sync)]
            writer: Arc::new(writer),
            info,
        }
    }

    pub fn to_grain_writer(self) -> Result<GrainWriter> {
        if !self.info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to GrainWriter for continuous flow of type \"{:?}\".",
                self.info.common().data_format()
            )));
        }
        let result = GrainWriter::new(self.context.clone(), self.writer);
        Ok(result)
    }

    pub fn to_samples_writer(self) -> Result<SamplesWriter> {
        if self.info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to SamplesWriter for discrete flow of type \"{:?}\".",
                self.info.common().data_format()
            )));
        }
        let result = SamplesWriter::new(self.context.clone(), self.writer);
        Ok(result)
    }
}
