fn main() {
    cc::Build::new()
        .file("../rust-demangle.c")
        .flag_if_supported("-std=c89")
        .warnings(true)
        .warnings_into_errors(true)
        .flag_if_supported("-Werror=uninitialized")
        .compile("rust-demangle");
}
