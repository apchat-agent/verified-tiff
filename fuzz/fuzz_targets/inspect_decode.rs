#![no_main]

#[path = "../oracle.rs"]
mod oracle;

use libfuzzer_sys::fuzz_target;
use verified_tiff::Limits;

const LIMITS: Limits = Limits { max_input: 32768, max_dimension: 128, max_output: 16384,
    max_directories: 8, max_tags: 128, max_segments: 128 };

fuzz_target!(|data: &[u8]| {
    if let Err(v) = oracle::check_input(data, &LIMITS) {
        panic!("fuzz oracle violation: {v:?}");
    }
});
