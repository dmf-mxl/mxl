/// Capabilities of the fabric interface
#[derive(Debug)]
pub struct Capabilities {
    blocking_operations: bool,
    remote_write: bool,
    send_recv: bool,

    max_message_size: u64,
}
impl Capabilities {
    pub fn builder() -> CapabilitiesBuilder {
        CapabilitiesBuilder::default()
    }

    /// The interface supports blocking operations.
    pub fn supports_blocking_operations(&self) -> bool {
        self.blocking_operations
    }

    /// The interface supports remote-write (RDMA) operations.
    pub fn supports_remote_write(&self) -> bool {
        self.remote_write
    }

    /// The interface supports send/receive message operations.
    pub fn supports_send_recv(&self) -> bool {
        self.send_recv
    }

    /// Maximum message size supported on this interface.
    pub fn max_message_size(&self) -> u64 {
        self.max_message_size
    }
}
impl Default for Capabilities {
    fn default() -> Self {
        Self {
            blocking_operations: true,
            remote_write: true,
            send_recv: false,
            max_message_size: u64::MAX,
        }
    }
}
impl From<&Capabilities> for mxl_sys::fabrics::FabricsInterfaceCaps {
    fn from(value: &Capabilities) -> Self {
        let flags = (if value.blocking_operations {
            mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS as u64
        } else {
            0
        }) | (if value.remote_write {
            mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_REMOTE_WRITE as u64
        } else {
            0
        }) | (if value.send_recv {
            mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_SEND_RECEIVE as u64
        } else {
            0
        });

        Self {
            version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
            flags,
            maxMessageSize: value.max_message_size,
        }
    }
}
impl From<Capabilities> for mxl_sys::fabrics::FabricsInterfaceCaps {
    fn from(value: Capabilities) -> Self {
        (&value).into()
    }
}
impl From<mxl_sys::fabrics::FabricsInterfaceCaps> for Capabilities {
    fn from(value: mxl_sys::fabrics::FabricsInterfaceCaps) -> Self {
        let flags = value.flags;
        Self {
            blocking_operations: (flags
                & mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS as u64)
                != 0,
            remote_write: (flags & mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_REMOTE_WRITE as u64)
                != 0,
            send_recv: (flags & mxl_sys::fabrics::MXL_FABRICS_IFACE_CAP_SEND_RECEIVE as u64) != 0,
            max_message_size: value.maxMessageSize,
        }
    }
}

pub struct CapabilitiesBuilder {
    // flags for the capabilities of the fabric
    blocking_operations: bool,
    remote_write: bool,
    send_recv: bool,

    max_message_size: u64,
}
impl CapabilitiesBuilder {
    pub fn with_blocking_operations(mut self, value: bool) -> Self {
        self.blocking_operations = value;
        self
    }

    pub fn with_remote_write(mut self, value: bool) -> Self {
        self.remote_write = value;
        self
    }

    pub fn with_send_recv(mut self, value: bool) -> Self {
        self.send_recv = value;
        self
    }

    pub fn max_message_size(mut self, value: u64) -> Self {
        self.max_message_size = value;
        self
    }

    pub fn build(self) -> Capabilities {
        Capabilities {
            blocking_operations: self.blocking_operations,
            remote_write: self.remote_write,
            send_recv: self.send_recv,
            max_message_size: self.max_message_size,
        }
    }
}
impl Default for CapabilitiesBuilder {
    fn default() -> Self {
        Self {
            blocking_operations: true,
            remote_write: true,
            send_recv: false,
            max_message_size: u64::MAX,
        }
    }
}
