// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Error types for MXL operations.
//!
//! This module defines the error types returned by MXL API calls, mapping
//! C-level status codes to idiomatic Rust error enums.

/// Convenience result type using [`Error`] as the error variant.
pub type Result<T> = core::result::Result<T, Error>;

/// Errors that can occur when using the MXL API.
///
/// This enum maps MXL C status codes to Rust error variants and includes
/// additional error types for Rust-specific failures (e.g., loading the
/// dynamic library, string conversion).
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// An unknown or unrecognized MXL status code.
    #[error("Unknown error: {0}")]
    Unknown(mxl_sys::Status),

    /// The requested flow ID does not exist in the domain.
    #[error("Flow not found")]
    FlowNotFound,

    /// Attempted to read/write data that is no longer available in the ring buffer
    /// (has been overwritten by newer data).
    #[error("Out of range - too late")]
    OutOfRangeTooLate,

    /// Attempted to read/write data that is not yet available in the ring buffer
    /// (index is ahead of the current head).
    #[error("Out of range - too early")]
    OutOfRangeTooEarly,

    /// The flow reader handle is invalid or has been released.
    #[error("Invalid flow reader")]
    InvalidFlowReader,

    /// The flow writer handle is invalid or has been released.
    #[error("Invalid flow writer")]
    InvalidFlowWriter,

    /// A blocking operation timed out before completing.
    #[error("Timeout")]
    Timeout,

    /// An argument passed to an MXL function was invalid.
    #[error("Invalid argument")]
    InvalidArg,

    /// A resource conflict occurred (e.g., attempting to create a flow that already exists).
    #[error("Conflict")]
    Conflict,

    /// A generic error for Rust-level failures not directly mapped to MXL C errors.
    ///
    /// This variant wraps error messages from higher-level Rust operations
    /// (e.g., type mismatches, UTF-8 conversion failures).
    #[error("Other error: {0}")]
    Other(String),

    /// Failed to convert a Rust string to a C-compatible null-terminated string.
    #[error("Null string: {0}")]
    NulString(#[from] std::ffi::NulError),

    /// Failed to load or interact with the MXL dynamic library.
    #[error("Loading library: {0}")]
    LibLoading(#[from] libloading::Error),
}

impl Error {
    /// Converts an MXL C API status code to a Rust [`Result`].
    ///
    /// This internal helper maps integer status codes from the C API to
    /// strongly-typed [`Error`] variants.
    ///
    /// # Arguments
    ///
    /// * `status` - The raw status code returned by an MXL C function
    ///
    /// # Returns
    ///
    /// - `Ok(())` if `status == MXL_STATUS_OK`
    /// - `Err(Error::...)` for any error status code
    ///
    /// # Examples
    ///
    /// ```ignore
    /// let status = unsafe { api.some_mxl_function(...) };
    /// Error::from_status(status)?; // Propagate error if status != OK
    /// ```
    pub fn from_status(status: mxl_sys::Status) -> Result<()> {
        match status {
            mxl_sys::MXL_STATUS_OK => Ok(()),
            mxl_sys::MXL_ERR_UNKNOWN => Err(Error::Unknown(mxl_sys::MXL_ERR_UNKNOWN)),
            mxl_sys::MXL_ERR_FLOW_NOT_FOUND => Err(Error::FlowNotFound),
            mxl_sys::MXL_ERR_OUT_OF_RANGE_TOO_LATE => Err(Error::OutOfRangeTooLate),
            mxl_sys::MXL_ERR_OUT_OF_RANGE_TOO_EARLY => Err(Error::OutOfRangeTooEarly),
            mxl_sys::MXL_ERR_INVALID_FLOW_READER => Err(Error::InvalidFlowReader),
            mxl_sys::MXL_ERR_INVALID_FLOW_WRITER => Err(Error::InvalidFlowWriter),
            mxl_sys::MXL_ERR_TIMEOUT => Err(Error::Timeout),
            mxl_sys::MXL_ERR_INVALID_ARG => Err(Error::InvalidArg),
            mxl_sys::MXL_ERR_CONFLICT => Err(Error::Conflict),
            other => Err(Error::Unknown(other)),
        }
    }
}
