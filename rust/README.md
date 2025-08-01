# Rust bindings for DMF MXL

## Goals

- Hide all the unsafe stuff inside these bindings.
- Provide more Rust-native like experience (async API based on `futures::stream` and
  `futures::sink`?).

## Code Guidelines

- Use `rustfmt` in it's default settings for code formatting.
- The `cargo clippy` should be always clean.
- Try to avoid adding more dependencies, unless really necessary.
- Never use `unwrap`, `expect`, or a similar construct that causes a panic. Always return errors. Tests are an exception.

## Building

- `cargo build`

## TODO

- Get rid of the headers copy. Use the main headers as part of the build process.
- Change the tests so they can use libraries build from the main repo.
- Setup CI/CD.
- Extend the functionality.
