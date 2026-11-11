// ============================================================================
// Sinum.h — sinum logic merged 1:1 into libOGS-12.41.so (single library).
//
// sinum (abdallh-rgx/Sinum, "Copyright (c) 2025 Project Nova LLC") was a
// separate libsinum.so the APK loaded next to this one. Two libraries hooking
// libUE4.so from two separate hook engines + sinum's early constructor-side
// dlopen("libUE4.so", RTLD_NOW) caused:
//
//   1. a linker-mutex deadlock (tombstone: sinum's worker stuck in
//      dlopen → NonPI::MutexLockWithTimeout while the main thread was still
//      bringing the app up), and
//   2. two independent inline-hook engines patching the same libUE4.so.
//
// The merge: sinum's entire runtime behavior lives in THIS library now —
// same redirect rules, same CURLOPT handling, same host — but installed at a
// deadlock-free point (after the engine singleton is up, via RTLD_NOLOAD).
//
// The APK must NOT contain libsinum.so anymore (see README deployment notes).
// ============================================================================
#pragma once

namespace Sinum
{
	// Install the curl_easy_setopt redirect hook. Must be called AFTER the
	// engine singleton is up (libUE4.so fully loaded by the app's own
	// GameActivity flow) — at that point dlopen("libUE4.so", RTLD_NOLOAD)
	// only returns the existing handle; it never loads, never re-runs
	// constructors and never forces RTLD_NOW re-resolution (the deadlock).
	// The redirect is therefore live before any Epic HTTP call that happens
	// after engine init (login/MCP/manifest traffic included).
	void Hook();
}
