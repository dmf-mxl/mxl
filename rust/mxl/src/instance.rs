// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! MXL instance management and core domain operations.
//!
//! This module provides [`MxlInstance`], the main entry point for interacting
//! with an MXL domain. An instance represents a connection to a shared memory
//! domain and provides methods to create readers/writers and manage timing.

use std::{ffi::CString, sync::Arc};

use crate::{Error, FlowConfigInfo, FlowReader, FlowWriter, Result, api::MxlApiHandle};

/// Internal shared context for an MXL instance.
///
/// This struct stores the raw C API handle and instance pointer, separated
/// from [`MxlInstance`] to allow readers/writers to outlive the instance and
/// to enable cloning for shared ownership across threads.
///
/// The context is automatically released when the last reference is dropped.
pub(crate) struct InstanceContext {
    pub(crate) api: MxlApiHandle,
    pub(crate) instance: mxl_sys::Instance,
}

// Safety: The MXL C API guarantees thread-safety at the instance level
// (but NOT at the reader/writer level). Multiple threads can safely share
// an InstanceContext to access different flows or timing functions.
unsafe impl Send for InstanceContext {}
unsafe impl Sync for InstanceContext {}

impl InstanceContext {
    /// Forces immediate destruction of the MXL instance.
    ///
    /// This consumes the context and explicitly destroys the underlying C instance.
    /// Normally destruction happens automatically via `Drop`, but this method allows
    /// explicit control for testing or cleanup scenarios.
    ///
    /// # Errors
    ///
    /// Returns an error if the C API fails to destroy the instance.
    pub fn destroy(mut self) -> Result<()> {
        unsafe {
            let mut instance = std::ptr::null_mut();
            std::mem::swap(&mut self.instance, &mut instance);
            Error::from_status(self.api.destroy_instance(instance))
        }
    }
}

impl Drop for InstanceContext {
    /// Automatically destroys the MXL C instance when the last reference is dropped.
    fn drop(&mut self) {
        if !self.instance.is_null() {
            unsafe { self.api.destroy_instance(self.instance) };
        }
    }
}

/// Creates a flow reader for the specified flow ID.
///
/// This internal helper is used by both [`MxlInstance::create_flow_reader`] and
/// [`crate::FlowWriter::get_flow_type`] (which temporarily creates a reader to
/// query flow metadata).
///
/// # Arguments
///
/// * `context` - Shared instance context
/// * `flow_id` - UUID string identifying the flow
///
/// # Returns
///
/// A [`FlowReader`] connected to the specified flow.
///
/// # Errors
///
/// Returns [`Error::FlowNotFound`] if the flow does not exist in the domain.
pub(crate) fn create_flow_reader(
    context: &Arc<InstanceContext>,
    flow_id: &str,
) -> Result<FlowReader> {
    let flow_id = CString::new(flow_id)?;
    let options = CString::new("")?;
    let mut reader: mxl_sys::FlowReader = std::ptr::null_mut();
    unsafe {
        Error::from_status(context.api.create_flow_reader(
            context.instance,
            flow_id.as_ptr(),
            options.as_ptr(),
            &mut reader,
        ))?;
    }
    if reader.is_null() {
        return Err(Error::Other("Failed to create flow reader.".to_string()));
    }
    Ok(FlowReader::new(context.clone(), reader))
}

/// Main entry point for interacting with an MXL domain.
///
/// An `MxlInstance` represents a connection to a shared memory domain (typically
/// a tmpfs directory like `/dev/shm/my_domain`). It provides methods to:
///
/// - Create flow readers and writers
/// - Query and manipulate timing (TAI timestamps and indices)
/// - Sleep with MXL's high-precision timing
///
/// The instance is cheaply cloneable and thread-safe (`Send + Sync`), but readers
/// and writers created from it are not thread-safe and should not be shared.
///
/// # Examples
///
/// ```no_run
/// use mxl::{load_api, MxlInstance};
///
/// # fn main() -> Result<(), mxl::Error> {
/// let api = load_api("libmxl.so")?;
/// let instance = MxlInstance::new(api, "/dev/shm/my_domain", "")?;
///
/// // Create a flow reader
/// let reader = instance.create_flow_reader("flow-uuid")?;
///
/// // Query current time
/// let tai_ns = instance.get_time();
/// println!("Current TAI: {}", tai_ns);
/// # Ok(())
/// # }
/// ```
#[derive(Clone)]
pub struct MxlInstance {
    context: Arc<InstanceContext>,
}

impl MxlInstance {
    /// Creates a new MXL instance bound to the specified domain.
    ///
    /// This establishes a connection to an MXL domain (a tmpfs directory containing
    /// shared memory for flows). If the domain does not exist, this function will fail.
    ///
    /// # Arguments
    ///
    /// * `api` - Shared handle to the loaded MXL C library (from [`crate::load_api`])
    /// * `domain` - Filesystem path to the domain directory (e.g., `/dev/shm/my_domain`)
    /// * `options` - Optional configuration string (typically empty `""`)
    ///
    /// # Returns
    ///
    /// A new instance connected to the domain.
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - The domain path does not exist or is not a valid MXL domain
    /// - The MXL C API fails to create the instance
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use mxl::{load_api, MxlInstance};
    ///
    /// # fn main() -> Result<(), mxl::Error> {
    /// let api = load_api("libmxl.so")?;
    /// let instance = MxlInstance::new(api, "/dev/shm/my_domain", "")?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn new(api: MxlApiHandle, domain: &str, options: &str) -> Result<Self> {
        let instance = unsafe {
            api.create_instance(
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

    /// Creates a flow reader for an existing flow in the domain.
    ///
    /// This connects to a flow that was previously created by a writer. The returned
    /// [`FlowReader`] is a generic reader that must be converted to either a
    /// [`crate::GrainReader`] (for discrete flows) or [`crate::SamplesReader`]
    /// (for continuous flows) using the appropriate `to_*` method.
    ///
    /// # Arguments
    ///
    /// * `flow_id` - UUID string identifying the flow (e.g., from flow metadata)
    ///
    /// # Returns
    ///
    /// A generic flow reader that can be converted to a typed reader.
    ///
    /// # Errors
    ///
    /// Returns [`Error::FlowNotFound`] if no flow with the given ID exists in the domain.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let reader = instance.create_flow_reader("abc-123-uuid")?;
    /// let grain_reader = reader.to_grain_reader()?; // Convert to typed reader
    /// # Ok(())
    /// # }
    /// ```
    pub fn create_flow_reader(&self, flow_id: &str) -> Result<FlowReader> {
        create_flow_reader(&self.context, flow_id)
    }

    /// Creates a flow writer from a JSON flow definition.
    ///
    /// This creates or opens a flow in the domain based on a JSON flow definition
    /// (following NMOS IS-04 flow schema). If a flow with the same ID already exists,
    /// it is reused instead of creating a new one.
    ///
    /// The returned [`FlowWriter`] is generic and must be converted to either
    /// [`crate::GrainWriter`] or [`crate::SamplesWriter`] based on the flow type.
    ///
    /// # Arguments
    ///
    /// * `flow_def` - JSON string defining the flow (format, rate, dimensions, etc.)
    /// * `options` - Optional configuration string (typically `None`)
    ///
    /// # Returns
    ///
    /// A tuple containing:
    /// - `FlowWriter`: Generic writer handle
    /// - `FlowConfigInfo`: Flow configuration metadata
    /// - `bool`: `true` if a new flow was created, `false` if reusing an existing flow
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - The flow definition JSON is invalid
    /// - The MXL C API fails to create the writer
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let flow_def = r#"{"id": "...", "format": "urn:x-nmos:format:video", ...}"#;
    /// let (writer, info, was_created) = instance.create_flow_writer(flow_def, None)?;
    ///
    /// if was_created {
    ///     println!("Created new flow");
    /// } else {
    ///     println!("Reusing existing flow");
    /// }
    ///
    /// let grain_writer = writer.to_grain_writer()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn create_flow_writer(
        &self,
        flow_def: &str,
        options: Option<&str>,
    ) -> Result<(FlowWriter, FlowConfigInfo, bool)> {
        let flow_def = CString::new(flow_def)?;
        let options = options.map(CString::new).transpose()?;
        let mut writer: mxl_sys::FlowWriter = std::ptr::null_mut();
        let mut info_unsafe = std::mem::MaybeUninit::<mxl_sys::FlowConfigInfo>::uninit();
        let mut was_created = false;
        unsafe {
            Error::from_status(self.context.api.create_flow_writer(
                self.context.instance,
                flow_def.as_ptr(),
                options.map(|cs| cs.as_ptr()).unwrap_or(std::ptr::null()),
                &mut writer,
                info_unsafe.as_mut_ptr(),
                &mut was_created,
            ))?;
        }
        if writer.is_null() {
            return Err(Error::Other("Failed to create flow writer.".to_string()));
        }

        let info = unsafe { info_unsafe.assume_init() };

        Ok((
            FlowWriter::new(
                self.context.clone(),
                writer,
                uuid::Uuid::from_bytes(info.common.id),
            ),
            FlowConfigInfo { value: info },
            was_created,
        ))
    }

    /// Retrieves the JSON flow definition for an existing flow.
    ///
    /// This queries the domain to get the original JSON flow definition that was
    /// used to create a flow. Useful for inspecting flow metadata or replicating
    /// flow configurations.
    ///
    /// # Arguments
    ///
    /// * `flow_id` - UUID string identifying the flow
    ///
    /// # Returns
    ///
    /// The flow definition as a JSON string.
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - The flow does not exist ([`Error::FlowNotFound`])
    /// - The returned data is not valid UTF-8
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let flow_def_json = instance.get_flow_def("abc-123-uuid")?;
    /// println!("Flow definition: {}", flow_def_json);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_flow_def(&self, flow_id: &str) -> Result<String> {
        let flow_id = CString::new(flow_id)?;
        const INITIAL_BUFFER_SIZE: usize = 4096;
        let mut buffer: Vec<u8> = vec![0; INITIAL_BUFFER_SIZE];
        let mut buffer_size = INITIAL_BUFFER_SIZE;

        let status = unsafe {
            self.context.api.get_flow_def(
                self.context.instance,
                flow_id.as_ptr(),
                buffer.as_mut_ptr() as *mut std::os::raw::c_char,
                &mut buffer_size,
            )
        };

        if status == mxl_sys::MXL_ERR_INVALID_ARG && buffer_size > INITIAL_BUFFER_SIZE {
            buffer = vec![0; buffer_size];
            unsafe {
                Error::from_status(self.context.api.get_flow_def(
                    self.context.instance,
                    flow_id.as_ptr(),
                    buffer.as_mut_ptr() as *mut std::os::raw::c_char,
                    &mut buffer_size,
                ))?;
            }
        } else {
            Error::from_status(status)?;
        }

        if buffer_size > 0 && buffer[buffer_size - 1] == 0 {
            buffer_size -= 1;
        }
        buffer.truncate(buffer_size);

        String::from_utf8(buffer)
            .map_err(|_| Error::Other("Invalid UTF-8 in flow definition".to_string()))
    }

    /// Returns the current media index for a given rate.
    ///
    /// MXL uses index-based addressing where each grain or sample batch is assigned
    /// a monotonically increasing index. This function computes the current index
    /// based on the system TAI time and the specified rate.
    ///
    /// # Arguments
    ///
    /// * `rational` - Frame rate (for video) or sample rate (for audio) as a rational number
    ///
    /// # Returns
    ///
    /// The current index at the present moment.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::{MxlInstance, Rational};
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let rate = Rational { numerator: 30000, denominator: 1001 }; // 29.97 fps
    /// let index = instance.get_current_index(&rate);
    /// println!("Current frame index: {}", index);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_current_index(&self, rational: &mxl_sys::Rational) -> u64 {
        unsafe { self.context.api.get_current_index(rational) }
    }

    /// Calculates the duration until a future index is reached.
    ///
    /// Given a target index and rate, computes how long to wait until that index
    /// becomes current. Useful for pacing writers to avoid writing too far ahead.
    ///
    /// # Arguments
    ///
    /// * `index` - Target index to wait for
    /// * `rate` - Frame or sample rate
    ///
    /// # Returns
    ///
    /// A [`Duration`] representing the wait time, or an error if the rate is invalid.
    ///
    /// # Errors
    ///
    /// Returns an error if the rate is invalid (e.g., zero denominator).
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::{MxlInstance, Rational};
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let rate = Rational { numerator: 48000, denominator: 1 };
    /// let next_index = instance.get_current_index(&rate) + 480; // 10ms ahead
    /// let wait_time = instance.get_duration_until_index(next_index, &rate)?;
    /// instance.sleep_for(wait_time);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_duration_until_index(
        &self,
        index: u64,
        rate: &mxl_sys::Rational,
    ) -> Result<std::time::Duration> {
        let duration_ns = unsafe { self.context.api.get_ns_until_index(index, rate) };
        if duration_ns == u64::MAX {
            Err(Error::Other(format!(
                "Failed to get duration until index, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(std::time::Duration::from_nanos(duration_ns))
        }
    }

    /// Converts a TAI timestamp to a media index.
    ///
    /// Given a TAI timestamp (nanoseconds since SMPTE ST 2059 epoch) and a rate,
    /// computes the corresponding media index. This is the inverse of
    /// [`Self::index_to_timestamp`].
    ///
    /// # Arguments
    ///
    /// * `timestamp` - TAI timestamp in nanoseconds
    /// * `rate` - Frame or sample rate
    ///
    /// # Returns
    ///
    /// The media index corresponding to the timestamp.
    ///
    /// # Errors
    ///
    /// Returns an error if the rate is invalid.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::{MxlInstance, Rational};
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let rate = Rational { numerator: 60, denominator: 1 };
    /// let tai_ns = instance.get_time();
    /// let index = instance.timestamp_to_index(tai_ns, &rate)?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn timestamp_to_index(&self, timestamp: u64, rate: &mxl_sys::Rational) -> Result<u64> {
        let index = unsafe { self.context.api.timestamp_to_index(rate, timestamp) };
        if index == u64::MAX {
            Err(Error::Other(format!(
                "Failed to convert timestamp to index, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(index)
        }
    }

    /// Converts a media index to a TAI timestamp.
    ///
    /// Given an index and rate, computes the TAI timestamp (nanoseconds since
    /// SMPTE ST 2059 epoch) when that index becomes current. This is the inverse
    /// of [`Self::timestamp_to_index`].
    ///
    /// # Arguments
    ///
    /// * `index` - Media index (frame number or sample number)
    /// * `rate` - Frame or sample rate
    ///
    /// # Returns
    ///
    /// TAI timestamp in nanoseconds.
    ///
    /// # Errors
    ///
    /// Returns an error if the rate is invalid.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::{MxlInstance, Rational};
    /// # fn example(instance: MxlInstance) -> Result<(), mxl::Error> {
    /// let rate = Rational { numerator: 30, denominator: 1 };
    /// let index = 1000; // Frame 1000
    /// let tai_ns = instance.index_to_timestamp(index, &rate)?;
    /// println!("Frame {} occurs at TAI {}", index, tai_ns);
    /// # Ok(())
    /// # }
    /// ```
    pub fn index_to_timestamp(&self, index: u64, rate: &mxl_sys::Rational) -> Result<u64> {
        let timestamp = unsafe { self.context.api.index_to_timestamp(rate, index) };
        if timestamp == u64::MAX {
            Err(Error::Other(format!(
                "Failed to convert index to timestamp, invalid rate {}/{}.",
                rate.numerator, rate.denominator
            )))
        } else {
            Ok(timestamp)
        }
    }

    /// Sleeps for the specified duration using MXL's high-precision timing.
    ///
    /// This uses the MXL timing system (TAI-based) rather than the OS scheduler,
    /// providing more accurate timing for media pacing operations.
    ///
    /// # Arguments
    ///
    /// * `duration` - How long to sleep
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # use std::time::Duration;
    /// # fn example(instance: MxlInstance) {
    /// // Sleep for exactly 16.67ms (one frame at ~60 fps)
    /// instance.sleep_for(Duration::from_micros(16667));
    /// # }
    /// ```
    pub fn sleep_for(&self, duration: std::time::Duration) {
        unsafe { self.context.api.sleep_for_ns(duration.as_nanos() as u64) }
    }

    /// Returns the current TAI time in nanoseconds.
    ///
    /// TAI (International Atomic Time) is the time standard used by MXL, following
    /// SMPTE ST 2059. Unlike UTC, TAI does not have leap seconds.
    ///
    /// The epoch is 1970-01-01 00:00:00 TAI.
    ///
    /// # Returns
    ///
    /// Current TAI time in nanoseconds since epoch.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::MxlInstance;
    /// # fn example(instance: MxlInstance) {
    /// let tai_ns = instance.get_time();
    /// println!("Current TAI: {} ns", tai_ns);
    /// # }
    /// ```
    pub fn get_time(&self) -> u64 {
        unsafe { self.context.api.get_time() }
    }

    /// Forces immediate destruction of the MXL instance.
    ///
    /// This explicitly destroys the underlying C instance, consuming `self`.
    /// Normally the instance is destroyed automatically when all references are
    /// dropped, but this method is useful for testing or explicit cleanup.
    ///
    /// # Safety
    ///
    /// The caller must ensure that no readers, writers, or other objects are
    /// still using this instance when this is called. Violating this may cause
    /// use-after-free errors.
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - There are still outstanding references to the instance context
    /// - The MXL C API fails to destroy the instance
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::{load_api, MxlInstance};
    /// # fn example() -> Result<(), mxl::Error> {
    /// let api = load_api("libmxl.so")?;
    /// let instance = MxlInstance::new(api, "/dev/shm/test", "")?;
    /// // ... use instance ...
    /// instance.destroy()?; // Explicit cleanup
    /// # Ok(())
    /// # }
    /// ```
    pub fn destroy(self) -> Result<()> {
        let context = Arc::into_inner(self.context)
            .ok_or_else(|| Error::Other("Instance is still in use.".to_string()))?;
        context.destroy()
    }
}
