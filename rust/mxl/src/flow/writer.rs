// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Generic flow writer implementation.
//!
//! Provides [`FlowWriter`], a type-erased writer that can be converted to either
//! [`crate::GrainWriter`] or [`crate::SamplesWriter`] based on the flow format.

use std::sync::Arc;

use crate::{
    DataFormat, Error, GrainWriter, Result, SamplesWriter,
    flow::is_discrete_data_format,
    instance::{InstanceContext, create_flow_reader},
};

/// Generic flow writer handle.
///
/// This is the initial writer type returned by [`crate::MxlInstance::create_flow_writer`].
/// It must be converted to a typed writer ([`GrainWriter`] or [`SamplesWriter`]) using
/// the appropriate conversion method based on the flow's data format.
///
/// The writer takes ownership of the flow and is responsible for writing media data
/// to the shared memory ring buffer.
///
/// # Examples
///
/// ```no_run
/// # use mxl::MxlInstance;
/// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
/// let flow_def = r#"{"id": "...", "format": "urn:x-nmos:format:video", ...}"#;
/// let (writer, info, was_created) = instance.create_flow_writer(flow_def, None)?;
///
/// // Convert to appropriate typed writer based on flow format
/// if info.is_discrete_flow() {
///     let grain_writer = writer.to_grain_writer()?;
///     // Use grain_writer for video/data
/// } else {
///     let samples_writer = writer.to_samples_writer()?;
///     // Use samples_writer for audio
/// }
/// # Ok(())
/// # }
/// ```
pub struct FlowWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
    id: uuid::Uuid,
}

// Safety: MXL writers are not thread-safe (no Sync), but can be transferred
// across threads (Send). Each writer should be used by only one thread at a time.
unsafe impl Send for FlowWriter {}

impl FlowWriter {
    /// Creates a new flow writer (internal use only).
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::FlowWriter,
        id: uuid::Uuid,
    ) -> Self {
        Self {
            context,
            writer,
            id,
        }
    }

    /// Converts this generic writer into a [`GrainWriter`] for discrete flows.
    ///
    /// This consumes the `FlowWriter` and returns a typed writer for grain-based
    /// media (video frames, data packets).
    ///
    /// # Errors
    ///
    /// Returns an error if the flow is continuous (audio).
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let flow_def = r#"{"format": "urn:x-nmos:format:video", ...}"#;
    /// let (writer, _, _) = instance.create_flow_writer(flow_def, None)?;
    /// let grain_writer = writer.to_grain_writer()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn to_grain_writer(mut self) -> Result<GrainWriter> {
        let flow_type = self.get_flow_type()?;
        if !is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to GrainWriter for continuous flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = GrainWriter::new(self.context.clone(), self.writer);
        self.writer = std::ptr::null_mut();
        Ok(result)
    }

    /// Converts this generic writer into a [`SamplesWriter`] for continuous flows.
    ///
    /// This consumes the `FlowWriter` and returns a typed writer for sample-based
    /// media (audio).
    ///
    /// # Errors
    ///
    /// Returns an error if the flow is discrete (video/data).
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let flow_def = r#"{"format": "urn:x-nmos:format:audio", ...}"#;
    /// let (writer, _, _) = instance.create_flow_writer(flow_def, None)?;
    /// let samples_writer = writer.to_samples_writer()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn to_samples_writer(mut self) -> Result<SamplesWriter> {
        let flow_type = self.get_flow_type()?;
        if is_discrete_data_format(flow_type) {
            return Err(Error::Other(format!(
                "Cannot convert FlowWriter to SamplesWriter for discrete flow of type \"{:?}\".",
                DataFormat::from(flow_type)
            )));
        }
        let result = SamplesWriter::new(self.context.clone(), self.writer);
        self.writer = std::ptr::null_mut();
        Ok(result)
    }

    /// Queries the flow's data format by temporarily creating a reader.
    ///
    /// This is an internal workaround: the MXL C API doesn't provide a direct way
    /// to query flow type from a writer, so we create a temporary reader to fetch
    /// the flow metadata.
    fn get_flow_type(&self) -> Result<u32> {
        let reader = create_flow_reader(&self.context, &self.id.to_string()).map_err(|error| {
            Error::Other(format!(
                "Error while creating flow reader to get the flow type: {error}"
            ))
        })?;
        let flow_info = reader.get_info().map_err(|error| {
            Error::Other(format!(
                "Error while getting flow type from temporary reader: {error}"
            ))
        })?;
        Ok(flow_info.config.value.common.format)
    }
}

impl Drop for FlowWriter {
    /// Automatically releases the flow writer when dropped.
    fn drop(&mut self) {
        if !self.writer.is_null()
            && let Err(err) = Error::from_status(unsafe {
                self.context
                    .api
                    .release_flow_writer(self.context.instance, self.writer)
            })
        {
            tracing::error!("Failed to release MXL flow writer: {:?}", err);
        }
    }
}
