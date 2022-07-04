extern crate rustc_demangle;

use std::env;
use std::io::BufReader;
use std::io::prelude::*;
use std::fs::File;
use std::path::PathBuf;

fn demangle_via_c(mangled: &str, verbose: bool) -> String {
    use std::ffi::{CStr, CString};
    use std::os::raw::c_char;

    extern "C" {
        fn rust_demangle(mangled: *const c_char, flags: i32) -> *mut c_char;
        fn free(ptr: *mut c_char);
    }

    let flags = if verbose { 1 } else { 0 };
    let out = unsafe {
        rust_demangle(CString::new(mangled).unwrap().as_ptr(), flags)
    };
    if out.is_null() {
        String::new()
    } else {
        unsafe {
            let s = CStr::from_ptr(out).to_string_lossy().into_owned();
            free(out);
            s
        }
    }
}

fn main() {
    macro_rules! t_nohash {
        ($a:expr, $b:expr) => ({
            assert_eq!(format!("{:#}", demangle_via_c($a, false)), $b);
        })
    }

    t_nohash!(
        "_RNvC6_123foo3bar",
        "123foo::bar"
    );
    t_nohash!(
        "_RNqCs4fqI2P2rA04_11utf8_identsu30____7hkackfecea1cbdathfdh9hlq6y",
        "utf8_idents::საჭმელად_გემრიელი_სადილი"
    );
    t_nohash!(
        "_RNCNCNgCs6DXkGYLi8lr_2cc5spawn00B5_",
        "cc::spawn::{closure#0}::{closure#0}"
    );
    t_nohash!(
        "_RNCINkXs25_NgCsbmNqQUJIY6D_4core5sliceINyB9_4IterhENuNgNoBb_4iter8iterator8Iterator9rpositionNCNgNpB9_6memchr7memrchrs_0E0Bb_",
        "<core::slice::Iter<u8> as core::iter::iterator::Iterator>::rposition::<core::slice::memchr::memrchr::{closure#1}>::{closure#0}"
    );
    t_nohash!(
        "_RINbNbCskIICzLVDPPb_5alloc5alloc8box_freeDINbNiB4_5boxed5FnBoxuEp6OutputuEL_ECs1iopQbuBiw2_3std",
        "alloc::alloc::box_free::<dyn alloc::boxed::FnBox<(), Output = ()>>"
    );

    let header = "legacy+generics,legacy,mw,mw+compression,v0,v0+compression";

    for path in env::args_os().skip(1).map(PathBuf::from) {
        let mut lines = BufReader::new(File::open(path).unwrap())
            .lines()
            .map(|l| l.unwrap());

        assert_eq!(lines.next().unwrap(), header);

        for line in lines {
            for mangling in line.split(',').skip(4) {
                match rustc_demangle::try_demangle(mangling) {
                    Ok(demangling) => {
                        let demangling_alt = format!("{:#}", demangling);
                        if demangling_alt.contains('?') {
                            panic!("demangle(alt) printing failed for {:?}\n{:?}", mangling, demangling_alt);
                        }
                        assert_eq!(demangling_alt, demangle_via_c(mangling, false));

                        let demangling = format!("{}", demangling);
                        if demangling.contains('?') {
                            panic!("demangle printing failed for {:?}\n{:?}", mangling, demangling);
                        }
                        assert_eq!(demangling, demangle_via_c(mangling, true));
                    }
                    Err(_) => panic!("try_demangle failed for {:?}", mangling),
                }
            }
        }
    }
}
