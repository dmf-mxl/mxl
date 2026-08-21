// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

pub type Result<T> = core::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Unknown error: {0}")]
    Unknown(mxl_sys::Status),
    #[error("Flow not found")]
    FlowNotFound,
    #[error("Out of range - too late")]
    OutOfRangeTooLate,
    #[error("Out of range - too early")]
    OutOfRangeTooEarly,
    #[error("Invalid flow reader")]
    InvalidFlowReader,
    #[error("Invalid flow writer")]
    InvalidFlowWriter,
    #[error("Timeout")]
    Timeout,
    #[error("Invalid argument")]
    InvalidArg,
    #[error("Conflict")]
    Conflict,

    // Fabrics errors
    #[error("String length exceeds buffer size")]
    Strlen,
    #[error("Interrupted")]
    Interrupted,
    #[error("No fabric available")]
    NoFabric,
    #[error("Invalid state")]
    InvalidState,
    #[error("Internal error")]
    Internal,
    #[error("Not ready")]
    NotReady,
    #[error("Not found")]
    NotFound,
    #[error("Already exists")]
    Exists,
    #[error("Unsupported operation")]
    UnsupportedOperation,

    /// The error is not defined in the MXL API, but it is used to wrap other errors.
    #[error("Other error: {0}")]
    Other(String),

    #[error("Null string: {0}")]
    NulString(#[from] std::ffi::NulError),

    #[error("Loading library: {0}")]
    LibLoading(#[from] libloading::Error),
}

impl Error {
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

            // fabrics errors
            mxl_sys::MXL_ERR_STRLEN => Err(Error::Strlen),
            mxl_sys::MXL_ERR_INTERRUPTED => Err(Error::Interrupted),
            mxl_sys::MXL_ERR_NO_FABRIC => Err(Error::NoFabric),
            mxl_sys::MXL_ERR_INVALID_STATE => Err(Error::InvalidState),
            mxl_sys::MXL_ERR_INTERNAL => Err(Error::Internal),
            mxl_sys::MXL_ERR_NOT_READY => Err(Error::NotReady),
            mxl_sys::MXL_ERR_NOT_FOUND => Err(Error::NotFound),
            mxl_sys::MXL_ERR_EXISTS => Err(Error::Exists),
            mxl_sys::MXL_ERR_UNSUPPORTED_OPERATION => Err(Error::UnsupportedOperation),

            other => Err(Error::Unknown(other)),
        }
    }
}
