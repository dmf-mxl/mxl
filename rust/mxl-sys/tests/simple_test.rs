#[test]
fn there_is_bindgen_generated_code() {
    let mxl_version = mxl_sys::mxlVersionType {
        major: 3,
        minor: 2,
        bugfix: 1,
        ..Default::default()
    };

    println!("mxl_version: {:?}", mxl_version);
}
