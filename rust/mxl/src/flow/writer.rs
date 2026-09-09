// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, ffi::CString, marker::PhantomData, ptr::NonNull, sync::Arc};

use crate::{Error, FlowConfigInfo, GrainWriter, Result, SamplesWriter, instance::InstanceContext};

/// A wrapper around the MXL FlowWriter instance to manage its lifetime and ensure proper cleanup.
pub(crate) struct FlowWriterResource {
    pub(crate) context: Arc<InstanceContext>,
    inner: NonNull<mxl_sys::FlowWriter_t>,
}
impl FlowWriterResource {
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
    /// # Safety
    ///
    /// This is only safe to be called by a single instance of FlowWriter, GrainWriter or SamplesWriter at a time.
    /// The underlying MXL FlowWriter is not thread-safe.
    pub(crate) unsafe fn as_ptr(&self) -> mxl_sys::FlowWriter {
        self.inner.as_ptr()
    }
    pub(crate) fn keep_alive(self: &Arc<Self>) -> FlowWriterResourceKeepAlive {
        FlowWriterResourceKeepAlive {
            _instance: self.clone(),
        }
    }
}
impl Drop for FlowWriterResource {
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

// SAFETY:
// This type is only used to manage the lifetime of the underlying MXL FlowWriter instance.
// No operation can be done directly, thus the type itself is thread-safe.
unsafe impl Send for FlowWriterResource {}
unsafe impl Sync for FlowWriterResource {}

/// A wrapper around FlowWriterResource to keep it alive. Users can't mutate the underlying pointer.
pub(crate) struct FlowWriterResourceKeepAlive {
    _instance: Arc<FlowWriterResource>,
}

/// Generic MXL Flow Writer, which can be further used to build either the "discrete" (grain-based
/// data like video frames or meta) or "continuous" (audio samples) flow writers in MXL terminology.
pub struct FlowWriter {
    writer: Arc<FlowWriterResource>,
    info: FlowConfigInfo,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL writer
                                      // is not thread-safe
}

impl FlowWriter {
    pub(crate) fn new(writer: FlowWriterResource, info: FlowConfigInfo) -> Self {
        Self {
            writer: Arc::new(writer),
            info,
            _not_sync: PhantomData,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowWriterResourceKeepAlive {
        self.writer.keep_alive()
    }

    pub fn to_grain_writer(self) -> Result<GrainWriter> {
        if !self.info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to GrainWriter for continuous flow of type \"{:?}\".",
                self.info.common().data_format()
            )));
        }
        let result = GrainWriter::new(self.writer);
        Ok(result)
    }

    pub fn to_samples_writer(self) -> Result<SamplesWriter> {
        if self.info.is_discrete_flow() {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to SamplesWriter for discrete flow of type \"{:?}\".",
                self.info.common().data_format()
            )));
        }
        let result = SamplesWriter::new(self.writer);
        Ok(result)
    }
}
