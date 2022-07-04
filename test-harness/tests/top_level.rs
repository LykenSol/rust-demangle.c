//! Tests copied from `https://github.com/rust-lang/rustc-demangle`'s
//! `src/lib.rs` at `fd906f850f90f6d4845c7b8219d218293e0ab3ed`.
//!
//! These are the only changes made to the tests:
//! * `super::` paths -> `rust_demangle_c_test_harness::`
//! * `#[ignore = "stack overflow"]` was added to tests that overflow the stack
//! * `#[should_panic]` was added to tests that don't pass yet

use rust_demangle_c_test_harness::{assert_contains, assert_ends_with};

macro_rules! t {
    ($a:expr, $b:expr) => {
        assert!(ok($a, $b))
    };
}

macro_rules! t_err {
    ($a:expr) => {
        assert!(ok_err($a))
    };
}

macro_rules! t_nohash {
    ($a:expr, $b:expr) => {{
        assert_eq!(
            format!("{:#}", rust_demangle_c_test_harness::demangle($a)),
            $b
        );
    }};
}

fn ok(sym: &str, expected: &str) -> bool {
    match rust_demangle_c_test_harness::try_demangle(sym) {
        Ok(s) => {
            if s.to_string() == expected {
                true
            } else {
                println!("\n{}\n!=\n{}\n", s, expected);
                false
            }
        }
        Err(_) => {
            println!("error demangling");
            false
        }
    }
}

fn ok_err(sym: &str) -> bool {
    match rust_demangle_c_test_harness::try_demangle(sym) {
        Ok(_) => {
            println!("succeeded in demangling");
            false
        }
        Err(_) => rust_demangle_c_test_harness::demangle(sym).to_string() == sym,
    }
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle() {
    t_err!("test");
    t!("_ZN4testE", "test");
    t_err!("_ZN4test");
    t!("_ZN4test1a2bcE", "test::a::bc");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_dollars() {
    t!("_ZN4$RP$E", ")");
    t!("_ZN8$RF$testE", "&test");
    t!("_ZN8$BP$test4foobE", "*test::foob");
    t!("_ZN9$u20$test4foobE", " test::foob");
    t!("_ZN35Bar$LT$$u5b$u32$u3b$$u20$4$u5d$$GT$E", "Bar<[u32; 4]>");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_many_dollars() {
    t!("_ZN13test$u20$test4foobE", "test test::foob");
    t!("_ZN12test$BP$test4foobE", "test*test::foob");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_osx() {
    t!(
        "__ZN5alloc9allocator6Layout9for_value17h02a996811f781011E",
        "alloc::allocator::Layout::for_value::h02a996811f781011"
    );
    t!("__ZN38_$LT$core..option..Option$LT$T$GT$$GT$6unwrap18_MSG_FILE_LINE_COL17haf7cb8d5824ee659E", "<core::option::Option<T>>::unwrap::_MSG_FILE_LINE_COL::haf7cb8d5824ee659");
    t!("__ZN4core5slice89_$LT$impl$u20$core..iter..traits..IntoIterator$u20$for$u20$$RF$$u27$a$u20$$u5b$T$u5d$$GT$9into_iter17h450e234d27262170E", "core::slice::<impl core::iter::traits::IntoIterator for &'a [T]>::into_iter::h450e234d27262170");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_windows() {
    t!("ZN4testE", "test");
    t!("ZN13test$u20$test4foobE", "test test::foob");
    t!("ZN12test$RF$test4foobE", "test&test::foob");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_elements_beginning_with_underscore() {
    t!("_ZN13_$LT$test$GT$E", "<test>");
    t!("_ZN28_$u7b$$u7b$closure$u7d$$u7d$E", "{{closure}}");
    t!("_ZN15__STATIC_FMTSTRE", "__STATIC_FMTSTR");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_trait_impls() {
    t!(
        "_ZN71_$LT$Test$u20$$u2b$$u20$$u27$static$u20$as$u20$foo..Bar$LT$Test$GT$$GT$3barE",
        "<Test + 'static as foo::Bar<Test>>::bar"
    );
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_without_hash() {
    let s = "_ZN3foo17h05af221e174051e9E";
    t!(s, "foo::h05af221e174051e9");
    t_nohash!(s, "foo");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_without_hash_edgecases() {
    // One element, no hash.
    t_nohash!("_ZN3fooE", "foo");
    // Two elements, no hash.
    t_nohash!("_ZN3foo3barE", "foo::bar");
    // Longer-than-normal hash.
    t_nohash!("_ZN3foo20h05af221e174051e9abcE", "foo");
    // Shorter-than-normal hash.
    t_nohash!("_ZN3foo5h05afE", "foo");
    // Valid hash, but not at the end.
    t_nohash!("_ZN17h05af221e174051e93fooE", "h05af221e174051e9::foo");
    // Not a valid hash, missing the 'h'.
    t_nohash!("_ZN3foo16ffaf221e174051e9E", "foo::ffaf221e174051e9");
    // Not a valid hash, has a non-hex-digit.
    t_nohash!("_ZN3foo17hg5af221e174051e9E", "foo::hg5af221e174051e9");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_thinlto() {
    // One element, no hash.
    t!("_ZN3fooE.llvm.9D1C9369", "foo");
    t!("_ZN3fooE.llvm.9D1C9369@@16", "foo");
    t_nohash!(
        "_ZN9backtrace3foo17hbb467fcdaea5d79bE.llvm.A5310EB9",
        "backtrace::foo"
    );
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_llvm_ir_branch_labels() {
    t!("_ZN4core5slice77_$LT$impl$u20$core..ops..index..IndexMut$LT$I$GT$$u20$for$u20$$u5b$T$u5d$$GT$9index_mut17haf9727c2edfbc47bE.exit.i.i", "core::slice::<impl core::ops::index::IndexMut<I> for [T]>::index_mut::haf9727c2edfbc47b.exit.i.i");
    t_nohash!("_ZN4core5slice77_$LT$impl$u20$core..ops..index..IndexMut$LT$I$GT$$u20$for$u20$$u5b$T$u5d$$GT$9index_mut17haf9727c2edfbc47bE.exit.i.i", "core::slice::<impl core::ops::index::IndexMut<I> for [T]>::index_mut.exit.i.i");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn demangle_ignores_suffix_that_doesnt_look_like_a_symbol() {
    t_err!("_ZN3fooE.llvm moocow");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn dont_panic() {
    rust_demangle_c_test_harness::demangle("_ZN2222222222222222222222EE").to_string();
    rust_demangle_c_test_harness::demangle("_ZN5*70527e27.ll34csaғE").to_string();
    rust_demangle_c_test_harness::demangle("_ZN5*70527a54.ll34_$b.1E").to_string();
    rust_demangle_c_test_harness::demangle(
        "\
         _ZN5~saäb4e\n\
         2734cOsbE\n\
         5usage20h)3\0\0\0\0\0\0\07e2734cOsbE\
         ",
    )
    .to_string();
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn invalid_no_chop() {
    t_err!("_ZNfooE");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn handle_assoc_types() {
    t!("_ZN151_$LT$alloc..boxed..Box$LT$alloc..boxed..FnBox$LT$A$C$$u20$Output$u3d$R$GT$$u20$$u2b$$u20$$u27$a$GT$$u20$as$u20$core..ops..function..FnOnce$LT$A$GT$$GT$9call_once17h69e8f44b3723e1caE", "<alloc::boxed::Box<alloc::boxed::FnBox<A, Output=R> + 'a> as core::ops::function::FnOnce<A>>::call_once::h69e8f44b3723e1ca");
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn handle_bang() {
    t!(
        "_ZN88_$LT$core..result..Result$LT$$u21$$C$$u20$E$GT$$u20$as$u20$std..process..Termination$GT$6report17hfc41d0da4a40b3e8E",
        "<core::result::Result<!, E> as std::process::Termination>::report::hfc41d0da4a40b3e8"
    );
}

// FIXME(eddyb) port recursion limits to C.
#[ignore = "stack overflow"]
#[test]
fn limit_recursion() {
    assert_contains!(
        rust_demangle_c_test_harness::demangle("_RNvB_1a").to_string(),
        "{recursion limit reached}"
    );
    assert_contains!(
        rust_demangle_c_test_harness::demangle("_RMC0RB2_").to_string(),
        "{recursion limit reached}"
    );
}

// FIXME(eddyb) port the relevant functionality to C.
#[should_panic]
#[test]
fn limit_output() {
    assert_ends_with!(
        rust_demangle_c_test_harness::demangle("RYFG_FGyyEvRYFF_EvRYFFEvERLB_B_B_ERLRjB_B_B_")
            .to_string(),
        "{size limit reached}"
    );
    // NOTE(eddyb) somewhat reduced version of the above, effectively
    // `<for<...> fn()>` with a larger number of lifetimes in `...`.
    assert_ends_with!(
        rust_demangle_c_test_harness::demangle("_RMC0FGZZZ_Eu").to_string(),
        "{size limit reached}"
    );
}
