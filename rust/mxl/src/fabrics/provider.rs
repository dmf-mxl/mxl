// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use mxl_sys::fabrics::FabricsProvider;

use crate::error::{Error, Result};
use std::{ffi::CString, sync::Arc};

use crate::fabrics::instance::FabricsInstanceContext;

/// The provider corresponds to the transport used for transfers. This is created from a
/// [FabricsInstance](crate::fabrics::FabricsInstance).
#[derive(Clone)]
pub struct Provider {
    inner: ProviderType,
    ctx: Arc<FabricsInstanceContext>,
}
unsafe impl Send for Provider {}
/// SAFETY: Although the `FabricsInstanceContext` type as a whole is not thread-safe, the subset of functions that this `Provider type uses is thread-safe: `fabrics_provider_from_string` and `fabrics_provider_to_string`
unsafe impl Sync for Provider {}

/// The available transports
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProviderType {
    ///  Any provider. Currently useful as an input to `FabricsInstance::get_interfaces`
    Any,
    /// Provider that use linux TCP sockets.
    Tcp,
    /// Provider for userspace verbs (libibverbs and librdmacm)
    Verbs,
    /// Provider for AWS Elastic Fabric Adapter
    Efa,
    /// Provider used for moving data between 2 memory regions inside the same system.
    Shm,
}

impl From<mxl_sys::fabrics::FabricsProvider> for ProviderType {
    fn from(value: mxl_sys::fabrics::FabricsProvider) -> Self {
        match value {
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_ANY => ProviderType::Any,
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_TCP => ProviderType::Tcp,
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_VERBS => ProviderType::Verbs,
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_EFA => ProviderType::Efa,
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_SHM => ProviderType::Shm,
            _ => panic!("Unknown FabricsProvider value"),
        }
    }
}

impl From<&ProviderType> for mxl_sys::fabrics::FabricsProvider {
    fn from(value: &ProviderType) -> Self {
        match value {
            ProviderType::Any => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_ANY,
            ProviderType::Tcp => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_TCP,
            ProviderType::Verbs => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_VERBS,
            ProviderType::Efa => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_EFA,
            ProviderType::Shm => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_SHM,
        }
    }
}

impl From<&Provider> for mxl_sys::fabrics::FabricsProvider {
    fn from(value: &Provider) -> Self {
        (&value.inner).into()
    }
}

impl Provider {
    fn new(ctx: Arc<FabricsInstanceContext>, inner: FabricsProvider) -> Self {
        Self {
            inner: inner.into(),
            ctx,
        }
    }

    pub fn prov_type(&self) -> &ProviderType {
        &self.inner
    }

    /// Convert a string to a fabrics provider enum value.
    /// Public visibility is set to crate only, because a `FabricsInstanceContext` is required.
    /// See [FabricsInstance](crate::FabricsInstance).
    pub(crate) fn from_str(ctx: Arc<FabricsInstanceContext>, s: &str) -> Result<Provider> {
        let mut inner = FabricsProvider::default();

        Error::from_status(unsafe {
            ctx.api()
                .fabrics_provider_from_string(CString::new(s)?.as_ptr(), &mut inner)
        })?;

        Ok(Self::new(ctx, inner))
    }

    /// Convert a fabrics provider enum value to a string.
    pub fn to_string(&self) -> Result<String> {
        let mut size = 0;

        Error::from_status(unsafe {
            self.ctx.api().fabrics_provider_to_string(
                (&self.inner).into(),
                std::ptr::null_mut(),
                &mut size,
            )
        })?;

        let mut out_string = vec![0u8; size];
        Error::from_status(unsafe {
            self.ctx.api().fabrics_provider_to_string(
                (&self.inner).into(),
                out_string.as_mut_ptr() as *mut i8,
                &mut size,
            )
        })?;

        CString::from_vec_with_nul(out_string)
            .map_err(|e| Error::Other(e.to_string()))?
            .into_string()
            .map_err(|e| Error::Other(e.to_string()))
    }
}
