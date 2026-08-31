#pragma once

#include <QObject>
#include <QHash>
#include "nothing/devtools/CdpClient.h"
#include "nothing/devtools/DevToolsModels.h"

namespace nothing {
namespace devtools {

/// StorageTracker — Tracks cookies, localStorage, sessionStorage, IndexedDB,
/// and CacheStorage. Provides read/write/delete operations for all storage types.
class StorageTracker : public QObject {
    Q_OBJECT
public:
    explicit StorageTracker(CdpClient* cdp, QObject* parent = nullptr);

    // === Cookies ===

    /// Get ALL cookies in the browser (no filtering).
    void getAllCookies(std::function<void(const QList<CookieInfo>&)> callback);

    /// Get cookies that would be sent to the given URLs.
    void getCookiesForUrls(const QList<QUrl>& urls,
                          std::function<void(const QList<CookieInfo>&)> callback);

    /// Set a single cookie.
    void setCookie(const CookieInfo& cookie, const QUrl& url,
                  std::function<void(bool success)> callback);

    /// Set multiple cookies.
    void setCookies(const QList<CookieInfo>& cookies, const QUrl& url,
                   std::function<void(int succeeded)> callback);

    /// Delete a cookie by name+domain+path.
    void deleteCookie(const QString& name, const QString& domain,
                      const QString& path = "/",
                      std::function<void()> callback = nullptr);

    /// Delete all cookies for a domain.
    void deleteCookiesForDomain(const QString& domain,
                               std::function<void(int deleted)> callback);

    /// Clear ALL cookies in the browser context.
    void clearAllCookies(std::function<void()> callback);

    /// Export all cookies to JSON.
    void exportCookiesToJson(std::function<void(const QJsonDocument&)> callback);

    /// Import cookies from JSON.
    void importCookiesFromJson(const QJsonDocument& doc,
                              std::function<void(int succeeded)> callback);

    // === DOM Storage (localStorage / sessionStorage) ===

    /// Enable DOM storage tracking.
    void enableDomStorage();

    /// Disable DOM storage tracking.
    void disableDomStorage();

    /// Get all items in a DOM storage area.
    void getDomStorageItems(const QString& securityOrigin, bool isLocalStorage,
                           std::function<void(const QList<StorageItem>&)> callback);

    /// Set a DOM storage item.
    void setDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                          const QString& key, const QString& value);

    /// Remove a DOM storage item.
    void removeDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                             const QString& key);

    /// Clear all items in a DOM storage area.
    void clearDomStorage(const QString& securityOrigin, bool isLocalStorage);

    // === IndexedDB ===

    /// Enable IndexedDB tracking.
    void enableIndexedDB();

    /// List all IndexedDB databases for an origin.
    void requestDatabaseNames(const QString& securityOrigin,
                             std::function<void(const QStringList&)> callback);

    /// Get the schema of a database.
    void requestDatabase(const QString& securityOrigin, const QString& databaseName,
                        std::function<void(const IndexedDBDatabase&)> callback);

    /// Read data from an object store (paginated).
    void requestData(const QString& securityOrigin, const QString& databaseName,
                    const QString& objectStoreName, int skipCount, int pageSize,
                    std::function<void(const QJsonArray&, bool hasMore)> callback);

    /// Clear all entries in an object store.
    void clearObjectStore(const QString& securityOrigin, const QString& databaseName,
                        const QString& objectStoreName);

    /// Delete an entire database.
    void deleteDatabase(const QString& securityOrigin, const QString& databaseName);

    // === CacheStorage ===

    /// List all caches for an origin.
    void requestCacheNames(const QString& securityOrigin,
                          std::function<void(const QList<QJsonObject>&)> callback);

    /// List entries in a cache.
    void requestCacheEntries(const QString& cacheId, int skipCount, int pageSize,
                            const QString& pathFilter,
                            std::function<void(const QList<CacheEntry>&, int)> callback);

    /// Delete a cache.
    void deleteCache(const QString& cacheId);

    /// Delete a specific entry from a cache.
    void deleteCacheEntry(const QString& cacheId, const QString& requestUrl);

    /// Get the response body of a cached entry.
    void requestCachedResponse(const QString& cacheId, const QString& requestUrl,
                              std::function<void(const QByteArray&)> callback);

    // === Storage tracking (browser-side change notifications) ===

    /// Track cookie changes for an origin.
    void trackCookies(const QString& origin);

    /// Untrack cookie changes.
    void untrackCookies(const QString& origin);

    /// Track IndexedDB changes for an origin.
    void trackIndexedDB(const QString& origin);

    /// Untrack IndexedDB changes.
    void untrackIndexedDB(const QString& origin);

    /// Track CacheStorage changes for an origin.
    void trackCacheStorage(const QString& origin);

    /// Untrack CacheStorage changes.
    void untrackCacheStorage(const QString& origin);

    /// Clear all storage for an origin.
    void clearDataForOrigin(const QString& origin,
                           const QStringList& storageTypes = {});

signals:
    // DOM Storage events
    void domStorageItemAdded(const QString& origin, bool isLocalStorage,
                            const QString& key, const QString& newValue);
    void domStorageItemRemoved(const QString& origin, bool isLocalStorage,
                              const QString& key);
    void domStorageItemUpdated(const QString& origin, bool isLocalStorage,
                              const QString& key, const QString& oldValue,
                              const QString& newValue);
    void domStorageItemsCleared(const QString& origin, bool isLocalStorage);

    // Storage tracking events (browser-side)
    void cookieChanged(const CookieInfo& cookie, bool deleted, const QString& cause);
    void indexedDBListUpdated(const QString& origin);
    void indexedDBContentUpdated(const QString& origin, const QString& databaseName);
    void cacheStorageListUpdated(const QString& origin);
    void cacheStorageContentUpdated(const QString& origin, const QString& cacheName);

private:
    void handleEvent(const QString& method, const QJsonObject& params, const QString& sessionId);

    static QList<CookieInfo> parseCookies(const QJsonArray& arr);
    static CookieInfo parseCookie(const QJsonObject& obj);
    static QList<StorageItem> parseStorageItems(const QJsonArray& arr);
    static QList<CacheEntry> parseCacheEntries(const QJsonArray& arr);

    CdpClient* m_cdp;
    bool m_domStorageEnabled = false;
};

} // namespace devtools
} // namespace nothing
