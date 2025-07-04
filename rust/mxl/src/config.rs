use std::str::FromStr;

include!(concat!(env!("OUT_DIR"), "/constants.rs"));

pub fn get_mxf_so_path() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_BUILD_DIR)
        .expect("build error: 'MXL_BUILD_DIR' is invalid")
        .join("lib")
        .join("libmxl.so")
}
