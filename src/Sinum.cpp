// ============================================================================
// Sinum.cpp — sinum's runtime logic, ported 1:1 (behavior-identical).
//
// Source: abdallh-rgx/Sinum Android/main.cpp + Url.h + Util.h.
// What each ported piece does:
//
//   Uri::Parse / Uri::CreateUri   — Url.h, verbatim (string_view parsing)
//   Util::IsPointerBad            — Util.h, verbatim (open() EFAULT probe)
//   curl_easy_setopt detour       — main.cpp, verbatim redirect rules:
//       ol.epicgames.com / ak.epicgames.com / *.akamaized.net /
//       on.epicgames.com  →  <kRedirectProtocol>://<kRedirectHost><path>
//       CURLOPT_SSL_VERIFYPEER   →  0 (self-signed tunnel certificate)
//
// Deltas from libsinum.so (both are bug fixes for the on-device crash, NOT
// behavior changes to the redirect itself):
//
//   [sinum] constructor → pthread → dlopen("libUE4.so", RTLD_NOW) as soon as
//          the library loads — races the main thread's own library loading;
//          when it loses the race it holds/waits the linker namespace mutex
//          while the main thread needs it too → wedged app (the tombstone).
//   [here] Sinum::Hook() runs after UEngine::GetEngine() != 0 (libUE4.so is
//          fully loaded and initialized by then) and uses dlopen(RTLD_NOLOAD)
//          which never loads anything — it only fetches the existing handle.
//
//   [sinum] hooked via its own Dobby instance inside libsinum.so.
//   [here] hooked via the same Dobby (third_party/dobby/libdobby-arm64.a)
//          that powers every other OGS hook — one engine, one library.
// ============================================================================
#include "framework.h"
#include "Sinum.h"

#include "dobby.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

namespace Sinum
{

// ─── redirect target (sinum's compiled-in values, 1:1) ──────────────────────
// Change these two lines to repoint the client at a different server.
constexpr const char* kRedirectProtocol = "http";
constexpr const char* kRedirectHost = "127.0.0.1:3551";
constexpr const char* kRedirectPort = ""; // empty = default port

// ─── Uri (Url.h, verbatim port) ────────────────────────────────────────────
struct Uri
{
	std::string_view Protocol, Host, Port, Path, QueryString;

	static Uri Parse(const std::string_view& uri)
	{
		Uri result;

		if (uri.length() == 0)
			return result;

		using iterator_t = std::string_view::const_iterator;
		iterator_t uriEnd = uri.end();

		iterator_t queryStart = std::find(uri.begin(), uriEnd, '?');

		iterator_t protocolStart = uri.begin();
		iterator_t protocolEnd = std::find(protocolStart, uriEnd, ':');

		if (protocolEnd != uriEnd)
		{
			std::string_view prot = &*(protocolEnd);
			if ((prot.length() > 3) && (prot.substr(0, 3) == "://"))
			{
				result.Protocol = MakeStringView(uri, protocolStart, protocolEnd);
				protocolEnd += 3;
			}
			else
				protocolEnd = uri.begin();
		}
		else
			protocolEnd = uri.begin();

		iterator_t hostStart = protocolEnd;
		iterator_t pathStart = std::find(hostStart, uriEnd, '/');

		iterator_t hostEnd = std::find(protocolEnd, (pathStart != uriEnd) ? pathStart : queryStart, ':');

		result.Host = MakeStringView(uri, hostStart, hostEnd);

		if ((hostEnd != uriEnd) && ((&*(hostEnd))[0] == ':'))
		{
			++hostEnd;
			iterator_t portEnd = (pathStart != uriEnd) ? pathStart : queryStart;
			result.Port = MakeStringView(uri, hostEnd, portEnd);
		}

		if (pathStart != uriEnd)
			result.Path = MakeStringView(uri, pathStart, queryStart);

		if (queryStart != uriEnd)
			result.QueryString = MakeStringView(uri, queryStart, uri.end());

		return result;
	}

	static std::string CreateUri(std::string_view Protocol, std::string_view Host, std::string_view Port,
		std::string_view Path, std::string_view QueryString)
	{
		std::ostringstream str;
		if (!Protocol.empty())
		{
			str.write(Protocol.data(), Protocol.size());
			str.write("://", 3);
		}
		str.write(Host.data(), Host.size());
		if (!Port.empty())
		{
			str.write(":", 1);
			str.write(Port.data(), Port.size());
		}
		if (!Path.empty())
			str.write(Path.data(), Path.size());
		if (!QueryString.empty())
			str.write(QueryString.data(), QueryString.size());
		return str.str();
	}

private:
	static constexpr std::string_view MakeStringView(const std::string_view& base,
		std::string_view::const_iterator first, std::string_view::const_iterator last)
	{
		return base.substr(std::distance(base.begin(), first), std::distance(first, last));
	}
};

// ─── Util::IsPointerBad (Util.h, verbatim port) ────────────────────────────
// open() on an unreadable pointer fails with EFAULT — a cheap readability
// probe that never crashes, guarding the CURLOPT_URL path against garbage.
static bool IsPointerBad(void* p)
{
	int fh = open((const char*)p, 0, 0);
	int e = errno;

	if (-1 == fh && e == EFAULT)
	{
		return true;
	}
	else if (fh != -1)
	{
		close(fh);
	}

	return false;
}

// ─── curl_easy_setopt detour (main.cpp, verbatim port) ─────────────────────
// libcurl option constants used here (stable across curl versions):
constexpr int CURLOPT_URL = 10002;            // 10000 + CURLOPTTYPE_OBJECTPOINT 2
constexpr int CURLOPT_SSL_VERIFYPEER = 64;    // CURLOPTTYPE_LONG 64

static void* (*curl_easy_setoptOG)(void*, int, void*);

static void* curl_easy_setoptHK(void* curl, int option, void* arg)
{
	if (!IsPointerBad(arg) && option == CURLOPT_URL)
	{
		std::string url = reinterpret_cast<char*>(arg);

		Uri uri = Uri::Parse(url);

		if (uri.Host.ends_with("ol.epicgames.com")
			|| uri.Host.ends_with("ak.epicgames.com")
			|| uri.Host.ends_with(".akamaized.net")
			|| uri.Host.ends_with("on.epicgames.com"))
		{
			url = Uri::CreateUri(kRedirectProtocol, kRedirectHost, kRedirectPort, uri.Path, uri.QueryString);
		}

		// curl copies the URL string (documented CURLOPT_URL semantics), so
		// passing the temporary's c_str() is safe — identical to sinum.
		return curl_easy_setoptOG(curl, option, (void*)url.c_str());
	}
	else if (option == CURLOPT_SSL_VERIFYPEER)
	{
		return curl_easy_setoptOG(curl, option, (void*)0);
	}

	return curl_easy_setoptOG(curl, option, arg);
}

// ─── install (deadlock-free replacement for sinum's constructor dlopen) ─────
void Hook()
{
	// RTLD_NOLOAD: return the handle ONLY if libUE4.so is already loaded.
	// No loading, no constructor re-run, no RTLD_NOW re-resolution — the
	// call cannot race the app's own startup (we are past engine init).
	void* Base = nullptr;
	for (int Attempt = 0; Attempt < 30 && !Base; Attempt++)
	{
		Base = dlopen("libUE4.so", RTLD_NOLOAD | RTLD_LAZY);
		if (!Base)
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	if (!Base)
	{
		Log("Sinum: libUE4.so not loaded — redirect hook NOT installed");
		return;
	}

	auto curl_easy_setopt = (void*)dlsym(Base, "curl_easy_setopt");
	if (!curl_easy_setopt)
	{
		Log("Sinum: curl_easy_setopt not found in libUE4.so — redirect hook NOT installed");
		return;
	}

	if (DobbyHook(curl_easy_setopt, (dobby_dummy_func_t)curl_easy_setoptHK,
			(dobby_dummy_func_t*)&curl_easy_setoptOG) != 0)
	{
		Log("Sinum: DobbyHook(curl_easy_setopt) FAILED");
		return;
	}

	Log("Sinum: curl_easy_setopt hooked — Epic traffic redirects to "
		+ std::string(kRedirectProtocol) + "://" + kRedirectHost);
}

} // namespace Sinum
