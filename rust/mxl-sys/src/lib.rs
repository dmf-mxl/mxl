// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// Suppress expected warnings from bindgen-generated code.
// See https://github.com/rust-lang/rust-bindgen/issues/1651.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(missing_docs)]
#![allow(rustdoc::broken_intra_doc_links)]
#![allow(rustdoc::invalid_html_tags)]
#![allow(unsafe_op_in_unsafe_fn)]
#![allow(deref_nullptr)]
#![allow(clippy::missing_safety_doc)]

extern crate libloading;

pub mod types {
    include!(concat!(env!("OUT_DIR"), "/types_bindings.rs"));
}

pub mod mxl {
    include!(concat!(env!("OUT_DIR"), "/mxl_bindings.rs"));
}

#[cfg(feature = "mxl-fabrics-ofi")]
pub mod fabrics {
    include!(concat!(env!("OUT_DIR"), "/fabrics_bindings.rs"));
}

pub use mxl::libmxl;
pub use types::*;
