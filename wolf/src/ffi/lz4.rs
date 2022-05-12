/* automatically generated by rust-bindgen 0.59.2 */

pub type __darwin_size_t = ::std::os::raw::c_ulong;
pub type size_t = __darwin_size_t;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct w_buf_t {
    pub data: *mut u8,
    pub len: size_t,
}
pub type w_buf = *mut w_buf_t;
extern "C" {
    #[doc = " compress stream using lz4 algorithm"]
    #[doc = " @param p_src the source buffer"]
    #[doc = " @param p_dst the result of compressed buffer"]
    #[doc = " @param p_fast_mode 1 means Fast mode and 0 is Default mode"]
    #[doc = " @param p_acceleration the acceleration of process. the default value is 1"]
    #[doc = " @param p_trace the trace information in the string format with maximum size of 256"]
    #[doc = " @return 0 means success"]
    pub fn w_lz4_compress(
        p_src: w_buf,
        p_dst: w_buf,
        p_fast_mode: ::std::os::raw::c_int,
        p_acceleration: ::std::os::raw::c_int,
        p_trace: w_buf,
    ) -> ::std::os::raw::c_int;
}
extern "C" {
    #[doc = " decompress lz4 stream"]
    #[doc = " @param p_src the compressed source buffer"]
    #[doc = " @param p_dst the decompressed buffer"]
    #[doc = " @param p_trace the trace information in the string fromat with maximum size of 256"]
    #[doc = " @return 0 means success"]
    pub fn w_lz4_decompress(p_src: w_buf, p_dst: w_buf, p_trace: w_buf) -> ::std::os::raw::c_int;
}
extern "C" {
    #[doc = " free buffer"]
    #[doc = " @param p_buf the buffer"]
    #[doc = " @return 0 means success"]
    pub fn w_lz4_free_buf(p_buf: w_buf) -> ::std::os::raw::c_int;
}
