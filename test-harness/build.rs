fn main() {
    let src = "../rust-demangle.c";
    let header = "../rust-demangle.h";
    println!("cargo:rerun-if-changed={}", src);
    println!("cargo:rerun-if-changed={}", header);

    cc::Build::new()
        .file("../rust-demangle.c")
        .flag_if_supported("-std=c89")
        .warnings(true)
        .warnings_into_errors(true)
        .flag_if_supported("-Werror=uninitialized")
        .compile("rust-demangle");
}
