pub fn read_file(file_path: impl AsRef<std::path::Path>) -> Result<String, std::io::Error> {
    use std::io::Read;

    let mut file = std::fs::File::open(file_path)?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)?;
    Ok(contents)
}
