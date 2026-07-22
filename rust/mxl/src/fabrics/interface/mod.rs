use std::sync::Arc;

use crate::{
    Error,
    fabrics::{
        instance::FabricsInstanceContext,
        interface::config::{InterfaceConfig, OwnedInterfaceConfig},
    },
};

pub mod config;

pub struct Interfaces {
    ctx: Arc<FabricsInstanceContext>,
    inner: *mut mxl_sys::fabrics::FabricsInterfaceList,
}

impl Interfaces {
    pub(crate) fn get(
        ctx: Arc<FabricsInstanceContext>,
        query: Option<InterfaceConfig>,
    ) -> Result<Self, crate::Error> {
        let query_storage = match query {
            Some(q) => Some(OwnedInterfaceConfig::new(&q)?),
            None => None,
        };
        let query_ptr = query_storage
            .as_ref()
            .map_or(std::ptr::null(), |q| q.as_ffi() as *const _);

        let mut out_list: *mut mxl_sys::fabrics::FabricsInterfaceList = std::ptr::null_mut();

        Error::from_status(unsafe {
            ctx.api()
                .fabrics_get_interfaces(ctx.inner, query_ptr, &mut out_list)
        })?;

        Ok(Self {
            ctx,
            inner: out_list,
        })
    }
    pub fn iter(&self) -> InterfaceIter<'_> {
        InterfaceIter {
            it: self.inner,
            _marker: std::marker::PhantomData,
        }
    }
}

impl Drop for Interfaces {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe {
                self.ctx.api().fabrics_free_interface_list(
                    self.inner as *mut mxl_sys::fabrics::FabricsInterfaceList,
                );
            }
        }
    }
}

pub struct InterfaceIter<'a> {
    it: *mut mxl_sys::fabrics::FabricsInterfaceList,
    _marker: std::marker::PhantomData<&'a ()>,
}

impl<'a> Iterator for InterfaceIter<'a> {
    type Item = InterfaceConfig<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.it.is_null() {
            return None;
        }

        let iface = unsafe { &*self.it };
        let out = iface.interface.try_into().ok();

        self.it = iface.next;

        out
    }
}
