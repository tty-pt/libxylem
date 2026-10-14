//! Ergonomic Rust bindings for libxylem.
//!
//! # Writing a module
//!
//! ```rust,ignore
//! use xy::prelude::*;
//! use core::ffi::c_int;
//!
//! // Emit XY static + get_xy_ptr
//! xy_module!();
//!
//! // Implement a hook
//! #[xy_impl]
//! pub fn on_tick(dt: c_int) -> c_int {
//!     dt + 1
//! }
//!
//! // Define a hook for other modules to implement
//! #[xy_def]
//! pub fn my_event(val: c_int) -> c_int {}
//!
//! // Module entry point
//! xy_install! {}
//! ```

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub use xylem_macros::{
    xy_claim, xy_decl, xy_def, xy_install, xy_impl,
    xy_module, xy_region_init, xy_region_state,
};

use core::ffi::{CStr, c_char, c_int, c_uchar, c_uint, c_void};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

pub const XY_MAX_RET_SIZE: usize = 4096;
pub const XY_INVALID: c_uint = c_uint::MAX;
pub const XY_OK: c_int = 0;
pub const XY_ERR_NOTFOUND: c_int = -1;
pub const XY_ERR_INVALID: c_int = -2;
pub const XY_ERR_TOOBIG: c_int = -3;
pub const XY_ERR_INIT: c_int = -4;
pub const XY_ERR_EPERM: c_int = -5;
/// Module context ABI mismatch — the module must be rebuilt against the
/// current header. See `XY_CTX_ABI_VER`.
pub const XY_ERR_ABI: c_int = -6;

/// Context ABI generation. **Must** match `XY_CTX_ABI_VER` in `xy.h`, and
/// `XY_CTX_SIZE` must equal `size_of::<XyCtx>()`.
pub const XY_CTX_ABI_VER: u32 = 3;
pub const XY_CTX_SIZE: usize = 184;

/// What a module must export from `xy_ctx_abi()`. The host compares this
/// against its own before writing into the module's `XyCtx` and refuses a
/// mismatch with `XY_ERR_ABI` — see the ABI note in `xy.h`.
pub const XY_CTX_ABI_DESC: u64 =
	((XY_CTX_ABI_VER as u64) << 32) | (XY_CTX_SIZE as u64);

pub const XY_REGION_ROOT: u64 = 0;
pub const XY_REGION_INVALID: u64 = u64::MAX;

/// Sentinel stored in fn_cache for "hook not found in this module".
pub const XY_FN_NOT_FOUND: *mut c_void = 1usize as *mut c_void;

// ---------------------------------------------------------------------------
// Deny type
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum XyDenyType {
	Hook = 0,
	Module = 1,
}

// ---------------------------------------------------------------------------
// xy_adapter_t
// ---------------------------------------------------------------------------

pub type XyAdapterCallFn = unsafe extern "C" fn(*mut c_void, *mut c_void, *mut c_void);

#[repr(C)]
pub struct XyAdapterT {
	pub name:     [c_char; 64],
	pub arg_size: usize,
	pub ret_size: usize,
	pub call:     Option<XyAdapterCallFn>,
	pub hook_id:  c_int,
	pub ret:      [c_char; XY_MAX_RET_SIZE],
	pub ran:      c_uint,
}

// SAFETY: XyAdapterT is only mutated before first use (during .init_array
// registration) and then treated as immutable by the dispatch hot path.
unsafe impl Sync for XyAdapterT {}
unsafe impl Send for XyAdapterT {}

// ---------------------------------------------------------------------------
// Function-pointer typedefs matching xy.h
// ---------------------------------------------------------------------------

pub type XyAregFn           = unsafe extern "C" fn(*mut c_char, *mut XyAdapterT) -> c_uint;
pub type XyCallFn           = unsafe extern "C" fn(*mut c_void, *mut XyAdapterT, *mut c_void) -> c_int;
pub type XyLastFn           = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type XyLoadFn           = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyUnloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyReloadFn         = unsafe extern "C" fn(*mut c_char) -> c_int;
pub type XyShutdownFn       = unsafe extern "C" fn();
pub type XyErrnoFn          = unsafe extern "C" fn() -> c_int;
pub type XyStrerrorFn       = unsafe extern "C" fn(c_int) -> *const c_char;
pub type XyDenyFn           = unsafe extern "C" fn(*const c_char, XyDenyType) -> c_int;
pub type XyScopeFn          = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type XyClaimHandlerFn   = unsafe extern "C" fn(*const c_char, c_uchar, *mut c_uchar, *mut c_void) -> c_int;
pub type XyRequireClaimFn   = unsafe extern "C" fn(Option<XyClaimHandlerFn>, *mut c_void) -> c_int;
/// (child_id, plen, ud) — a region is identified by the PAIR.  `plen` was added
/// in 1.5.0; without it a callback cannot tell two sibling regions apart when
/// they share an id.
pub type XyRegionEachCbFn   = unsafe extern "C" fn(u64, u8, *mut c_void) -> c_int;
pub type XyRegionEachFn     = unsafe extern "C" fn(Option<XyRegionEachCbFn>, *mut c_void) -> c_int;
/// xy_with_region(region_id, plen, fn, ud)
pub type XyWithRegionFn     = unsafe extern "C" fn(u64, u8, Option<XyScopeFn>, *mut c_void) -> c_int;
pub type XyCurrentRegionFn  = unsafe extern "C" fn() -> u64;
pub type XyCurrentRegionPlenFn = unsafe extern "C" fn() -> u8;
pub type XyRegionExistsFn   = unsafe extern "C" fn(u64, u8) -> c_int;
/// xy_claim_at(id, plen, fn, ud) — engine-driven region creation. `fn` is
/// nullable, so it is an `Option` exactly like [`XyRequireClaimFn`]'s handler.
pub type XyClaimAtFn        = unsafe extern "C" fn(u64, u8, Option<XyClaimHandlerFn>, *mut c_void) -> c_int;
/// xy_region_at(prefix_id, plen, region_plen) — deepest covering region.
/// `region_plen` is a nullable out-param (`*mut c_uchar`), not an `Option`.
pub type XyRegionAtFn       = unsafe extern "C" fn(u64, u8, *mut c_uchar) -> u64;
/// xy_call_self(retp, adapter, args) — exact-region dispatch.
pub type XyCallSelfFn       = unsafe extern "C" fn(*mut c_void, *mut XyAdapterT, *mut c_void) -> c_int;

// ---------------------------------------------------------------------------
// struct xy_ctx  (injected into each module by the host)
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct XyCtx {
	pub call:           Option<XyCallFn>,
	pub areg:           Option<XyAregFn>,
	pub load:           Option<XyLoadFn>,
	pub err:            Option<XyErrnoFn>,
	pub strerror:       Option<XyStrerrorFn>,
	pub adapter:        *mut XyAdapterT,
	pub last:           Option<XyLastFn>,
	pub shutdown:       Option<XyShutdownFn>,
	pub module_path:    *const c_char,
	pub region_id:      u64,
	pub deny:           Option<XyDenyFn>,
	pub require_claim:  Option<XyRequireClaimFn>,
	pub region_each:    Option<XyRegionEachFn>,
	pub with_region:    Option<XyWithRegionFn>,
	pub current_region: Option<XyCurrentRegionFn>,
	pub current_region_plen: Option<XyCurrentRegionPlenFn>,
	pub region_exists:  Option<XyRegionExistsFn>,
	pub claim_at:       Option<XyClaimAtFn>,
	pub region_at:      Option<XyRegionAtFn>,
	pub call_self:      Option<XyCallSelfFn>,
	pub unload:         Option<XyUnloadFn>,
	pub reload:         Option<XyReloadFn>,
	pub region_state:   *mut c_void,
}

unsafe impl Sync for XyCtx {}
unsafe impl Send for XyCtx {}

/// Rust mirror tripwire, matching the `_Static_assert` on `struct xy_ctx` in
/// `xy.h`: a `#[repr(C)]` struct that silently changes size is an ABI break for
/// every cdylib, and the symptom is corruption in an unrelated library rather
/// than a compile error. Keep in step with `XY_CTX_ABI_VER` / `XY_CTX_SIZE`.
const _: () = assert!(
	core::mem::size_of::<XyCtx>() == XY_CTX_SIZE,
	"XyCtx no longer matches XY_CTX_SIZE: bump XY_CTX_ABI_VER/XY_CTX_SIZE in both \
	 xylem and xy.h, then rebuild every module (stale ones are refused at load)"
);

impl XyCtx {
	/// A zeroed XyCtx suitable for use as a module-level static.
	/// The host fills all function pointers before xy_install runs.
	pub const fn zeroed() -> Self {
		Self {
			call:           None,
			areg:           None,
			load:           None,
			err:            None,
			strerror:       None,
			adapter:        core::ptr::null_mut(),
			last:           None,
			shutdown:       None,
			module_path:    core::ptr::null(),
			region_id:      0,
			deny:           None,
			require_claim:  None,
			region_each:    None,
			with_region:    None,
			current_region: None,
			current_region_plen: None,
			region_exists:  None,
			claim_at:       None,
			region_at:      None,
			call_self:      None,
			unload:         None,
			reload:         None,
			region_state:   core::ptr::null_mut(),
		}
	}
}

// ---------------------------------------------------------------------------
// extern "C" declarations (link against libxylem)
// Only xy_areg is needed by module-side generated code (.init_array reg).
// ---------------------------------------------------------------------------

extern "C" {
	pub fn xy_areg(name: *mut c_char, adapter: *mut XyAdapterT) -> c_uint;
	/// Register this module's XY context from a .init_array constructor so
	/// the host can initialize it even when get_xy_ptr lookup fails.
	pub fn xy_self_init_ctx(ctx: *mut XyCtx);
}

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum XyError {
	NotFound,
	Invalid,
	TooBig,
	Init,
	Eperm,
	/// Module built against a different `xy_ctx` layout; rebuild it.
	Abi,
	Unknown(i32),
}

impl XyError {
	pub fn from_code(code: i32) -> Self {
		match code {
			XY_ERR_NOTFOUND => XyError::NotFound,
			XY_ERR_INVALID  => XyError::Invalid,
			XY_ERR_TOOBIG   => XyError::TooBig,
			XY_ERR_INIT     => XyError::Init,
			XY_ERR_EPERM    => XyError::Eperm,
			XY_ERR_ABI      => XyError::Abi,
			other            => XyError::Unknown(other),
		}
	}
}

// ---------------------------------------------------------------------------
// Module-side safe wrappers (operate through the module's injected XyCtx)
// ---------------------------------------------------------------------------

/// Load a module through the caller's injected XY context.
pub unsafe fn load(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.load.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Unload a module through the caller's injected XY context.
pub unsafe fn unload(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.unload.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Reload a module through the caller's injected XY context.
pub unsafe fn reload(xy: &XyCtx, path: &CStr) -> Result<(), XyError> {
	let code = unsafe { xy.reload.unwrap()(path.as_ptr() as *mut _) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Deny a hook or module through the caller's injected XY context.
pub unsafe fn deny(xy: &XyCtx, what: &CStr, ty: XyDenyType) -> Result<(), XyError> {
	let code = unsafe { xy.deny.unwrap()(what.as_ptr(), ty) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Require claim through the caller's injected XY context.
pub unsafe fn require_claim(
	xy: &XyCtx,
	handler: Option<XyClaimHandlerFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.require_claim.unwrap()(handler, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Enumerate child regions through the caller's injected XY context.
pub unsafe fn region_each(
	xy: &XyCtx,
	f: Option<XyRegionEachCbFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.region_each.unwrap()(f, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Run a closure in a given region through the caller's injected XY context.
///
/// `region_id` alone does not name a region: the root `(0, 0)` and its leftmost
/// 16-bit child `(0, 16)` share `region_id == 0`, so `plen` is required. Fails
/// with `XyError::NotFound` unless that exact `(region_id, plen)` pair exists.
pub unsafe fn with_region(
	xy: &XyCtx,
	region_id: u64,
	plen: u8,
	f: Option<XyScopeFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.with_region.unwrap()(region_id, plen, f, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Return the current thread-local region ID through the caller's injected XY context.
///
/// This is only half an identity — see [`current_region_plen`].
pub unsafe fn current_region(xy: &XyCtx) -> u64 {
	unsafe { xy.current_region.unwrap()() }
}

/// Return the width (prefix length in bits) of the caller's current region.
///
/// The second half of the identity returned by [`current_region`]. Read from the
/// thread-local entry, so it costs no hash lookup. `0` is a real width (the
/// root), not a sentinel.
pub unsafe fn current_region_plen(xy: &XyCtx) -> u8 {
	unsafe { xy.current_region_plen.unwrap()() }
}

/// Whether the region `(region_id, plen)` exists and is addressable.
///
/// `Ok(())` when it does, `Err(XyError::NotFound)` when it does not. Both halves
/// must be given: four regions can share `region_id == 0`.
pub unsafe fn region_exists(xy: &XyCtx, region_id: u64, plen: u8) -> Result<(), XyError> {
	let code = unsafe { xy.region_exists.unwrap()(region_id, plen) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Create the region `(id, plen)` and make it the caller's current region.
///
/// This is the *engine-driven* counterpart to [`require_claim`]: no module is
/// loaded, the engine just carves out a region that later `load` calls and
/// dispatches can name.
///
/// The new region attaches to the **nearest existing ancestor** — the region of
/// the largest width below `plen` that prefix-covers `(id, plen)`, the root
/// always qualifying. No intermediate ancestors are invented, so a multi-bit
/// jump is legal and cheap: claiming `(0, 64)` straight from the root creates
/// one region, not four.
///
/// Rejects with [`XyError::TooBig`] for `plen > 64`, and with
/// [`XyError::Invalid`] when `id` has bits set below its prefix width
/// (misaligned — a canonical plen-16 id is `2<<48`, not `0x1234_5678_9abc_d000`)
/// or when the request is not strictly wider than its nearest covering
/// ancestor.
///
/// An exact `(id, plen)` match is **idempotent**: it succeeds, becomes the
/// current region, and installs `fn` as that region's claim handler when `fn`
/// is `Some`. A later claim therefore reconfigures an existing region rather
/// than silently ignoring the handler.
pub unsafe fn claim_at(
	xy: &XyCtx,
	id: u64,
	plen: u8,
	fn_: Option<XyClaimHandlerFn>,
	ud: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.claim_at.unwrap()(id, plen, fn_, ud) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

/// Find the deepest existing region whose prefix covers a point.
///
/// The "which region is this point in?" primitive: a caller walking the tree
/// coarse-to-fine needs to tell an exact hit from an ancestor fallback.
///
/// `(a, plen_a)` covers `(prefix_id, plen)` when `plen_a <= plen` and masking
/// `prefix_id` to `plen_a` yields `a`. The root covers every point, so the only
/// way to miss is a `plen` no region reaches — hence the `Option`.
///
/// The **id half alone does not name a region**: `(0, 0)`, `(0, 16)`,
/// `(0, 32)` and `(0, 48)` all share `id == 0`, so the covering region's width
/// is returned as well. Feed the pair to [`with_region`] to dispatch there.
/// Note the width is a property of the *region found*, not of the point asked
/// about, which is why it comes back separately.
///
/// Pass `core::ptr::null_mut()` for `region_plen` to skip it; the C side treats
/// a null out-param as "don't care" and always defines it when non-null.
pub unsafe fn region_at(
	xy: &XyCtx,
	prefix_id: u64,
	plen: u8,
	region_plen: *mut c_uchar,
) -> Option<u64> {
	let id = unsafe { xy.region_at.unwrap()(prefix_id, plen, region_plen) };
	if id == XY_REGION_INVALID { None } else { Some(id) }
}

/// Dispatch to the caller's current region's own modules only.
///
/// The exact-region counterpart of `xy_call`: `call` reaches the current
/// region's whole subtree, this reaches only the modules loaded *directly*
/// into the current region — no descendant region, and no ancestor.
///
/// It exists so an ancestor walk gives each region one observable turn instead
/// of re-running every level's entire subtree once per step. Deny checks and
/// `xy_last` predecessor semantics are identical to `call`'s: a listener still
/// sees its predecessor's return value, and a deny from any ancestor still
/// refuses the whole dispatch with `EPERM`.
///
/// Returns [`XyError::NotFound`] when no listener in this exact region ran.
/// That is *not* the same as a listener returning zero — read `xy.err()` to tell
/// them apart.
pub unsafe fn call_self(
	xy: &XyCtx,
	retp: *mut c_void,
	adapter: *mut XyAdapterT,
	args: *mut c_void,
) -> Result<(), XyError> {
	let code = unsafe { xy.call_self.unwrap()(retp, adapter, args) };
	if code == XY_OK { Ok(()) } else { Err(XyError::from_code(code)) }
}

// ---------------------------------------------------------------------------
// XY_RS! macro — typed access to per-region state
// ---------------------------------------------------------------------------

/// Access per-region state as a typed pointer.
///
/// ```rust,ignore
/// let state = XY_RS!(XyRegionState);
/// unsafe { (*state).counter += 1; }
/// ```
#[macro_export]
macro_rules! XY_RS {
	($ty:ty) => {
		XY.region_state as *mut $ty
	};
}

// ---------------------------------------------------------------------------
// Prelude
// ---------------------------------------------------------------------------

pub mod prelude {
	pub use crate::{
		XyCtx, XyDenyType, XyError,
		XY_OK, XY_ERR_NOTFOUND, XY_REGION_ROOT,
		XY_ERR_ABI, XY_CTX_ABI_DESC,
		load, unload, reload, deny, require_claim, region_each, with_region, current_region,
		current_region_plen, region_exists,
		XY_RS,
	};
	pub use xylem_macros::{
		xy_claim, xy_decl, xy_def, xy_install, xy_impl,
		xy_module, xy_region_init, xy_region_state,
	};
}
