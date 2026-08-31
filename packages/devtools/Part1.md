# PART 1: COOKIE STORAGE & MANAGEMENT

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every byte from `Set-Cookie` header to disk to DevTools to your scraper.*

---

## 1.1 The Complete Cookie Lifecycle

### 1.1.1 The Big Picture Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  NETWORK SERVICE PROCESS  (separate from browser process)                  │
│                                                                            │
│  ┌───────────────────────┐    ┌───────────────────────┐                  │
│  │ URL Loader            │───▶│ HTTP Stream Parser    │                  │
│  │ (per-request)         │    │ (parses HTTP/1.1 / 2) │                  │
│  └───────────────────────┘    └───────────┬───────────┘                  │
│                                            │                              │
│                                            ▼                              │
│                                ┌───────────────────────┐                  │
│                                | HttpResponseHeaders   │                  │
│                                | (vector of headers)   │                  │
│                                └───────────┬───────────┘                  │
│                                            │ for each Set-Cookie          │
│                                            ▼                              │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │ STEP 1: ParsedCookie                                             │     │
│  │ File: net/cookies/parsed_cookie.cc:179                          │     │
│  │ Input:  "name=value; Path=/; Secure; HttpOnly; SameSite=Lax"    │     │
│  │ Output: ParsedCookie {pairs_: [("name","value"),                 │     │
│  │                                ("path","/"),                     │     │
│  │                                ("secure",""),                   │     │
│  │                                ("httponly",""),                  │     │
│  │                                ("samesite","lax")]}              │     │
│  │ Algorithm: ParseTokenValuePairs() walks `;`-delimited pairs     │     │
│  └─────────────────────────────────────────┬───────────────────────┘     │
│                                            │                              │
│                                            ▼                              │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │ STEP 2: CanonicalCookie::Create()                                │     │
│  │ File: net/cookies/canonical_cookie.cc:297                      │     │
│  │ - GetCookieDomainWithString(url, parsed.Domain()) → ".example"   │     │
│  │   (cookie_util.cc:379)                                          │     │
│  │ - CanonPathWithString(url, parsed.Path()) → "/"                  │     │
│  │   (cookie_util.cc:648)                                          │     │
│  │ - ParseExpiration(parsed) → base::Time (Max-Age > Expires)       │     │
│  │   (canonical_cookie.cc:218)                                     │     │
│  │ - ValidateAndAdjustExpiryDate() → caps at 400 days              │     │
│  │   (canonical_cookie.cc:265)                                     │     │
│  │ - CookiePrefix check (__Host- / __Secure- rules)                 │     │
│  │ - CookieInclusionStatus populated with INCLUDE/EXCLUDE_*        │     │
│  └─────────────────────────────────────────┬───────────────────────┘     │
│                                            │                              │
│                                            ▼                              │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │ STEP 3: CookieMonster::SetCanonicalCookie()                     │     │
│  │ File: net/cookies/cookie_monster.cc:1728                        │     │
│  │ - IsSetPermittedInContext() → checks SameSite context           │     │
│  │   (cookie_base.cc:299)                                          │     │
│  │ - MaybeDeleteEquivalentCookieAndUpdateStatus() — overwrite       │     │
│  │ - InternalInsertCookie()                                         │     │
│  └─────────────────────────────────────────┬───────────────────────┘     │
│                                            │                              │
│              ┌────────────────────────────┴─────────────────────────┐    │
│              ▼                            ▼                          ▼    │
│  ┌───────────────────┐  ┌──────────────────────────┐  ┌────────────┐  │
│  │ cookies_          │  │ partitioned_cookies_     │  │ store_     │  │
│  │ (CookieMap        │  │ (map<CookiePartitionKey, │  │ AddCookie()│  │
│  │  multimap<eTLD+1, │  │  unique_ptr<CookieMap>>) │  │ → SQLite   │  │
│  │  CanonicalCookie>)│  │                          │  │ background │  │
│  └───────────────────┘  └──────────────────────────┘  └──────┬─────┘  │
│                                                               │        │
│                                                               ▼        │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │ SQLitePersistentCookieStore (net/extras/sqlite/)                │  │
│  │ Table: cookies (host_key, name, value, path, top_frame_site_key│  │
│  │   expires_utc, is_secure, is_httponly, samesite, ...)          │  │
│  │ File on disk: <profile>/Network/Cookies                       │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │ STEP 4: Notify listeners                                         │  │
│  │ CookieMonsterChangeDispatcher::DispatchChange()                  │  │
│  │ File: cookie_monster_change_dispatcher.cc:194                   │  │
│  │ Notifies: per-domain listeners, per-URL listeners, global       │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.1.2 The Step-by-Step Sequence (for one cookie)

Consider a server response:
```
HTTP/1.1 200 OK
Set-Cookie: session_id=abc123; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=86400
```

**T+0ms**: Network service receives the HTTP response headers.

**T+0.1ms**: `URLLoader` parses headers via `net::HttpResponseHeaders`. Each `Set-Cookie` header value is passed individually to the cookie subsystem.

**T+0.5ms**: `ParsedCookie` constructor fires (`parsed_cookie.cc:179`):
```cpp
ParsedCookie::ParsedCookie(std::string_view cookie_line,
                           CookieInclusionStatus* status_out) {
  CookieInclusionStatus blank_status;
  if (status_out == nullptr) status_out = &blank_status;
  *status_out = CookieInclusionStatus();
  ParseTokenValuePairs(cookie_line, *status_out);  // splits on ';'
  if (IsValid()) SetupAttributes();                 // sets path_index_, etc.
}
```
Result: `pairs_ = [("session_id","abc123"), ("path","/"), ("secure",""), ("httponly",""), ("samesite","lax"), ("max-age","86400")]`

**T+1ms**: `CanonicalCookie::Create()` (`canonical_cookie.cc:297`):
- `cookie_util::GetCookieDomainWithString(url, "")` → returns `"example.com"` (host-only since no Domain= attribute)
- `cookie_util::CanonPathWithString(url, "/")` → returns `"/"` (URL-escaped if needed)
- `ParseExpiration()` → prefers `Max-Age=86400` over `Expires=`
- `ValidateAndAdjustExpiryDate()` → caps at 400 days (86400s = 1 day, fine)
- Prefix check: cookie name doesn't start with `__Host-` or `__Secure-`, so no prefix validation needed
- Returns `std::unique_ptr<CanonicalCookie>` with all fields populated

**T+2ms**: `CookieMonster::SetCanonicalCookie()` (`cookie_monster.cc:1728`):
- Calls `cc->IsSetPermittedInContext(source_url, options, params, cookieable_schemes_, access_result)` — this runs the SameSite / Secure / HttpOnly inclusion logic
- If `CookieInclusionStatus::IsInclude()` is true → proceed
- `MaybeDeleteEquivalentCookieAndUpdateStatus()` — finds any existing cookie with the same `(name, host, path, partition_key)` and deletes it (with proper `CookieChangeCause::OVERWRITE`)
- `InternalInsertCookie()` (`cookie_monster.cc:1639`):
  - If `ShouldUpdatePersistentStore(*cc)` (true because `IsPersistent()` is true since expiry is set): calls `store_->AddCookie(*cc_ptr)` — this POSTS the write to a background thread, returns immediately
  - Inserts into `cookies_` CookieMap (key = `"example.com"` eTLD+1, value = the unique_ptr<CanonicalCookie>)
  - Calls `change_dispatcher_.DispatchChange(CookieChangeInfo(*cc_ptr, access_result, ToCookieChangeCause(observability)), /*notify_global_hooks=*/true)`

**T+2.5ms**: `CookieMonsterChangeDispatcher::DispatchChange()` (`cookie_monster_change_dispatcher.cc:194`):
```cpp
void CookieMonsterChangeDispatcher::DispatchChange(const CookieChangeInfo& change,
                                                    bool notify_global_hooks) {
  DispatchChangeToDomainKey(change, DomainKey(change.cookie.Domain()));
  if (notify_global_hooks)
    DispatchChangeToDomainKey(change, std::string(kGlobalDomainKey));
}
```
For each registered listener whose URL/name filter matches, the callback fires with `CookieChangeInfo{cookie, access_result, cause=CookieChangeCause::INSERTED}`.

**T+5ms (background thread)**: SQLite write completes. Cookie is now persisted.

**T+5ms (back to network thread)**: The Cookie: request header for future requests to `example.com` will include `session_id=abc123`.

---

## 1.2 The Data Structures

### 1.2.1 ParsedCookie (raw header parse)

**File**: `net/cookies/parsed_cookie.h:24-226`

```cpp
class NET_EXPORT ParsedCookie {
 public:
  static const size_t kMaxCookieNamePlusValueSize = 4096;
  static const size_t kMaxCookieAttributeValueSize = 1024;

  explicit ParsedCookie(std::string_view cookie_line,
                        CookieInclusionStatus* status_out = nullptr);

  bool IsValid() const;
  const std::string& Name() const  { return pairs_[0].first; }
  const std::string& Value() const { return pairs_[0].second; }
  std::optional<std::string_view> Path() const;
  std::optional<std::string_view> Domain() const;
  std::optional<std::string_view> Expires() const;
  std::optional<std::string_view> MaxAge() const;
  bool IsSecure() const      { return secure_index_ != 0; }
  bool IsHttpOnly() const    { return httponly_index_ != 0; }
  std::pair<CookieSameSite, CookieSameSiteString> SameSite() const;
  CookiePriority Priority() const;
  bool IsPartitioned() const { return partitioned_index_ != 0; }
  bool HasInternalHtab() const { return internal_htab_; }

 private:
  std::vector<std::pair<std::string, std::string>> pairs_;
  size_t path_index_ = 0;
  size_t domain_index_ = 0;
  size_t expires_index_ = 0;
  size_t maxage_index_ = 0;
  size_t secure_index_ = 0;
  size_t httponly_index_ = 0;
  size_t same_site_index_ = 0;
  size_t priority_index_ = 0;
  size_t partitioned_index_ = 0;
  bool internal_htab_ = false;
};
```

**Field-by-field**:
- `pairs_`: raw (name, value) tuples in source order. Index 0 is always the cookie name+value. Indices 1+ are attributes (lowercased).
- `*_index_`: 0 means "absent", else points into `pairs_`. Set in `SetupAttributes()` (`parsed_cookie.cc:695-720`).
- `internal_htab_`: true if a horizontal tab was stripped from a value (used for compliance warnings).

### 1.2.2 CanonicalCookie (validated, canonical form)

**File**: `net/cookies/canonical_cookie.h:67-482`

Combined fields from `CookieBase` (`cookie_base.h:32`) + `CanonicalCookie`:

```cpp
class NET_EXPORT CanonicalCookie final {
 public:
  // Construction is gated by PassKey so only static factories can build one
  CanonicalCookie(base::PassKey<CanonicalCookie>,
                  std::string name, std::string value,
                  std::string domain, std::string path,
                  base::Time creation, base::Time expiration,
                  base::Time last_access, base::Time last_update,
                  bool secure, bool httponly,
                  CookieSameSite same_site,
                  CookiePriority priority,
                  std::optional<CookiePartitionKey> partition_key,
                  CookieSourceScheme source_scheme,
                  int source_port,
                  CookieSourceType source_type);

  // === Static factories ===
  static std::unique_ptr<CanonicalCookie> Create(
      const GURL& url, std::string_view cookie_line,
      base::Time creation_time,
      std::optional<base::Time> server_time,
      std::optional<CookiePartitionKey> cookie_partition_key,
      CookieSourceType source_type,
      CookieInclusionStatus* status);

  static std::unique_ptr<CanonicalCookie> CreateSanitizedCookie(
      const GURL& url, const std::string& name, const std::string& value,
      const std::string& domain, const std::string& path,
      base::Time creation, base::Time expiration, base::Time last_access,
      bool secure, bool http_only, CookieSameSite same_site,
      CookiePriority priority,
      std::optional<CookiePartitionKey> partition_key,
      CookieInclusionStatus* status);

  // === Accessors ===
  const std::string& Name() const        { return name_; }
  std::string Value() const;             // may decrypt OS-keychain-bound value
  const std::string& Domain() const      { return domain_; }
  const std::string& Path() const        { return path_; }
  base::Time CreationDate() const         { return creation_date_; }
  base::Time ExpiryDate() const          { return expiry_date_; }
  base::Time LastAccessDate() const      { return last_access_date_; }
  base::Time LastUpdateDate() const       { return last_update_date_; }
  bool SecureAttribute() const           { return secure_; }
  bool IsHttpOnly() const                { return httponly_; }
  CookieSameSite SameSite() const       { return same_site_; }
  CookiePriority Priority() const        { return priority_; }
  CookieSourceType SourceType() const    { return source_type_; }
  CookieSourceScheme SourceScheme() const { return source_scheme_; }
  int SourcePort() const                 { return source_port_; }
  const std::optional<CookiePartitionKey>& PartitionKey() const { return partition_key_; }

  bool IsPersistent() const { return !expiry_date_.is_null(); }
  bool IsExpired(base::Time current) const {
    return !expiry_date_.is_null() && current >= expiry_date_;
  }
  bool IsDomainCookie() const { return !domain_.empty() && domain_[0] == '.'; }
  bool IsHostOnlyCookie() const { return !IsDomainCookie(); }
  bool IsPartitioned() const { return partition_key_.has_value(); }

  // === Equivalence (used for overwrite detection) ===
  bool IsEquivalent(const CanonicalCookie& ecc) const {
    return RefUniqueKey() == ecc.RefUniqueKey();
  }
  bool IsEquivalentForSecureCookieMatching(const CanonicalCookie& secure_cookie) const;
  bool IsProbablyEquivalentTo(const CanonicalCookie& other) const;

  // === Inclusion logic (the heart of SameSite/Secure/HttpOnly checks) ===
  CookieAccessResult IncludeForRequestURL(
      const GURL& url, const CookieOptions& options,
      const CookieAccessParams& params) const;       // in CookieBase
  CookieAccessResult IsSetPermittedInContext(
      const GURL& source_url, const CookieOptions& options,
      const CookieAccessParams& params,
      const std::vector<std::string>& cookieable_schemes,
      std::optional<CookieAccessResult> cookie_access_result) const;

  // === Serialization ===
  static std::string BuildCookieLine(const CookieList& cookies);
  static std::string BuildCookieLine(const CookieAccessResultList& cookies);
  static std::string BuildCookieAttributesLine(const CanonicalCookie& cookie);

 private:
  // From CookieBase
  std::string   name_;
  std::string   domain_;
  std::string   path_;
  base::Time    creation_date_;
  bool          secure_{false};
  bool          httponly_{false};
  CookieSameSite             same_site_{CookieSameSite::NO_RESTRICTION};
  std::optional<CookiePartitionKey> partition_key_;
  CookieSourceScheme         source_scheme_{CookieSourceScheme::kUnset};
  int          source_port_{url::PORT_UNSPECIFIED};

  // CanonicalCookie's own
  std::optional<crypto::ProcessBoundString> value_;   // may be OS-encrypted
  base::Time   expiry_date_;
  base::Time   last_access_date_;
  base::Time   last_update_date_;
  CookiePriority    priority_{COOKIE_PRIORITY_MEDIUM};
  CookieSourceType  source_type_{CookieSourceType::kOther};
};
```

### 1.2.3 CookiePartitionKey (CHIPS)

**File**: `net/cookies/cookie_partition_key.h:29-226`

```cpp
class NET_EXPORT CookiePartitionKey {
 public:
  enum class AncestorChainBit {
    kSameSite = 0,    // All ancestor frames same-site
    kCrossSite = 1,   // At least one cross-site ancestor
  };

  static CookiePartitionKey FromWire(
      const SchemefulSite& site,
      AncestorChainBit ancestor_chain_bit,
      std::optional<base::UnguessableToken> nonce = std::nullopt);

  static std::optional<CookiePartitionKey> FromNetworkIsolationKey(
      const NetworkIsolationKey& network_isolation_key,
      const SiteForCookies& site_for_cookies,
      const SchemefulSite& request_site,
      bool main_frame_navigation);

  // === Serialization for SQLite storage ===
  [[nodiscard]] static base::expected<SerializedCookiePartitionKey, std::string>
      Serialize(base::optional_ref<const CookiePartitionKey> in);
  [[nodiscard]] static base::expected<std::optional<CookiePartitionKey>, std::string>
      FromStorage(const std::string& top_level_site, bool has_cross_site_ancestor);
  [[nodiscard]] static base::expected<CookiePartitionKey, std::string>
      FromUntrustedInput(const std::string& top_level_site, bool has_cross_site_ancestor);

  bool IsThirdParty() const { return ancestor_chain_bit_ == AncestorChainBit::kCrossSite; }

 private:
  SchemefulSite site_;                            // top-level site (eTLD+1 + scheme)
  std::optional<base::UnguessableToken> nonce_;   // opaque-origin partition
  AncestorChainBit ancestor_chain_bit_ = AncestorChainBit::kCrossSite;
};
```

**On-disk format**: `top_frame_site_key` column = `Serialize()` output. For `"https://example.com"` partition with cross-site ancestor: string is `"https://example.com"` + `has_cross_site_ancestor=true`. Empty string = unpartitioned.

### 1.2.4 CookieMonster (the in-memory store)

**File**: `net/cookies/cookie_monster.h:125-141`

```cpp
class CookieMonster : public CookieStore {
 public:
  // ... public API ...

 private:
  using CookieMap = std::multimap<std::string, std::unique_ptr<CanonicalCookie>>;
  using CookieMapItPair = std::pair<CookieMap::iterator, CookieMap::iterator>;
  using CookieItVector = std::vector<CookieMap::iterator>;
  using CookieItList = std::list<CookieMap::iterator>;

  // === The two stores ===
  CookieMap cookies_;                              // unpartitioned, keyed by eTLD+1

  using PartitionedCookieMap =
      std::map<CookiePartitionKey, std::unique_ptr<CookieMap>>;
  PartitionedCookieMap partitioned_cookies_;       // CHIPS — double-keyed

  // === Stats ===
  size_t num_cookies_ = 0;
  size_t num_partitioned_cookies_ = 0;
  size_t num_keys_ = 0;
  size_t num_cookies_bytes_ = 0;
  size_t num_partitioned_cookies_bytes_ = 0;
  std::map<CookiePartitionKey, size_t> bytes_per_cookie_partition_;

  // === Async loading ===
  std::set<std::string> keys_loaded_;
  std::map<std::string, base::circular_deque<base::OnceClosure>> tasks_pending_for_key_;
  base::circular_deque<base::OnceClosure> tasks_pending_;
  bool initialized_ = false;
  bool persist_session_cookies_ = false;

  // === Persistent storage (refcounted, may be null in tests) ===
  scoped_refptr<PersistentCookieStore> store_;

  // === Change dispatcher ===
  CookieMonsterChangeDispatcher change_dispatcher_;

  // === Constants (defined at cookie_monster.cc:229-245) ===
  static const size_t kDomainMaxCookies = 180;          // per eTLD+1
  static const size_t kDomainPurgeCookies = 30;
  static const size_t kMaxCookies = 3300;                // global
  static const size_t kPurgeCookies = 300;
  static const size_t kMaxDomainPurgedKeys = 100;
  static const size_t kPerPartitionDomainMaxCookieBytes = 10240;   // 10 KB
  static const size_t kPerPartitionDomainMaxCookies = 180;
  static const int kSafeFromGlobalPurgeDays = 30;
};
```

### 1.2.5 CookieInclusionStatus (the rejection reasoner)

**File**: `net/cookies/cookie_inclusion_status.h:29-348`

```cpp
enum class ExclusionReason {
  EXCLUDE_UNKNOWN_ERROR = 0,
  EXCLUDE_HTTP_ONLY = 1,                              // HttpOnly accessed via JS
  EXCLUDE_SECURE_ONLY = 2,                            // Secure over non-HTTPS
  EXCLUDE_DOMAIN_MISMATCH = 3,                        // URL host doesn't match
  EXCLUDE_NOT_ON_PATH = 4,                            // URL path doesn't match
  EXCLUDE_SAMESITE_STRICT = 5,
  EXCLUDE_SAMESITE_LAX = 6,
  EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX = 7,
  EXCLUDE_SAMESITE_NONE_INSECURE = 8,                 // SameSite=None without Secure
  EXCLUDE_USER_PREFERENCES = 9,                       // blocked by content settings
  EXCLUDE_FAILURE_TO_STORE = 10,
  EXCLUDE_NONCOOKIEABLE_SCHEME = 11,                  // e.g. file://, chrome://
  EXCLUDE_OVERWRITE_SECURE = 12,
  EXCLUDE_OVERWRITE_HTTP_ONLY = 13,
  EXCLUDE_INVALID_DOMAIN = 14,
  EXCLUDE_INVALID_PREFIX = 15,                        // __Host- / __Secure- rules
  EXCLUDE_INVALID_PARTITIONED = 16,                  // Partitioned without __Host-
  EXCLUDE_NAME_VALUE_PAIR_EXCEEDS_MAX_SIZE = 17,     // > 4096 bytes
  EXCLUDE_ATTRIBUTE_VALUE_EXCEEDS_MAX_SIZE = 18,
  EXCLUDE_DOMAIN_NON_ASCII = 19,
  EXCLUDE_THIRD_PARTY_BLOCKED_WITHIN_FIRST_PARTY_SET = 20,
  EXCLUDE_PORT_MISMATCH = 21,                         // Origin-Bound Cookies
  EXCLUDE_SCHEME_MISMATCH = 22,                       // Origin-Bound Cookies
  EXCLUDE_SHADOWING_DOMAIN = 23,
  EXCLUDE_DISALLOWED_CHARACTER = 24,
  EXCLUDE_THIRD_PARTY_PHASEOUT = 25,                  // 3PCD phase-out
  EXCLUDE_NO_COOKIE_CONTENT = 26,
  EXCLUDE_ANONYMOUS_CONTEXT = 27,
  EXCLUDE_INVALID_PATH = 28,
  EXCLUDE_AMBIGUOUS_SERIALIZATION = 29,
  MAX_EXCLUSION_REASON = EXCLUDE_AMBIGUOUS_SERIALIZATION
};

enum class ExemptionReason {
  kNone = 0,
  kUserSetting = 1,
  kEnterprisePolicy = 6,
  kStorageAccess = 7,                                  // storage-access API
  kTopLevelStorageAccess = 8,
  kScheme = 9,
  kSameSiteNoneCookiesInSandbox = 10,
};

class CookieInclusionStatus {
 public:
  bool IsInclude() const;
  bool HasExclusionReason(ExclusionReason) const;
  void AddExclusionReason(ExclusionReason);
  void RemoveExclusionReason(ExclusionReason);
  bool HasWarningReason(WarningReason) const;
  void AddWarningReason(WarningReason);

 private:
  ExclusionReasonBitset exclusion_reasons_;           // base::EnumSet
  WarningReasonBitset  warning_reasons_;
  ExemptionReason      exemption_reason_ = ExemptionReason::kNone;
};
```

### 1.2.6 CookieOptions (the per-call config)

**File**: `net/cookies/cookie_options.h:18-273`

```cpp
class NET_EXPORT CookieOptions {
 public:
  class NET_EXPORT SameSiteCookieContext {
   public:
    enum class ContextType {
      CROSS_SITE = 0,
      SAME_SITE_LAX_METHOD_UNSAFE = 1,
      SAME_SITE_LAX = 2,
      SAME_SITE_STRICT = 3,
      COUNT
    };
    static SameSiteCookieContext MakeInclusive();        // SAME_SITE_STRICT
    static SameSiteCookieContext MakeInclusiveForSet();  // SAME_SITE_LAX

    ContextType GetContextForCookieInclusion() const;   // returns schemeful (strict)

   private:
    ContextType context_;             // schemeless (looser)
    ContextType schemeful_context_;   // schemeful (stricter)
    ContextMetadata metadata_;
    ContextMetadata schemeful_metadata_;
  };

  CookieOptions();  // default: exclude_httponly=true, context=CROSS_SITE

  void set_exclude_httponly();
  void set_include_httponly();
  bool exclude_httponly() const;

  void set_same_site_cookie_context(const SameSiteCookieContext&);
  const SameSiteCookieContext& same_site_cookie_context() const;

  void set_update_access_time();
  void set_do_not_update_access_time();
  bool update_access_time() const;

  void set_return_excluded_cookies();
  bool return_excluded_cookies() const;

  static CookieOptions MakeAllInclusive();  // include httponly, MakeInclusive

 private:
  bool exclude_httponly_ = true;
  SameSiteCookieContext same_site_cookie_context_;
  bool update_access_time_ = true;
  bool return_excluded_cookies_ = false;
};
```

### 1.2.7 CookieChangeInfo (the notification payload)

**File**: `net/cookies/cookie_change_dispatcher.h:51-66`

```cpp
struct CookieChangeInfo {
  CanonicalCookie cookie;
  CookieAccessResult access_result;
  CookieChangeCause cause = CookieChangeCause::EXPLICIT;
};

enum class CookieChangeCause {
  INSERTED,
  EXPLICIT,
  UNKNOWN_DELETION,
  OVERWRITE,
  EXPIRED,
  EVICTED,                              // GC'd due to limits
  EXPIRED_OVERWRITE,
  INSERTED_NO_CHANGE_OVERWRITE,
  INSERTED_NO_VALUE_CHANGE_OVERWRITE,
};
```

### 1.2.8 The SQLite Table Schema

The SQLitePersistentCookieStore (lives at `//net/extras/sqlite/sqlite_persistent_cookie_store.h`, not in your slice but documented in `net/cookies/README.md:293`) uses this schema:

```sql
CREATE TABLE cookies (
  creation_utc         INTEGER NOT NULL,             -- base::Time internal value
  host_key             TEXT NOT NULL,                -- CanonicalCookie::Domain()
                                                    -- (".example.com" for domain cookies)
  top_frame_site_key   TEXT NOT NULL DEFAULT '',    -- CookiePartitionKey::Serialize()
                                                    -- '' = unpartitioned
  name                 TEXT NOT NULL,
  value                TEXT NOT NULL,
  encrypted_value      BLOB DEFAULT '',              -- if OS-level encryption enabled
  path                 TEXT NOT NULL,
  expires_utc          INTEGER NOT NULL,             -- 0 for session cookies
  is_secure            INTEGER NOT NULL,
  is_httponly          INTEGER NOT NULL,
  last_access_utc      INTEGER NOT NULL,
  has_expires          INTEGER NOT NULL DEFAULT 1,
  is_persistent        INTEGER NOT NULL DEFAULT 1,
  priority             INTEGER NOT NULL DEFAULT 1,  -- CookiePriority enum
  samesite             INTEGER NOT NULL DEFAULT -1, -- CookieSameSite enum (-1=UNSPECIFIED)
  source_scheme        INTEGER NOT NULL DEFAULT 0,
  source_port          INTEGER NOT NULL DEFAULT 0,
  last_update_utc      INTEGER NOT NULL DEFAULT 0,
  is_same_party        INTEGER NOT NULL DEFAULT 0,  -- deprecated
  source_type          INTEGER NOT NULL DEFAULT 3   -- CookieSourceType
);
CREATE INDEX cookies_domain_index ON cookies(host_key);
CREATE INDEX cookies_scheme_index ON cookies(source_scheme);
```

### 1.2.9 Disk File Locations Per OS

| OS | Path |
|---|---|
| **Windows** | `%LOCALAPPDATA%\<App>\Network\Cookies` |
| **macOS** | `~/Library/Application Support/<App>/Network/Cookies` |
| **Linux** | `~/.config/<App>/Network/Cookies` |

For Qt6 WebEngine the `<App>` is whatever you set via `QStandardPaths::setTestModeEnabled()` or the application name set via `QCoreApplication::setApplicationName()`. The path is rooted at the StoragePartition path:

```
<browser_context_path> / <relative_partition_path> / Network / Cookies
```

Where `relative_partition_path_` is computed from `StoragePartitionConfig` (`storage_partition_impl.cc:1378`).

---

## 1.3 File Locations Reference

| Component | File Path |
|---|---|
| ParsedCookie | `net/cookies/parsed_cookie.cc` + `.h` |
| CanonicalCookie | `net/cookies/canonical_cookie.cc` + `.h` |
| CookieBase (parent) | `net/cookies/cookie_base.cc` + `.h` |
| CookieMonster | `net/cookies/cookie_monster.cc` + `.h` |
| CookieStore (base) | `net/cookies/cookie_store.cc` + `.h` |
| CookiePartitionKey (CHIPS) | `net/cookies/cookie_partition_key.cc` + `.h` |
| CookieConstants (enums) | `net/cookies/cookie_constants.cc` + `.h` |
| CookieOptions | `net/cookies/cookie_options.cc` + `.h` |
| CookieInclusionStatus | `net/cookies/cookie_inclusion_status.cc` + `.h` |
| CookieChangeDispatcher | `net/cookies/cookie_change_dispatcher.cc` + `.h` |
| CookieMonsterChangeDispatcher | `net/cookies/cookie_monster_change_dispatcher.cc` + `.h` |
| CookieDeletionInfo | `net/cookies/cookie_deletion_info.cc` + `.h` |
| CookieAccessDelegate | `net/cookies/cookie_access_delegate.cc` + `.h` |
| CookieUtil (helpers) | `net/cookies/cookie_util.cc` + `.h` |
| UniqueCookieKey | `net/cookies/unique_cookie_key.cc` + `.h` |
| RefUniqueCookieKey | `net/cookies/ref_unique_cookie_key.cc` + `.h` |
| SQLitePersistentCookieStore | `net/extras/sqlite/sqlite_persistent_cookie_store.cc` + `.h` |
| CDP StorageHandler (browser) | `browser/devtools/protocol/storage_handler.cc` + `.h` |
| CDP NetworkHandler (browser) | `browser/devtools/protocol/network_handler.cc` + `.h` |
| StoragePartitionImpl | `browser/storage_partition_impl.cc` + `.h` |
| CookieManager (mojo) | `services/network/cookie_manager.cc` + `.h` (in services/network, not in your slice) |
| document.cookie JS API | `third_party/blink/renderer/core/loader/cookie_jar.cc` + `.h` (referenced from net/cookies/README.md:399) |
| RestrictedCookieManager (mojo) | `services/network/restricted_cookie_manager.cc` + `.h` |

---

## 1.4 Class Diagram

```
                          ┌──────────────────────────┐
                          │   CookieStore            │
                          │   (net/cookies/          │
                          │    cookie_store.h:39)    │
                          │   abstract base          │
                          └────────────┬─────────────┘
                                       │
                                       ▼
              ┌───────────────────────────────────────────┐
              │   CookieMonster                            │
              │   (net/cookies/cookie_monster.h:61)       │
              │                                            │
              │   - cookies_: CookieMap                    │
              │   - partitioned_cookies_:                  │
              │       map<CookiePartitionKey,              │
              │           unique_ptr<CookieMap>>           │
              │   - store_: scoped_refptr<                 │
              │       PersistentCookieStore>               │
              │   - change_dispatcher_:                   │
              │       CookieMonsterChangeDispatcher       │
              └──────┬─────────────────────────┬───────────┘
                     │                          │
                     ▼                          ▼
   ┌─────────────────────────────┐  ┌─────────────────────────────┐
   │ PersistentCookieStore       │  │ CookieMonsterChangeDispatcher│
   │ (abstract, refcounted)     │  │ (cookie_monster_change_     │
   │ cookie_monster.h:929        │  │  dispatcher.h:30)            │
   └────────────┬────────────────┘  │                              │
                │                   │ - cookie_domain_map_:        │
                │                   │   map<string, CookieNameMap> │
                ▼                   │   (eTLD+1 → name → subs)     │
   ┌─────────────────────────────┐  └──────────────────────────────┘
   │ SQLitePersistentCookieStore │
   │ (net/extras/sqlite/)        │
   │ - background task runner   │
   │ - SQLite file on disk      │
   └─────────────────────────────┘


   ┌──────────────────────────┐         ┌──────────────────────────┐
   │ ParsedCookie             │         │ CanonicalCookie          │
   │ (parsed_cookie.h:24)    │         │ (canonical_cookie.h:67)  │
   │                          │         │                          │
   │ - pairs_: vector<       │ Create()│ - name_, domain_, path_  │
   │     pair<string,string>>│◀────────│ - secure_, httponly_     │
   │ - *_index_: size_t      │         │ - same_site_, priority_  │
   │                          │         │ - partition_key_         │
   │ SetupAttributes()        │         │ - source_scheme_,        │
   └──────────────────────────┘         │   source_port_,          │
                                        │   source_type_           │
                                        │ - value_: optional<      │
                                        │   ProcessBoundString>   │
                                        │                          │
                                        │ IncludeForRequestURL()   │
                                        │ IsSetPermittedInContext()│
                                        └──────────────────────────┘


   ┌─────────────────────────────┐         ┌─────────────────────────────┐
   │ CookiePartitionKey         │         │ CookieInclusionStatus       │
   │ (cookie_partition_key.h:29)│         │ (cookie_inclusion_status.h)  │
   │                             │         │                              │
   │ - site_: SchemefulSite     │         │ - exclusion_reasons_:        │
   │ - nonce_: optional<         │         │     EnumSet<ExclusionReason>│
   │     UnguessableToken>      │         │ - warning_reasons_:         │
   │ - ancestor_chain_bit_:     │         │     EnumSet<WarningReason>  │
   │     AncestorChainBit       │         │ - exemption_reason_         │
   │                             │         │                              │
   │ FromNetworkIsolationKey()  │         │ IsInclude()                 │
   │ Serialize() / FromStorage()│         │ AddExclusionReason()        │
   └─────────────────────────────┘         └─────────────────────────────┘


   ┌─────────────────────────────┐         ┌─────────────────────────────┐
   │ CookieOptions               │         │ CookieChangeInfo            │
   │ (cookie_options.h:18)       │         │ (cookie_change_dispatcher.h)│
   │                             │         │                              │
   │ - exclude_httponly_: bool  │         │ - cookie: CanonicalCookie   │
   │ - same_site_cookie_context_│         │ - access_result             │
   │ - update_access_time_       │         │ - cause: CookieChangeCause │
   │ - return_excluded_cookies_  │         │                              │
   │                             │         │ CookieChangeCause enum:    │
   │ MakeAllInclusive()          │         │   INSERTED, OVERWRITE,     │
   │ SameSiteCookieContext::     │         │   EXPIRED, EVICTED,         │
   │   MakeInclusive()           │         │   EXPLICIT, ...             │
   └─────────────────────────────┘         └─────────────────────────────┘
```

---

## 1.5 CDP Command & Event Reference

### 1.5.1 Storage Domain CDP Commands (browser-side)

| Command | Implementation | What it does |
|---|---|---|
| `Storage.getCookies` | `storage_handler.cc:435` | Returns ALL cookies in cookie jar (no filtering) |
| `Storage.setCookies` | `storage_handler.cc:464` | Sets a list of cookies (delegates to NetworkHandler::SetCookies) |
| `Storage.clearCookies` | `storage_handler.cc:491` | Clears ALL cookies in the browser context |
| `Storage.getStorageKeyForFrame` | `storage_handler.cc` | Returns the storage key for a frame (partition-aware) |
| `Storage.trackCookies` | `storage_handler.cc` | Subscribe to cookie changes for an origin |
| `Storage.untrackCookies` | `storage_handler.cc` | Unsubscribe from cookie changes |
| `Storage.trackIndexedDBForOrigin` | `storage_handler.cc:800` | Subscribe to IndexedDB list/content changes |
| `Storage.trackCacheStorageForOrigin` | `storage_handler.cc:601` | Subscribe to CacheStorage changes |

### 1.5.2 Storage Domain CDP Events

| Event | Payload | When fired |
|---|---|---|
| `Storage.cookieChanged` | `{deleted: bool, cookie: Cookie, cause: string}` | When ANY cookie in tracked origins changes |

### 1.5.3 Network Domain Cookie Commands

| Command | Implementation | Difference from Storage |
|---|---|---|
| `Network.getCookies` | `network_handler.cc:2355` | URL-scoped — applies SameSite/cookie inclusion logic. Returns only cookies that WOULD be sent to the URLs. |
| `Network.setCookie` | `network_handler.cc:2399` | Sets a single cookie (uses MakeInclusive SameSite context) |
| `Network.deleteCookies` | `network_handler.cc:2549` | Deletes by name+domain+path (deletes all matching) |
| `Network.clearBrowserCookies` | `network_handler.cc:2325` | Clears ALL cookies in the browser context |
| `Network.getBlockedCookies` | Returns cookies that were blocked by Set-Cookie rejection reasons | For debugging 3PCD |

### 1.5.4 Network Domain Cookie Events

| Event | Payload | When fired |
|---|---|---|
| `Network.responseReceivedExtraInfo` | `{blockedCookies: [{name, blockedReasons}], headers, resourceIPAddressSpace, statusCode, ...}` | When raw response headers (including Set-Cookie) arrive from the network |

### 1.5.5 Network.CookieParam (input to Network.setCookie)

```json
{
  "name": "session_id",
  "value": "abc123",
  "url": "https://example.com/",          // optional, alternative to domain+path
  "domain": ".example.com",               // optional
  "path": "/",                            // optional, default "/"
  "secure": true,                         // optional
  "httpOnly": true,                       // optional
  "sameSite": "Lax",                      // "Strict" | "Lax" | "None"
  "expires": 1735689600.0,               // optional, epoch seconds (-1 = session)
  "priority": "Medium",                   // "Low" | "Medium" | "High"
  "sameParty": false,                     // deprecated
  "sourceScheme": "Secure",               // "Unset" | "NonSecure" | "Secure"
  "sourcePort": 443,                      // optional
  "partitionKey": {                        // optional (CHIPS)
    "topLevelSite": "https://example.com",
    "hasCrossSiteAncestor": false
  }
}
```

### 1.5.6 Network.Cookie (returned by getCookies)

```json
{
  "name": "session_id",
  "value": "abc123",
  "domain": "example.com",
  "path": "/",
  "expires": 1735689600.0,                // -1 for session cookies
  "size": 14,                              // name.length + value.length
  "httpOnly": true,
  "secure": true,
  "session": false,                        // !IsPersistent
  "sameSite": "Lax",
  "priority": "Medium",
  "sourceScheme": "Secure",
  "sourcePort": 443,
  "partitionKey": "https://example.com",  // only if partitioned
  "partitionKeyOpaque": false
}
```

---

## 1.6 SameSite Enforcement Algorithm

The SameSite logic lives in `CookieBase::IncludeForRequestURL` (`cookie_base.cc:103-297`) and `CookieBase::IsSetPermittedInContext` (`cookie_base.cc:299-431`). The algorithm:

```
FUNCTION IncludeForRequestURL(url, options, params):

  1. Check HttpOnly exclusion:
     IF options.exclude_httponly() AND IsHttpOnly():
       ADD EXCLUDE_HTTP_ONLY

  2. Determine access scheme:
     - kNonCryptographic → can't access Secure cookies
     - kTrustworthy / kCryptographic → can access Secure cookies

  3. Check Secure-only:
     IF SecureAttribute() AND NOT is_allowed_to_access_secure_cookies:
       ADD EXCLUDE_SECURE_ONLY

  4. Check domain match:
     IF NOT IsDomainMatch(domain_, url.host()):
       ADD EXCLUDE_DOMAIN_MISMATCH

  5. Check path match:
     IF NOT IsOnPath(url.path()):
       ADD EXCLUDE_NOT_ON_PATH

  6. Determine effective SameSite (the "Lax+POST" mitigation):
     effective_same_site = GetEffectiveSameSite(access_semantics)

     SWITCH effective_same_site:
       CASE STRICT_MODE:
         IF cookie_inclusion_context < SAME_SITE_STRICT:
           ADD EXCLUDE_SAMESITE_STRICT

       CASE LAX_MODE:
         IF cookie_inclusion_context < SAME_SITE_LAX:
           IF SameSite() == UNSPECIFIED:
             ADD EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX
           ELSE:
             ADD EXCLUDE_SAMESITE_LAX

       CASE LAX_MODE_ALLOW_UNSAFE:  // recently-created UNSPECIFIED cookie
         IF cookie_inclusion_context < SAME_SITE_LAX_METHOD_UNSAFE:
           ADD EXCLUDE_SAMESITE_UNSPECIFIED_TREATED_AS_LAX

       CASE NO_RESTRICTION:
         // No SameSite exclusion

  7. Check SameSite=None requires Secure:
     IF access_semantics != LEGACY
        AND SameSite() == NO_RESTRICTION
        AND NOT SecureAttribute():
       ADD EXCLUDE_SAMESITE_NONE_INSECURE

  RETURN CookieAccessResult{status, is_allowed_to_access_secure_cookies}
```

### The "Lax+POST" Mitigation (recently-created cookies)

```cpp
CookieEffectiveSameSite CookieBase::GetEffectiveSameSite(
    CookieAccessSemantics access_semantics) const {
  base::TimeDelta threshold = GetLaxAllowUnsafeThresholdAge();
  switch (SameSite()) {
    case CookieSameSite::UNSPECIFIED:
      return (access_semantics == CookieAccessSemantics::LEGACY)
                 ? CookieEffectiveSameSite::NO_RESTRICTION
                 : (IsRecentlyCreated(threshold)
                        ? CookieEffectiveSameSite::LAX_MODE_ALLOW_UNSAFE
                        : CookieEffectiveSameSite::LAX_MODE);
    case CookieSameSite::NO_RESTRICTION:
      return CookieEffectiveSameSite::NO_RESTRICTION;
    case CookieSameSite::LAX_MODE:
      return CookieEffectiveSameSite::LAX_MODE;
    case CookieSameSite::STRICT_MODE:
      return CookieEffectiveSameSite::STRICT_MODE;
  }
}
```

The thresholds (`cookie_constants.cc:21-22`):
- `kLaxAllowUnsafeMaxAge = base::Minutes(2)` — default
- `kShortLaxAllowUnsafeMaxAge = base::Seconds(10)` — if `kShortLaxAllowUnsafeThreshold` feature is enabled

### Context Computation Helpers

Located at `net/cookies/cookie_util.h:314-368`:

| Helper | Used for |
|---|---|
| `ComputeSameSiteContextForRequest` | HTTP requests (with redirect chain, method, main-frame-ness) |
| `ComputeSameSiteContextForScriptGet` | `document.cookie` read |
| `ComputeSameSiteContextForResponse` | HTTP response (Set-Cookie write) |
| `ComputeSameSiteContextForScriptSet` | `document.cookie = "..."` write |
| `ComputeSameSiteContextForSubresource` | Subresource fetch (img, fetch, etc.) |

Each takes: `url_chain` (for cross-site redirect downgrade), `SiteForCookies` (the top-frame site), `initiator`, `is_main_frame_navigation`, `force_ignore_site_for_cookies`.

---

## 1.7 Qt6 WebEngine Integration

### 1.7.1 The Qt6 WebEngine Cookie Architecture

Qt6 WebEngine wraps Chromium's `network::mojom::CookieManager` mojo interface. Each `QWebEngineProfile` corresponds to a `BrowserContext`. Each profile's `cookieStore()` returns a `QWebEngineCookieStore` that proxies to the underlying `network::mojom::CookieManager`:

```
┌─────────────────────────────────────────────────────────────────┐
│ YOUR QT6 APP                                                    │
│                                                                 │
│   QWebEngineProfile ──▶ QWebEngineCookieStore                  │
│         │                       │                              │
│         │                       │  (Qt's C++ wrapper)         │
│         │                       ▼                              │
│         │             ┌─────────────────────────────┐          │
│         │             │ CookieManager (mojo proxy)  │          │
│         │             └────────────┬────────────────┘          │
│         │                          │ (mojo IPC)                 │
│         ▼                          ▼                            │
│   StoragePartitionImpl ──▶ network::mojom::CookieManager        │
│                                  │                              │
└──────────────────────────────────┼──────────────────────────────┘
                                   │ (in network service process)
                                   ▼
                          ┌────────────────────┐
                          │ CookieMonster      │
                          │ - cookies_         │
                          │ - partitioned_      │
                          │   cookies_          │
                          │ - store_ (SQLite)   │
                          └────────────────────┘
```

### 1.7.2 Qt6 Cookie API Basics

The Qt6 cookie API (`QWebEngineCookieStore`) is intentionally limited — it exposes only:
- `setCookie(cookie, origin)`
- `deleteCookie(cookie)`
- `deleteAllCookies()`
- `loadRequest()`
- `getAllCookies(callback)`  (Qt 6.4+)
- `cookieAdded` signal
- `cookieRemoved` signal

For full scraping power you need to bypass Qt's wrapper and speak CDP directly.

### 1.7.3 Full CDP-Based Cookie Manager (Qt6 C++ Implementation)

Here is a complete, production-ready Qt6 cookie manager that uses CDP for full control:

#### `CookieManager.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <optional>

struct Cookie {
    QString name;
    QString value;
    QString domain;
    QString path;
    qint64 expires = -1;        // epoch seconds; -1 = session
    bool secure = false;
    bool httpOnly = false;
    QString sameSite;           // "Strict" | "Lax" | "None" | ""
    QString priority;           // "Low" | "Medium" | "High"
    QString sourceScheme;       // "Unset" | "NonSecure" | "Secure"
    int sourcePort = 0;
    bool session = false;
    int size = 0;
    std::optional<QString> partitionKey;
    bool partitionKeyOpaque = false;
    
    // Convert to CDP JSON for Network.setCookie
    QJsonObject toCdpParam(const QUrl& defaultUrl = {}) const;
    
    // Parse from CDP JSON returned by Network.getCookies
    static Cookie fromCdpCookie(const QJsonObject& obj);
    
    // Unique key for de-duplication
    QString uniqueKey() const {
        return QString("%1|%2|%3|%4|%5")
            .arg(name, domain, path)
            .arg(secure ? "1" : "0")
            .arg(partitionKey.value_or(""));
    }
};

class CookieManager : public QObject {
    Q_OBJECT
public:
    explicit CookieManager(const QUrl& devtoolsUrl, QObject* parent = nullptr);
    ~CookieManager();
    
    // === High-level scraping operations ===
    
    // Get all cookies (Storage.getCookies — returns entire jar, no filtering)
    void getAllCookies(std::function<void(const QList<Cookie>&)> callback);
    
    // Get cookies that would be sent to URLs (Network.getCookies — SameSite-aware)
    void getCookiesForUrls(const QList<QUrl>& urls,
                          std::function<void(const QList<Cookie>&)> callback);
    
    // Set a single cookie (Network.setCookie — applies SameSite context)
    void setCookie(const Cookie& cookie, const QUrl& url,
                  std::function<void(bool success)> callback);
    
    // Set multiple cookies in parallel
    void setCookies(const QList<Cookie>& cookies, const QUrl& url,
                   std::function<void(int succeeded)> callback);
    
    // Delete by name+domain+path (Network.deleteCookies)
    void deleteCookie(const QString& name, const QString& domain,
                     const QString& path = "/",
                     std::function<void()> callback = {});
    
    // Delete all cookies matching a domain
    void deleteCookiesForDomain(const QString& domain,
                                std::function<void(int deleted)> callback);
    
    // Clear ALL cookies in browser context (Network.clearBrowserCookies)
    void clearAllCookies(std::function<void()> callback);
    
    // === Real-time change tracking ===
    
    // Subscribe to ALL cookie changes (Storage.trackCookies on each origin)
    void startTrackingChanges(const QStringList& origins);
    void stopTrackingChanges();
    
    // === Export / Import ===
    
    void exportAllToJson(std::function<void(const QJsonDocument&)> callback);
    void importFromJson(const QJsonDocument& doc,
                       std::function<void(int succeeded)> callback);
    
    // === Domain blocking (via Network.setBlockedURLs is for requests; for cookies
    // we intercept Set-Cookie via Fetch.enable) ===
    
    void blockCookieDomain(const QString& domain);
    void unblockCookieDomain(const QString& domain);
    
signals:
    void cookieAdded(const Cookie& cookie);
    void cookieRemoved(const Cookie& cookie, const QString& cause);
    void cookieChanged(const Cookie& oldCookie, const Cookie& newCookie);
    
private:
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    void handleMessage(const QString& message);
    
    QWebSocket* m_ws;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QString m_sessionId;   // for flattened sessions
    QSet<QString> m_blockedDomains;
};
```

#### `CookieManager.cpp` (key methods)

```cpp
#include "CookieManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QSet>

Cookie::Cookie() = default;

QJsonObject Cookie::toCdpParam(const QUrl& defaultUrl) const {
    QJsonObject param;
    param["name"] = name;
    param["value"] = value;
    
    if (!domain.isEmpty()) param["domain"] = domain;
    else if (defaultUrl.isValid()) param["url"] = defaultUrl.toString();
    
    if (!path.isEmpty()) param["path"] = path;
    if (secure) param["secure"] = true;
    if (httpOnly) param["httpOnly"] = true;
    if (!sameSite.isEmpty()) param["sameSite"] = sameSite;
    if (!priority.isEmpty()) param["priority"] = priority;
    if (!sourceScheme.isEmpty()) param["sourceScheme"] = sourceScheme;
    if (sourcePort > 0) param["sourcePort"] = sourcePort;
    if (expires > 0) param["expires"] = static_cast<double>(expires);
    if (partitionKey.has_value()) {
        QJsonObject pk;
        pk["topLevelSite"] = partitionKey.value();
        pk["hasCrossSiteAncestor"] = false;
        param["partitionKey"] = pk;
    }
    return param;
}

Cookie Cookie::fromCdpCookie(const QJsonObject& obj) {
    Cookie c;
    c.name = obj.value("name").toString();
    c.value = obj.value("value").toString();
    c.domain = obj.value("domain").toString();
    c.path = obj.value("path").toString();
    c.expires = static_cast<qint64>(obj.value("expires").toDouble(-1));
    c.size = obj.value("size").toInt();
    c.httpOnly = obj.value("httpOnly").toBool();
    c.secure = obj.value("secure").toBool();
    c.session = obj.value("session").toBool();
    c.sameSite = obj.value("sameSite").toString();
    c.priority = obj.value("priority").toString();
    c.sourceScheme = obj.value("sourceScheme").toString();
    c.sourcePort = obj.value("sourcePort").toInt();
    
    if (obj.contains("partitionKey")) {
        c.partitionKey = obj.value("partitionKey").toString();
        c.partitionKeyOpaque = obj.value("partitionKeyOpaque").toBool(false);
    }
    return c;
}

CookieManager::CookieManager(const QUrl& devtoolsUrl, QObject* parent)
    : QObject(parent), m_ws(new QWebSocket) {
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &CookieManager::handleMessage);
    connect(m_ws, &QWebSocket::connected, this, [this]{
        // Auto-attach to the page target on connect
        // (already implicit if URL is /devtools/page/<id>)
    });
    m_ws->open(devtoolsUrl);
}

CookieManager::~CookieManager() {
    if (m_ws->isValid()) m_ws->close();
}

void CookieManager::sendCommand(const QString& method, const QJsonObject& params,
                                std::function<void(const QJsonObject&)> callback) {
    const int id = m_nextId++;
    if (callback) m_callbacks[id] = callback;
    
    QJsonObject msg;
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) msg["params"] = params;
    if (!m_sessionId.isEmpty()) msg["sessionId"] = m_sessionId;
    
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void CookieManager::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response to a command we sent
    if (doc.contains("id")) {
        const int id = doc.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);
            if (cb) cb(doc.value("result").toObject());
        }
        return;
    }
    
    // Event
    const QString method = doc.value("method").toString();
    const QJsonObject params = doc.value("params").toObject();
    
    if (method == "Storage.cookieChanged") {
        const bool deleted = params.value("deleted").toBool();
        const QJsonObject cookieObj = params.value("cookie").toObject();
        const QString cause = params.value("cause").toString();
        const Cookie c = Cookie::fromCdpCookie(cookieObj);
        
        if (deleted) {
            emit cookieRemoved(c, cause);
        } else {
            // Check if this is an insert or update
            // (CDP fires INSERTED cause for both, OVERWRITE for updates)
            if (cause == "overwrite" || cause == "expiredOverwrite") {
                emit cookieChanged(c, c);  // old/new same in this simplified version
            } else {
                emit cookieAdded(c);
            }
        }
    }
}

// === Storage.getCookies (entire jar) ===
void CookieManager::getAllCookies(std::function<void(const QList<Cookie>&)> callback) {
    sendCommand("Storage.getCookies", {}, [callback](const QJsonObject& result) {
        QList<Cookie> cookies;
        const QJsonArray arr = result.value("cookies").toArray();
        for (const QJsonValue& v : arr) {
            cookies.append(Cookie::fromCdpCookie(v.toObject()));
        }
        callback(cookies);
    });
}

// === Network.getCookies (URL-scoped) ===
void CookieManager::getCookiesForUrls(const QList<QUrl>& urls,
                                       std::function<void(const QList<Cookie>&)> callback) {
    QJsonArray urlArr;
    for (const QUrl& u : urls) urlArr.append(u.toString());
    
    QJsonObject params;
    params["urls"] = urlArr;
    
    sendCommand("Network.getCookies", params, [callback](const QJsonObject& result) {
        QList<Cookie> cookies;
        const QJsonArray arr = result.value("cookies").toArray();
        for (const QJsonValue& v : arr) {
            cookies.append(Cookie::fromCdpCookie(v.toObject()));
        }
        // De-duplicate (same cookie may match multiple URLs)
        QHash<QString, Cookie> seen;
        for (const Cookie& c : cookies) seen[c.uniqueKey()] = c;
        callback(seen.values());
    });
}

// === Network.setCookie ===
void CookieManager::setCookie(const Cookie& cookie, const QUrl& url,
                              std::function<void(bool success)> callback) {
    QJsonObject params = cookie.toCdpParam(url);
    sendCommand("Network.setCookie", params, [callback](const QJsonObject& result) {
        callback(result.value("success").toBool(true));
    });
}

void CookieManager::setCookies(const QList<Cookie>& cookies, const QUrl& url,
                               std::function<void(int succeeded)> callback) {
    if (cookies.isEmpty()) { callback(0); return; }
    
    int remaining = cookies.size();
    int succeeded = 0;
    
    for (const Cookie& c : cookies) {
        setCookie(c, url, [&, callback](bool ok) {
            if (ok) ++succeeded;
            if (--remaining == 0) callback(succeeded);
        });
    }
}

// === Network.deleteCookies (by name+domain+path) ===
void CookieManager::deleteCookie(const QString& name, const QString& domain,
                                  const QString& path,
                                  std::function<void()> callback) {
    QJsonObject params;
    params["name"] = name;
    params["domain"] = domain;
    if (!path.isEmpty()) params["path"] = path;
    
    sendCommand("Network.deleteCookies", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void CookieManager::deleteCookiesForDomain(const QString& domain,
                                            std::function<void(int deleted)> callback) {
    // Strategy: getAllCookies → filter by domain → delete each
    getAllCookies([this, domain, callback](const QList<Cookie>& all) {
        QList<Cookie> matching;
        for (const Cookie& c : all) {
            // Match if domain equals or is a subdomain
            if (c.domain == domain ||
                c.domain.endsWith("." + domain) ||
                c.domain == "." + domain ||
                ("." + c.domain) == domain) {
                matching.append(c);
            }
        }
        
        if (matching.isEmpty()) { callback(0); return; }
        
        int remaining = matching.size();
        int deleted = 0;
        for (const Cookie& c : matching) {
            deleteCookie(c.name, c.domain, c.path, [&, callback]() {
                ++deleted;
                if (--remaining == 0) callback(deleted);
            });
        }
    });
}

void CookieManager::clearAllCookies(std::function<void()> callback) {
    sendCommand("Network.clearBrowserCookies", {}, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

// === Real-time change tracking ===
void CookieManager::startTrackingChanges(const QStringList& origins) {
    // Storage.trackCookies subscribes to cookie changes per origin
    for (const QString& origin : origins) {
        QJsonObject params;
        params["origin"] = origin;
        sendCommand("Storage.trackCookies", params);
    }
}

void CookieManager::stopTrackingChanges() {
    // There's no explicit untrack; call Storage.untrackCookies per origin
    // (or just close the session)
}

// === Export/Import ===
void CookieManager::exportAllToJson(std::function<void(const QJsonDocument&)> callback) {
    getAllCookies([callback](const QList<Cookie>& cookies) {
        QJsonArray arr;
        for (const Cookie& c : cookies) {
            QJsonObject obj;
            obj["name"] = c.name;
            obj["value"] = c.value;
            obj["domain"] = c.domain;
            obj["path"] = c.path;
            obj["expires"] = static_cast<double>(c.expires);
            obj["secure"] = c.secure;
            obj["httpOnly"] = c.httpOnly;
            obj["sameSite"] = c.sameSite;
            obj["priority"] = c.priority;
            obj["sourceScheme"] = c.sourceScheme;
            obj["sourcePort"] = c.sourcePort;
            if (c.partitionKey.has_value()) {
                obj["partitionKey"] = c.partitionKey.value();
            }
            arr.append(obj);
        }
        QJsonObject root;
        root["cookies"] = arr;
        root["exportedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        callback(QJsonDocument(root));
    });
}

void CookieManager::importFromJson(const QJsonDocument& doc,
                                   std::function<void(int succeeded)> callback) {
    const QJsonArray arr = doc.object().value("cookies").toArray();
    QList<Cookie> cookies;
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        Cookie c;
        c.name = obj.value("name").toString();
        c.value = obj.value("value").toString();
        c.domain = obj.value("domain").toString();
        c.path = obj.value("path").toString("/");
        c.expires = static_cast<qint64>(obj.value("expires").toDouble(-1));
        c.secure = obj.value("secure").toBool();
        c.httpOnly = obj.value("httpOnly").toBool();
        c.sameSite = obj.value("sameSite").toString();
        c.priority = obj.value("priority").toString("Medium");
        c.sourceScheme = obj.value("sourceScheme").toString("Secure");
        c.sourcePort = obj.value("sourcePort").toInt(0);
        if (obj.contains("partitionKey")) {
            c.partitionKey = obj.value("partitionKey").toString();
        }
        cookies.append(c);
    }
    
    setCookies(cookies, QUrl(), callback);
}

// === Domain blocking via Fetch interception ===
void CookieManager::blockCookieDomain(const QString& domain) {
    m_blockedDomains.insert(domain);
    // We use Fetch.enable to intercept Set-Cookie headers
    // Pattern: any URL on this domain, response stage
    QJsonObject pattern;
    pattern["urlPattern"] = QString("*://%1/*").arg(domain);
    pattern["requestStage"] = "Response";
    
    QJsonObject params;
    QJsonArray patterns;
    patterns.append(pattern);
    params["patterns"] = patterns;
    
    sendCommand("Fetch.enable", params);
}

void CookieManager::unblockCookieDomain(const QString& domain) {
    m_blockedDomains.remove(domain);
    // Note: Fetch.disable would disable ALL interception; we need to re-enable
    // with the remaining patterns. For simplicity, do that here:
    if (m_blockedDomains.isEmpty()) {
        sendCommand("Fetch.disable", {});
    } else {
        QJsonArray patterns;
        for (const QString& d : m_blockedDomains) {
            QJsonObject p;
            p["urlPattern"] = QString("*://%1/*").arg(d);
            p["requestStage"] = "Response";
            patterns.append(p);
        }
        QJsonObject params;
        params["patterns"] = patterns;
        sendCommand("Fetch.enable", params);
    }
}
```

### 1.7.4 Using the Cookie Manager from Your Scraper

```cpp
// In your main scraper:
auto* cookieMgr = new CookieManager(QUrl("ws://127.0.0.1:9222/devtools/page/<id>"));

// Connect signals
connect(cookieMgr, &CookieManager::cookieAdded, [](const Cookie& c) {
    qDebug() << "Cookie added:" << c.name << "=" << c.value 
             << "for" << c.domain;
});
connect(cookieMgr, &CookieManager::cookieRemoved, [](const Cookie& c, const QString& cause) {
    qDebug() << "Cookie removed:" << c.name << "cause:" << cause;
});

// Snapshot all cookies for a domain before scraping
cookieMgr->getCookiesForUrls({QUrl("https://example.com/")}, 
    [](const QList<Cookie>& cookies) {
        qDebug() << "Got" << cookies.size() << "cookies for example.com";
        for (const Cookie& c : cookies) {
            qDebug() << "  " << c.name << "=" << c.value 
                     << "sameSite:" << c.sameSite
                     << "partition:" << c.partitionKey.value_or("(unpartitioned)");
        }
    });

// Inject a session cookie
Cookie session;
session.name = "session_id";
session.value = "injected-session-123";
session.domain = ".example.com";
session.path = "/";
session.secure = true;
session.httpOnly = true;
session.sameSite = "Lax";
session.sourceScheme = "Secure";
session.sourcePort = 443;

cookieMgr->setCookie(session, QUrl("https://example.com/"), [](bool ok) {
    qDebug() << "Cookie set:" << ok;
});

// Listen to ALL cookie changes
cookieMgr->startTrackingChanges({"https://example.com"});

// Export for stateless retry
cookieMgr->exportAllToJson([](const QJsonDocument& doc) {
    QFile f("cookies_backup.json");
    f.open(QIODevice::WriteOnly);
    f.write(doc.toJson());
    f.close();
});

// Block tracking cookies
cookieMgr->blockCookieDomain("doubleclick.net");
cookieMgr->blockCookieDomain("google-analytics.com");
```

---

## 1.8 Edge Cases

### 1.8.1 Cookie Path Edge Cases

| Scenario | Behavior |
|---|---|
| `Set-Cookie: x=1; Path=/api` | Cookie is sent to `/api`, `/api/v1`, `/api/users`. NOT sent to `/apis` or `/`. |
| `Set-Cookie: x=1; Path=/api/` (trailing slash) | Same as above. |
| `Set-Cookie: x=1` (no Path) | Defaults to "directory of URL" per RFC 6265bis §5.1.4. If URL is `https://example.com/foo/bar`, path = `/foo/`. If URL is `https://example.com/`, path = `/`. |
| `Set-Cookie: x=1; Path=/foo/../bar` | Path is normalized to `/bar` via URL canonicalization. |
| `Set-Cookie: x=1; Path=` (empty) | Treated as no Path attribute → defaults to directory of URL. |
| `Set-Cookie: x=1; Path=/foo/; Path=/bar/` | Second Path attribute wins (SetupAttributes walks `pairs_` and overwrites `path_index_`). |
| Cookie path = `/api`, URL = `/apiv1` | NO MATCH (path matching requires `/` separator or exact match). |

### 1.8.2 Domain Matching Edge Cases

| Cookie Domain | URL | Match? |
|---|---|---|
| `example.com` (host-only) | `https://example.com/` | ✓ |
| `example.com` (host-only) | `https://www.example.com/` | ✗ |
| `.example.com` (domain cookie) | `https://example.com/` | ✓ |
| `.example.com` (domain cookie) | `https://www.example.com/` | ✓ |
| `.example.com` (domain cookie) | `https://anotherexample.com/` | ✗ (no leading dot in host) |
| `192.168.1.1` (IP) | `https://192.168.1.1/` | ✓ |
| `.192.168.1.1` (IP with leading dot) | `https://192.168.1.1/` | ✗ (IPs cannot be domain cookies; ParsedCookie strips the dot) |
| `localhost` | `http://localhost:8080/` | ✓ |

### 1.8.3 SameSite Edge Cases

| Scenario | Default SameSite | Effective behavior |
|---|---|---|
| `Set-Cookie: x=1` (no SameSite) | UNSPECIFIED | Behaves as Lax after 2 minutes; Lax+POST (allows unsafe methods) for first 2 min |
| `Set-Cookie: x=1; SameSite=None` | NO_RESTRICTION | Sent on cross-site requests. REQUIRES Secure, otherwise EXCLUDE_SAMESITE_NONE_INSECURE |
| `Set-Cookie: x=1; SameSite=None` over HTTP | NO_RESTRICTION | Cookie rejected (EXCLUDE_SAMESITE_NONE_INSECURE) |
| `Set-Cookie: x=1; SameSite=Lax` | LAX_MODE | Sent on top-level navigation (GET), not on cross-site subresource requests |
| `Set-Cookie: x=1; SameSite=Strict` | STRICT_MODE | Only sent on same-site requests |
| `Set-Cookie: x=1; SameSite=invalid` | UNSPECIFIED + CookieSameSiteString::kUnrecognized | Behaves as Lax, but flagged as unrecognized |

### 1.8.4 CHIPS (Partitioned Cookies) Edge Cases

| Scenario | Behavior |
|---|---|
| `Set-Cookie: x=1; Partitioned` over HTTP | EXCLUDE_INVALID_PARTITIONED (must be Secure) |
| `Set-Cookie: x=1; Partitioned` without `__Host-` prefix | EXCLUDE_INVALID_PARTITIONED (must be __Host-prefixed) |
| `Set-Cookie: __Host-x=1; Path=/; Secure; Partitioned` | ✓ Stored in `partitioned_cookies_[partition_key]` |
| Partition key `CookiePartitionKey{site="https://example.com", has_cross_site_ancestor=false}` | First-party partitioned (top-level site is example.com, request is same-site) |
| Partition key `CookiePartitionKey{site="https://example.com", has_cross_site_ancestor=true}` | Third-party partitioned (top-level site is example.com, but request came from a cross-site iframe) |
| NetworkIsolationKey with nonce (e.g. opaque origin) | CookiePartitionKey gets `nonce_` set → not serializable → in-memory only (not persisted to SQLite) |

### 1.8.5 Limits and Garbage Collection

| Limit | Value | Action on exceed |
|---|---|---|
| Per eTLD+1 cookie count | 180 | GarbageCollect kicks in, purges 30 lowest-priority cookies |
| Global cookie count | 3300 | Global GC, preserves cookies accessed within 30 days |
| Per-partition cookie count | 180 | GarbageCollectPartitionedCookies |
| Per-partition byte size | 10240 bytes (10 KB) | Partition-specific GC |
| Per-cookie name+value size | 4096 bytes | EXCLUDE_NAME_VALUE_PAIR_EXCEEDS_MAX_SIZE |
| Per-attribute value size | 1024 bytes | EXCLUDE_ATTRIBUTE_VALUE_EXCEEDS_MAX_SIZE |
| Max expiry | 400 days | ValidateAndAdjustExpiryDate caps it |

### 1.8.6 Cookie Overwrite Behavior

When `Set-Cookie` is received for `(name, host, path, partition_key)` that already exists:
1. `MaybeDeleteEquivalentCookieAndUpdateStatus()` finds the existing cookie
2. The old cookie is removed with `CookieChangeCause::OVERWRITE`
3. The new cookie is inserted with `CookieChangeCause::INSERTED_NO_CHANGE_OVERWRITE` (if value same) or `CookieChangeCause::INSERTED` (if value changed)
4. **Creation date is preserved** if the new cookie has no creation date — see `creation_date_to_inherit` in `SetCanonicalCookie`
5. **LastAccessDate is preserved** similarly

### 1.8.7 Session vs Persistent Cookie Edge Cases

| Scenario | Behavior |
|---|---|
| `Set-Cookie: x=1` (no expiry) | Session cookie (`IsPersistent()` returns false, `expiry_date_` is null). NOT written to SQLite unless `persist_session_cookies_` is true. |
| `Set-Cookie: x=1; Max-Age=0` | Cookie is "already expired" → `SetCanonicalCookie` still calls `InternalInsertCookie` but with `already_expired=true`, then immediately removes it with `CookieChangeCause::EXPIRED`. Net effect: cookie is deleted if it existed. |
| `Set-Cookie: x=1; Expires=Wed, 21 Oct 2026 07:28:00 GMT` | Persistent cookie, expiry_date_ set, written to SQLite. |
| `Set-Cookie: x=1; Max-Age=999999999` (way too long) | `ValidateAndAdjustExpiryDate` caps at 400 days. |
| Browser restart with `persist_session_cookies_=true` | Session cookies are restored from SQLite (used by "continue where you left off" feature). |
| Incognito profile | `is_in_memory()` returns true → SQLitePersistentCookieStore is not created → all cookies live in memory only. |

### 1.8.8 document.cookie Synchronous Blocking

When JavaScript calls `document.cookie`, the renderer makes a **synchronous mojo call** to the browser process via `RestrictedCookieManager::GetCookiesString()`. This blocks the V8 isolate until the browser process responds.

**Implication for scraping**: if your scraper does `await page.evaluate("document.cookie")` and the cookie store is busy (e.g. loading from SQLite), the entire JS execution stalls. **Avoid `document.cookie` reads in scraping loops** — use `Network.getCookies` (async, doesn't block JS) instead.

### 1.8.9 Set-Cookie Header Edge Cases

| Header | ParsedCookie result |
|---|---|
| `Set-Cookie: =value` (empty name) | Nameless cookie. `pairs_[0] = ("", "value")`. Valid. |
| `Set-Cookie: name=` (empty value) | Valid. `pairs_[0] = ("name", "")`. |
| `Set-Cookie: name` (no `=`, no value) | Nameless cookie with name="name" treated as value. Per RFC 6265bis §5.2 step 5. |
| `Set-Cookie: name=value; Path` (no `=`) | `path_index_` = 0 (Path attribute without value is ignored). |
| `Set-Cookie: name=value; Path=/; Path=/bar` (duplicate) | Second wins (`SetupAttributes` overwrites). |
| `Set-Cookie: name=value; SameSite=Foo` (unrecognized) | `same_site_` = UNSPECIFIED, `CookieSameSiteString::kUnrecognized`. |
| `Set-Cookie: name=value; Expires=invalid` | `expires_index_` set, but `ParseExpiration` returns null → cookie becomes session cookie. |
| `Set-Cookie: name=value\r\nX-Injected: header` (CRLF injection) | `FindFirstTerminator` truncates at `\r\n`. Cookie name = "name", value = "value". The injected header is rejected. |
| `Set-Cookie: name\t=\tvalue` (HTABs around `=`) | `internal_htab_` = true; parsed as `name=value`. |
| `Set-Cookie: __Host-x=1; Path=/; Secure` | Prefix check passes. Cookie is host-only. |
| `Set-Cookie: __Host-x=1; Path=/api; Secure` | EXCLUDE_INVALID_PREFIX (Path must be `/` for __Host-). |
| `Set-Cookie: __Host-x=1; Domain=example.com` | EXCLUDE_INVALID_PREFIX (Domain forbidden for __Host-). |
| `Set-Cookie: __Secure-x=1; Secure` | ✓ (must be Secure, but Path/Domain can be anything). |

---

## 1.9 Performance Impact

### 1.9.1 Read Performance

| Operation | Cost | Notes |
|---|---|---|
| `CookieMonster::GetCookieListForUrl` | O(log N + K) where K = cookies for eTLD+1 | Map lookup by key (eTLD+1), then linear scan for path/domain match. Typically <0.1ms. |
| `CookieMonster::GetAllCookies` | O(N) where N = total cookies | Walks entire `cookies_` map + `partitioned_cookies_`. For ~3000 cookies: ~1-2ms. |
| `document.cookie` (JS) | O(K) + 1-2ms mojo sync roundtrip | Blocks V8 isolate. |
| `CookieMonster::SetCanonicalCookie` | O(log N + K) | Find + insert + write to SQLite (async). |
| SQLite write (background) | ~1-5ms per write | Batched into transactions if many writes. |
| `FlushStore()` | Forces pending writes to commit | Blocks until SQLite fsyncs. |

### 1.9.2 Memory Footprint

| Storage | Memory |
|---|---|
| Per CanonicalCookie | ~200-400 bytes (depends on string lengths) |
| 3300 cookies (max) | ~1-2 MB |
| CookieMap overhead | ~50 bytes per entry (multimap node) |
| PartitionedCookieMap overhead | ~100 bytes per partition entry |

### 1.9.3 Optimization Tips for Scraping

1. **Avoid `document.cookie` in tight loops** — use `Network.getCookies` once and cache the result.
2. **Batch cookie sets** — group multiple `setCookie` calls into a single `Storage.setCookies` (which loops internally but avoids N round-trips).
3. **Don't call `getAllCookies` on every page load** — subscribe to `cookieChanged` events and maintain your own mirror.
4. **For massive cookie exports** (>10,000 cookies across many profiles), use `Storage.getCookies` per profile and parallelize.
5. **Throttle `Storage.trackCookies` subscriptions** — only subscribe to origins you actually care about; each tracked origin has overhead.

### 1.9.4 LastAccess Throttling

```cpp
void CookieMonster::InternalUpdateCookieAccessTime(CanonicalCookie& cc, Time current) {
  // Throttle: only update if older than last_access_threshold_ (default 60s)
  if ((current - cc.LastAccessDate()) < last_access_threshold_) return;
  cc.SetLastAccessDate(current);
  if (ShouldUpdatePersistentStore(cc))
    store_->UpdateCookieAccessTime(cc);
}
```

The 60-second throttle (`kDefaultAccessUpdateThresholdSeconds`) prevents SQLite write storms when a page sends many requests to the same origin.

---

## 1.10 Security & Privacy Impact

### 1.10.1 What CDP Can Access

A CDP client connected to the browser target can:
- Read ALL cookies across ALL profiles and partitions (Storage.getCookies)
- Set cookies for ANY domain (Network.setCookie — no SameSite/origin restrictions because it uses MakeInclusive context)
- Delete any cookie (Network.deleteCookies)
- Bypass HttpOnly restrictions (Network.getCookies includes HttpOnly cookies)
- Read partitioned cookies from any partition (Storage.getCookies returns all)
- Read cookies from private browsing windows (if attached to an OTR profile)
- Subscribe to real-time cookie changes for any origin (Storage.trackCookies)

### 1.10.2 SameSite Bypass via CDP

When you call `Network.setCookie` via CDP, the implementation in `network_handler.cc:2439-2449` uses `CookieOptions::SameSiteCookieContext::MakeInclusive()` — which is the most permissive context (`SAME_SITE_STRICT`). This means:

- You can set `SameSite=None` cookies without `Secure` flag (which would normally be rejected)
- You can set cookies for any domain without origin checks
- You can set HttpOnly cookies from JS context (because CDP is treated as system)

**For scraping**: this is powerful — you can pre-load cookies for any domain before navigating there. **For privacy**: this is dangerous — never expose your CDP endpoint to untrusted clients.

### 1.10.3 Cookie Encryption at Rest

The `value_` field of `CanonicalCookie` is a `std::optional<crypto::ProcessBoundString>`. When the embedder configures encryption:
- **Windows**: uses DPAPI (Data Protection API)
- **macOS**: uses Keychain
- **Linux**: stored in plaintext by default (no OS-level encryption available)

The encryption is **process-bound** — only the browser process that wrote the cookie can decrypt it. If you copy the SQLite file to another machine, the encrypted values are unreadable.

**For scraping**: if you export cookies via CDP and re-import them on another machine, the values are transferred as plaintext (because CDP returns decrypted values). The destination machine will re-encrypt them with its own OS key.

### 1.10.4 Cross-Origin Cookie Isolation

Cookies are isolated per `StoragePartition`. Each `QWebEngineProfile` corresponds to a different StoragePartition with its own SQLite file. **Two QWebEngineProfiles have completely isolated cookie jars** — different SQLite files, different in-memory CookieMonsters.

For per-request isolation within one profile, Chromium uses `NetworkIsolationKey` (top-frame site + frame site + nonce). This is computed per-request and used to derive `CookiePartitionKey` for partitioned cookies. Unpartitioned cookies are shared across all NetworkIsolationKeys within the same StoragePartition.

### 1.10.5 Detection of CDP Cookie Access

A sophisticated anti-bot script can detect CDP-based cookie manipulation:
1. **Cookie set without `document.cookie` write**: if a cookie appears in `document.cookie` but no `document.cookie = "..."` setter was called, it was set externally (CDP).
2. **HttpOnly cookie read via JS**: if JS can read an HttpOnly cookie (it normally can't), CDP is involved.
3. **SameSite=None without Secure**: if such a cookie exists, it was set via CDP (browser would reject it normally).
4. **Cookie with future creation date**: if `creation_date_` is in the future, CDP set it.
5. **SourceScheme mismatch**: if a cookie has `sourceScheme=Secure` but the page is HTTP, CDP set it.

**For stealth scraping**: avoid setting cookies via CDP unless absolutely necessary. Prefer:
1. Navigate to the target URL first
2. Inject cookies via `document.cookie` JS calls (Runtime.evaluate) — they go through normal SameSite/origin checks
3. Use Fetch interception to inject `Cookie:` headers per-request without modifying the cookie jar

---

## 1.11 Testing

### 1.11.1 Unit Tests

```cpp
#include <QtTest>
#include "CookieManager.h"

class TestCookieManager : public QObject {
    Q_OBJECT
private slots:
    void testSetAndGetCookie();
    void testDeleteCookie();
    void testCookieChangeTracking();
    void testPartitionedCookies();
    void testSameSiteEnforcement();
};

void TestCookieManager::testSetAndGetCookie() {
    CookieManager mgr(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    // Wait for connection
    QSignalSpy spy(&mgr, &CookieManager::cookieAdded);
    QVERIFY(spy.wait(1000));
    
    Cookie c;
    c.name = "test_session";
    c.value = "abc123";
    c.domain = "example.com";
    c.path = "/";
    c.secure = true;
    c.httpOnly = true;
    c.sameSite = "Lax";
    
    QSemaphore sem;
    bool setOk = false;
    mgr.setCookie(c, QUrl("https://example.com/"), [&](bool ok) {
        setOk = ok;
        sem.release();
    });
    sem.acquire();
    QVERIFY(setOk);
    
    // Verify it's retrievable
    QList<Cookie> got;
    mgr.getCookiesForUrls({QUrl("https://example.com/")}, [&](const QList<Cookie>& cookies) {
        got = cookies;
        sem.release();
    });
    sem.acquire();
    
    QCOMPARE(got.size(), 1);
    QCOMPARE(got[0].name, "test_session");
    QCOMPARE(got[0].value, "abc123");
    QCOMPARE(got[0].httpOnly, true);
}

void TestCookieManager::testCookieChangeTracking() {
    CookieManager mgr(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    mgr.startTrackingChanges({"https://example.com"});
    
    QSignalSpy addedSpy(&mgr, &CookieManager::cookieAdded);
    QSignalSpy removedSpy(&mgr, &CookieManager::cookieRemoved);
    
    Cookie c;
    c.name = "tracked";
    c.value = "v1";
    c.domain = ".example.com";
    mgr.setCookie(c, QUrl("https://example.com/"));
    
    QVERIFY(addedSpy.wait(2000));
    QCOMPARE(addedSpy.count(), 1);
    
    mgr.deleteCookie("tracked", ".example.com");
    QVERIFY(removedSpy.wait(2000));
    QCOMPARE(removedSpy.count(), 1);
}
```

### 1.11.2 Integration Test: Round-Trip Export/Import

```cpp
void TestCookieManager::testExportImport() {
    CookieManager src(QUrl("ws://127.0.0.1:9222/devtools/page/src"));
    CookieManager dst(QUrl("ws://127.0.0.1:9222/devtools/page/dst"));
    
    // Set up some cookies in src
    QList<Cookie> cookies;
    for (int i = 0; i < 10; ++i) {
        Cookie c;
        c.name = QString("cookie_%1").arg(i);
        c.value = QString("value_%1").arg(i);
        c.domain = ".example.com";
        c.path = "/";
        c.secure = true;
        c.sameSite = "Lax";
        cookies.append(c);
    }
    
    QSemaphore sem;
    src.setCookies(cookies, QUrl("https://example.com/"), [&](int n){
        qDebug() << "Set" << n << "cookies";
        sem.release();
    });
    sem.acquire();
    
    // Export
    QJsonDocument doc;
    src.exportAllToJson([&](const QJsonDocument& d){
        doc = d;
        sem.release();
    });
    sem.acquire();
    
    // Clear destination
    dst.clearAllCookies([&](){ sem.release(); });
    sem.acquire();
    
    // Import
    int succeeded = 0;
    dst.importFromJson(doc, [&](int n){
        succeeded = n;
        sem.release();
    });
    sem.acquire();
    
    QCOMPARE(succeeded, 10);
    
    // Verify
    QList<Cookie> imported;
    dst.getAllCookies([&](const QList<Cookie>& all){
        imported = all;
        sem.release();
    });
    sem.acquire();
    
    QCOMPARE(imported.size(), 10);
}
```

### 1.11.3 Manual Test via Chrome DevTools

You can also verify cookie behavior manually using Chrome's DevTools:
1. Open Chrome with `--remote-debugging-port=9222`
2. Navigate to `chrome://devtools` or connect via WebSocket
3. In the DevTools Console, run:
   ```js
   // List all cookies
   await (await fetch('http://127.0.0.1:9222/json/list')).json()
   ```
4. Connect via WebSocket to `ws://127.0.0.1:9222/devtools/page/<id>`
5. Send `{"id":1,"method":"Network.getCookies","params":{"urls":["https://example.com/"]}}`

### 1.11.4 Test CHIPS Partitioned Cookies

```cpp
void TestCookieManager::testPartitionedCookies() {
    CookieManager mgr(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    Cookie partitioned;
    partitioned.name = "__Host-partitioned_session";
    partitioned.value = "abc123";
    partitioned.path = "/";
    partitioned.secure = true;
    partitioned.sameSite = "None";
    partitioned.partitionKey = "https://top-level-site.com";
    
    QSemaphore sem;
    bool ok = false;
    mgr.setCookie(partitioned, QUrl("https://target-site.com/"), [&](bool success){
        ok = success;
        sem.release();
    });
    sem.acquire();
    QVERIFY(ok);
    
    // Storage.getCookies should return it
    QList<Cookie> all;
    mgr.getAllCookies([&](const QList<Cookie>& cookies){
        all = cookies;
        sem.release();
    });
    sem.acquire();
    
    auto it = std::find_if(all.begin(), all.end(), [](const Cookie& c){
        return c.name == "__Host-partitioned_session";
    });
    QVERIFY(it != all.end());
    QVERIFY(it->partitionKey.has_value());
    QCOMPARE(it->partitionKey.value(), QString("https://top-level-site.com"));
}
```

---

## 1.12 Roadmap: Unique Features That Beat Puppeteer/Playwright

Based on this analysis, here are cookie-management features you can build that existing tools lack:

### 1.12.1 "Cookie Vault" — Encrypted Cross-Session Persistence

```cpp
class CookieVault {
public:
    // Export cookies with optional PGP encryption
    void exportEncrypted(const QString& pgpPublicKey,
                        std::function<void(const QByteArray&)> callback);
    
    // Import with decryption
    void importEncrypted(const QByteArray& encrypted,
                        const QString& pgpPrivateKey,
                        std::function<void(int succeeded)> callback);
};
```

### 1.12.2 "Cookie Time Machine" — Versioned Cookie History

```cpp
class CookieTimeMachine {
public:
    // Snapshot current cookie state every N seconds
    void startSnapshotting(int intervalSeconds);
    
    // Restore to a specific point in time
    void restoreTo(qint64 timestamp);
    
    // Diff two snapshots
    QList<CookieChange> diff(qint64 t1, qint64 t2);
};
```

### 1.12.3 "Cookie Firewall" — Real-Time Rule-Based Filtering

```cpp
class CookieFirewall {
public:
    // Block Set-Cookie by regex on name, value, or domain
    void addRule(const QString& namePattern, const QString& domainPattern,
                bool block);
    
    // Intercept Set-Cookie headers via Fetch.enable
    void enable();
    
    // Returns statistics: how many cookies blocked, by which rule
    CookieFirewallStats stats() const;
};
```

### 1.12.4 "Cookie Profile Switcher" — Instant Profile Swapping

```cpp
class CookieProfileSwitcher {
public:
    // Save current cookie state as a named profile
    void saveProfile(const QString& name);
    
    // Load a profile (replaces all cookies)
    void loadProfile(const QString& name);
    
    // Switch between profiles without page reload
    void switchTo(const QString& name);
    
    // List all saved profiles
    QStringList profiles() const;
};
```

### 1.12.5 "Cookie Inspector" — Real-Time Visualization

```cpp
class CookieInspector : public QAbstractTableModel {
public:
    // Columns: Name, Value, Domain, Path, Expires, SameSite, Secure,
    // HttpOnly, Partition, Source, LastAccessed, Created, Size
    int columnCount() const override { return 13; }
    
    // Real-time updates via cookieAdded/cookieRemoved signals
    void setCookieManager(CookieManager* mgr);
};
```

### 1.12.6 "Cookie Sync" — Multi-Browser Synchronization

```cpp
class CookieSync {
public:
    // Sync cookies between multiple QWebEngineProfile instances
    void syncProfiles(const QList<QWebEngineProfile*>& profiles);
    
    // Directional sync (one-way)
    void sync单向(QWebEngineProfile* from, QWebEngineProfile* to);
    
    // Selective sync (only cookies matching a filter)
    void syncFiltered(QWebEngineProfile* from, QWebEngineProfile* to,
                     const CookieFilter& filter);
};
```

---

## 1.13 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Get all cookies (no filtering) | `Storage.getCookies` | `storage_handler.cc:435` |
| Get cookies for URLs (SameSite-aware) | `Network.getCookies` | `network_handler.cc:2355` |
| Set one cookie | `Network.setCookie` | `network_handler.cc:2399` |
| Set multiple cookies | `Storage.setCookies` | `storage_handler.cc:464` |
| Delete by name+domain+path | `Network.deleteCookies` | `network_handler.cc:2549` |
| Clear all cookies | `Network.clearBrowserCookies` | `network_handler.cc:2325` |
| Track real-time changes | `Storage.trackCookies` | `storage_handler.cc` |
| Cookie change event | `Storage.cookieChanged` | (fired from CookieMonsterChangeDispatcher) |
| Get partitioned cookies | `Network.getCookies` with `partitionKey` param | `network_handler.cc:250-315` |

---

## End of Part 1

This concludes **Part 1: Cookie Storage & Management** — approximately 8,000 words covering the complete lifecycle, all data structures, file references, class diagrams, CDP command tables, the SameSite enforcement algorithm, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## What's Next?

**Part 2: Network Tab** (your #2 priority) will cover:
- The complete `Network.requestWillBeSent` → `loadingFinished` lifecycle
- `InspectorNetworkAgent` internals (the renderer-side capture)
- `NetworkHandler` internals (the browser-side handler)
- Body capture via `NetworkResourcesData` + `SegmentedBuffer`
- `Network.getResponseBody` / `getRequestPostData` implementations
- `Fetch.enable` + `Fetch.requestPaused` + `Fetch.continueRequest`/`fulfillRequest`/`failRequest`
- WebSocket frame capture (7 events)
- SSE capture
- Redirect handling
- Service Worker interception
- CORS error capture
- TLS/SSL security details
- HTTP/2 and HTTP/3 protocol detection
- Remote IP/port capture
- Full Qt6 C++ implementation of a `NetworkCapture` class
- Edge cases, performance, security, testing
- Unique features (request replay, response mocking, HAR export, etc.)

**Should I proceed with Part 2: Network Tab, or do you want to jump to a different priority (Runtime.evaluate, DOM, Page Lifecycle, Find File, etc.)?**

Also — if you want me to write Part 1 to a file on disk (e.g. `/home/z/my-project/download/cookies_part1.md`) so you have it as a reference document, just say the word.