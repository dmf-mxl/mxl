// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Generic flow reader implementation.
//!
//! Provides [`FlowReader`], a type-erased reader that can be converted to either
//! [`crate::GrainReader`] or [`crate::SamplesReader`] based on the flow format.

use std::sync::Arc;

use crate::{
    DataFormat, Error, FlowConfigInfo, FlowRuntimeInfo, GrainReader, Result, SamplesReader,
    flow::{FlowInfo, is_discrete_data_format},
    instance::InstanceContext,
};

/// Generic flow reader handle.
///
/// This is the initial reader type returned by [`crate::MxlInstance::create_flow_reader`].
/// It must be converted to a typed reader ([`GrainReader`] or [`SamplesReader`]) using
/// the appropriate conversion method based on the flow's data format.
///
/// # Examples
///
/// ```no_run
/// # use mxl::MxlInstance;
/// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
/// let reader = instance.create_flow_reader("flow-uuid")?;
///
/// // Check flow type and convert to appropriate reader
/// if reader.get_info()?.config.is_discrete_flow() {
///     let grain_reader = reader.to_grain_reader()?;
///     // Use grain_reader for video/data
/// } else {
///     let samples_reader = reader.to_samples_reader()?;
///     // Use samples_reader for audio
/// }
/// # Ok(())
/// # }
/// ```
pub struct FlowReader {
    context: Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
}

// Safety: MXL readers are not thread-safe (no Sync), but can be transferred
// across threads (Send). Each reader should be used by only one thread at a time.
unsafe impl Send for FlowReader {}

/// Internal helper to query complete flow information from a reader.
pub(crate) fn get_flow_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<FlowInfo> {
    let mut flow_info: mxl_sys::FlowInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(context.api.flow_reader_get_info(reader, &mut flow_info))?;
    }
    Ok(FlowInfo {
        config: FlowConfigInfo {
            value: flow_info.config,
        },
        runtime: FlowRuntimeInfo {
            value: flow_info.runtime,
        },
    })
}

/// Internal helper to query flow configuration (without runtime info).
pub(crate) fn get_config_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<FlowConfigInfo> {
    let mut config_info: mxl_sys::FlowConfigInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(
            context
                .api
                .flow_reader_get_config_info(reader, &mut config_info),
        )?;
    }
    Ok(FlowConfigInfo { value: config_info })
}

/// Internal helper to query flow runtime information (head index, timestamps).
pub(crate) fn get_runtime_info(
    context: &Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
) -> Result<mxl_sys::FlowRuntimeInfo> {
    let mut runtime_info: mxl_sys::FlowRuntimeInfo = unsafe { std::mem::zeroed() };
    unsafe {
        Error::from_status(
            context
                .api
                .flow_reader_get_runtime_info(reader, &mut runtime_info),
        )?;
    }
    Ok(runtime_info)
}

impl FlowReader {
    /// Creates a new `FlowReader` from internal components (internal use only).
    pub(crate) fn new(context: Arc<InstanceContext>, reader: mxl_sys::FlowReader) -> Self {
        Self { context, reader }
    }

    /// Retrieves complete flow information (config + runtime).
    ///
    /// This is a relatively heavy operation. Consider using flow metadata from
    /// the writer creation if available, or query config/runtime separately.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader)
    }

    /// Converts this generic reader into a [`GrainReader`] for discrete flows.
    ///
    /// # Errors
    ///
    /// Returns an error if the flow is continuous (audio). Check the flow type
    /// with [`Self::get_info`] first if unsure.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let reader = instance.create_flow_reader("video-flow-uuid")?;
    /// let grain_reader = reader.to_grain_reader()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn to_grain_reader(mut self) -> Result<GrainReader> {
        let flow_type = self.get_info()?.config.value.common.format;
        if !is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to GrainReader for continuous flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = GrainReader::new(self.context.clone(), self.reader);
        self.reader = std::ptr::null_mut();
        Ok(result)
    }

    /// Converts this generic reader into a [`SamplesReader`] for continuous flows.
    ///
    /// # Errors
    ///
    /// Returns an error if the flow is discrete (video/data). Check the flow type
    /// with [`Self::get_info`] first if unsure.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let reader = instance.create_flow_reader("audio-flow-uuid")?;
    /// let samples_reader = reader.to_samples_reader()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn to_samples_reader(mut self) -> Result<SamplesReader> {
        let flow_type = self.get_info()?.config.value.common.format;
        if is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowReader to SamplesReader for discrete flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = SamplesReader::new(self.context.clone(), self.reader);
        self.reader = std::ptr::null_mut();
        Ok(result)
    }
}

impl Drop for FlowReader {
    /// Automatically releases the flow reader when dropped.
    fn drop(&mut self) {
        if !self.reader.is_null()
            && let Err(err) = Error::from_status(unsafe {
                self.context
                    .api
                    .release_flow_reader(self.context.instance, self.reader)
            })
        {
            tracing::error!("Failed to release MXL flow reader: {:?}", err);
        }
    }
}
