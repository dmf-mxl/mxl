// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Common utilities shared across examples.

/// Initializes tracing subscriber for examples.
///
/// Configures logging to stdout with an INFO level filter, respecting the
/// `RUST_LOG` environment variable for custom log levels.
pub fn setup_logging() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::builder()
                .with_default_directive(tracing::level_filters::LevelFilter::INFO.into())
                .from_env_lossy(),
        )
        .init();
}
