use mxl_sys::fabrics::FabricsProvider;

use crate::error::{Error, Result};
use std::{ffi::CString, rc::Rc};

use crate::fabrics::instance::FabricsInstanceContext;

/// The provider corresponds to the transport used for transfers. This is created from a
/// [FabricsInstance](crate::fabrics::FabricsInstance).
#[derive(Clone)]
pub struct Provider {
    inner: ProviderType,
    ctx: Rc<FabricsInstanceContext>,
}

/// The available transports
#[derive(Clone)]
enum ProviderType {
    Auto,
    Tcp,
    Verbs,
    Efa,
    Shm,
}

impl From<mxl_sys::fabrics::FabricsProvider> for ProviderType {
    fn from(value: mxl_sys::fabrics::FabricsProvider) -> Self {
        match value {
            mxl_sys::fabrics::MXL_FABRICS_PROVIDER_AUTO => ProviderType::Auto,
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
            ProviderType::Auto => mxl_sys::fabrics::MXL_FABRICS_PROVIDER_AUTO,
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
    fn new(ctx: Rc<FabricsInstanceContext>, inner: FabricsProvider) -> Self {
        Self {
            inner: inner.into(),
            ctx,
        }
    }

    /// Convert a string to a fabrics provider enum value.
    /// Public visibility is set to crate only, because a `FabricsInstanceContext` is required.
    /// See [FabricsInstance](crate::FabricsInstance).
    pub(crate) fn from_str(ctx: Rc<FabricsInstanceContext>, s: &str) -> Result<Provider> {
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

        let prov: mxl_sys::fabrics::FabricsProvider = (&self.inner).into();

        Error::from_status(unsafe {
            self.ctx
                .api()
                .fabrics_provider_to_string(prov, std::ptr::null_mut(), &mut size)
        })?;

        // fabrics_provider_to_string already includes space for null terminator. So we must remove it here, because CString includes it.
        let out_string = unsafe { CString::from_vec_unchecked(vec![0; size - 1]) };

        Error::from_status(unsafe {
            self.ctx.api().fabrics_provider_to_string(
                prov,
                out_string.as_ptr() as *mut i8,
                &mut size,
            )
        })?;
        out_string
            .into_string()
            .map_err(|e| Error::Other(e.to_string()))
    }
}
