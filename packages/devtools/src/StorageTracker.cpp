#include "nothing/devtools/StorageTracker.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace nothing {
namespace devtools {

StorageTracker::StorageTracker(CdpClient* cdp, QObject* parent)
    : QObject(parent)
    , m_cdp(cdp)
{
    connect(m_cdp, &CdpClient::eventReceived,
            this, &StorageTracker::handleEvent);
}

// === Cookies ===

void StorageTracker::getAllCookies(std::function<void(const QList<CookieInfo>&)> callback)
{
    m_cdp->sendCommand("Storage.getCookies", {}, [callback](const QJsonObject& result) {
        if (callback) callback(parseCookies(result.value("cookies").toArray()));
    });
}

void StorageTracker::getCookiesForUrls(const QList<QUrl>& urls,
                                      std::function<void(const QList<CookieInfo>&)> callback)
{
    QJsonArray urlArr;
    for (const QUrl& u : urls) urlArr.append(u.toString());

    QJsonObject params;
    params["urls"] = urlArr;
    m_cdp->sendCommand("Network.getCookies", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseCookies(result.value("cookies").toArray()));
    });
}

void StorageTracker::setCookie(const CookieInfo& cookie, const QUrl& url,
                               std::function<void(bool)> callback)
{
    QJsonObject params;
    params["name"] = cookie.name;
    params["value"] = cookie.value;
    if (!cookie.domain.isEmpty()) params["domain"] = cookie.domain;
    else if (url.isValid()) params["url"] = url.toString();
    if (!cookie.path.isEmpty()) params["path"] = cookie.path;
    if (cookie.secure) params["secure"] = true;
    if (cookie.httpOnly) params["httpOnly"] = true;
    if (!cookie.sameSite.isEmpty()) params["sameSite"] = cookie.sameSite;
    if (!cookie.priority.isEmpty()) params["priority"] = cookie.priority;
    if (!cookie.sourceScheme.isEmpty()) params["sourceScheme"] = cookie.sourceScheme;
    if (cookie.sourcePort > 0) params["sourcePort"] = cookie.sourcePort;
    if (cookie.expires > 0) params["expires"] = cookie.expires;

    m_cdp->sendCommand("Network.setCookie", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("success").toBool(true));
    });
}

void StorageTracker::setCookies(const QList<CookieInfo>& cookies, const QUrl& url,
                                std::function<void(int)> callback)
{
    if (cookies.isEmpty()) { callback(0); return; }
    int remaining = cookies.size();
    int succeeded = 0;

    for (const CookieInfo& c : cookies) {
        setCookie(c, url, [&, callback](bool ok) {
            if (ok) ++succeeded;
            if (--remaining == 0) {
                if (callback) callback(succeeded);
            }
        });
    }
}

void StorageTracker::deleteCookie(const QString& name, const QString& domain,
                                  const QString& path,
                                  std::function<void()> callback)
{
    QJsonObject params;
    params["name"] = name;
    params["domain"] = domain;
    if (!path.isEmpty()) params["path"] = path;
    m_cdp->sendCommand("Network.deleteCookies", params, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void StorageTracker::deleteCookiesForDomain(const QString& domain,
                                            std::function<void(int)> callback)
{
    getAllCookies([this, domain, callback](const QList<CookieInfo>& all) {
        QList<CookieInfo> matching;
        for (const CookieInfo& c : all) {
            if (c.domain == domain || c.domain.endsWith("." + domain) ||
                c.domain == "." + domain || ("." + c.domain) == domain) {
                matching.append(c);
            }
        }
        if (matching.isEmpty()) { callback(0); return; }

        int remaining = matching.size();
        int deleted = 0;
        for (const CookieInfo& c : matching) {
            deleteCookie(c.name, c.domain, c.path, [&, callback]() {
                ++deleted;
                if (--remaining == 0) {
                    if (callback) callback(deleted);
                }
            });
        }
    });
}

void StorageTracker::clearAllCookies(std::function<void()> callback)
{
    m_cdp->sendCommand("Network.clearBrowserCookies", {}, [callback](const QJsonObject&) {
        if (callback) callback();
    });
}

void StorageTracker::exportCookiesToJson(std::function<void(const QJsonDocument&)> callback)
{
    getAllCookies([callback](const QList<CookieInfo>& cookies) {
        QJsonArray arr;
        for (const CookieInfo& c : cookies) {
            QJsonObject obj;
            obj["name"] = c.name;
            obj["value"] = c.value;
            obj["domain"] = c.domain;
            obj["path"] = c.path;
            obj["expires"] = c.expires;
            obj["secure"] = c.secure;
            obj["httpOnly"] = c.httpOnly;
            obj["sameSite"] = c.sameSite;
            obj["priority"] = c.priority;
            obj["sourceScheme"] = c.sourceScheme;
            obj["sourcePort"] = c.sourcePort;
            if (!c.partitionKey.isEmpty()) obj["partitionKey"] = c.partitionKey;
            arr.append(obj);
        }
        QJsonObject root;
        root["cookies"] = arr;
        root["exportedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        callback(QJsonDocument(root));
    });
}

void StorageTracker::importCookiesFromJson(const QJsonDocument& doc,
                                          std::function<void(int)> callback)
{
    const QJsonArray arr = doc.object().value("cookies").toArray();
    QList<CookieInfo> cookies;
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        CookieInfo c;
        c.name = obj.value("name").toString();
        c.value = obj.value("value").toString();
        c.domain = obj.value("domain").toString();
        c.path = obj.value("path").toString("/");
        c.expires = obj.value("expires").toDouble(-1);
        c.secure = obj.value("secure").toBool();
        c.httpOnly = obj.value("httpOnly").toBool();
        c.sameSite = obj.value("sameSite").toString();
        c.priority = obj.value("priority").toString("Medium");
        c.sourceScheme = obj.value("sourceScheme").toString("Secure");
        c.sourcePort = obj.value("sourcePort").toInt(0);
        if (obj.contains("partitionKey")) c.partitionKey = obj.value("partitionKey").toString();
        cookies.append(c);
    }
    setCookies(cookies, QUrl(), callback);
}

// === DOM Storage ===

void StorageTracker::enableDomStorage()
{
    if (m_domStorageEnabled) return;
    m_domStorageEnabled = true;
    m_cdp->sendCommand("DOMStorage.enable");
}

void StorageTracker::disableDomStorage()
{
    if (!m_domStorageEnabled) return;
    m_domStorageEnabled = false;
    m_cdp->sendCommand("DOMStorage.disable");
}

void StorageTracker::getDomStorageItems(const QString& securityOrigin, bool isLocalStorage,
                                       std::function<void(const QList<StorageItem>&)> callback)
{
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    m_cdp->sendCommand("DOMStorage.getDOMStorageItems", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseStorageItems(result.value("entries").toArray()));
    });
}

void StorageTracker::setDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                                      const QString& key, const QString& value)
{
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    params["key"] = key;
    params["value"] = value;
    m_cdp->sendCommand("DOMStorage.setDOMStorageItem", params);
}

void StorageTracker::removeDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                                         const QString& key)
{
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    params["key"] = key;
    m_cdp->sendCommand("DOMStorage.removeDOMStorageItem", params);
}

void StorageTracker::clearDomStorage(const QString& securityOrigin, bool isLocalStorage)
{
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    m_cdp->sendCommand("DOMStorage.clear", params);
}

// === IndexedDB ===

void StorageTracker::enableIndexedDB()
{
    m_cdp->sendCommand("IndexedDB.enable");
}

void StorageTracker::requestDatabaseNames(const QString& securityOrigin,
                                         std::function<void(const QStringList&)> callback)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    m_cdp->sendCommand("IndexedDB.requestDatabaseNames", params, [callback](const QJsonObject& result) {
        QStringList names;
        const QJsonArray arr = result.value("databaseNames").toArray();
        for (const QJsonValue& v : arr) names.append(v.toString());
        if (callback) callback(names);
    });
}

void StorageTracker::requestDatabase(const QString& securityOrigin, const QString& databaseName,
                                    std::function<void(const IndexedDBDatabase&)> callback)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    m_cdp->sendCommand("IndexedDB.requestDatabase", params, [callback](const QJsonObject& result) {
        IndexedDBDatabase db;
        const QJsonObject dbObj = result.value("databaseWithObjectStores").toObject();
        db.name = dbObj.value("name").toString();
        db.version = static_cast<qint64>(dbObj.value("version").toDouble());
        const QJsonArray stores = dbObj.value("objectStores").toArray();
        for (const QJsonValue& v : stores) {
            const QJsonObject so = v.toObject();
            IndexedDBObjectStore store;
            store.name = so.value("name").toString();
            store.keyPath = so.value("keyPath").toObject();
            store.autoIncrement = so.value("autoIncrement").toBool();
            store.indexes = so.value("indexes").toArray();
            db.objectStores.append(store);
        }
        if (callback) callback(db);
    });
}

void StorageTracker::requestData(const QString& securityOrigin, const QString& databaseName,
                                const QString& objectStoreName, int skipCount, int pageSize,
                                std::function<void(const QJsonArray&, bool)> callback)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    params["objectStoreName"] = objectStoreName;
    params["indexName"] = "";
    params["skipCount"] = skipCount;
    params["pageSize"] = pageSize;
    m_cdp->sendCommand("IndexedDB.requestData", params, [callback](const QJsonObject& result) {
        QJsonArray entries = result.value("objectStoreDataEntries").toArray();
        bool hasMore = result.value("hasMore").toBool();
        if (callback) callback(entries, hasMore);
    });
}

void StorageTracker::clearObjectStore(const QString& securityOrigin, const QString& databaseName,
                                     const QString& objectStoreName)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    params["objectStoreName"] = objectStoreName;
    m_cdp->sendCommand("IndexedDB.clearObjectStore", params);
}

void StorageTracker::deleteDatabase(const QString& securityOrigin, const QString& databaseName)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    m_cdp->sendCommand("IndexedDB.deleteDatabase", params);
}

// === CacheStorage ===

void StorageTracker::requestCacheNames(const QString& securityOrigin,
                                      std::function<void(const QList<QJsonObject>&)> callback)
{
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    m_cdp->sendCommand("CacheStorage.requestCacheNames", params, [callback](const QJsonObject& result) {
        QList<QJsonObject> caches;
        const QJsonArray arr = result.value("caches").toArray();
        for (const QJsonValue& v : arr) caches.append(v.toObject());
        if (callback) callback(caches);
    });
}

void StorageTracker::requestCacheEntries(const QString& cacheId, int skipCount, int pageSize,
                                        const QString& pathFilter,
                                        std::function<void(const QList<CacheEntry>&, int)> callback)
{
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["skipCount"] = skipCount;
    params["pageSize"] = pageSize;
    if (!pathFilter.isEmpty()) params["pathFilter"] = pathFilter;
    m_cdp->sendCommand("CacheStorage.requestEntries", params, [callback](const QJsonObject& result) {
        QList<CacheEntry> entries = parseCacheEntries(result.value("cacheDataEntries").toArray());
        int returnCount = result.value("returnCount").toInt();
        if (callback) callback(entries, returnCount);
    });
}

void StorageTracker::deleteCache(const QString& cacheId)
{
    QJsonObject params;
    params["cacheId"] = cacheId;
    m_cdp->sendCommand("CacheStorage.deleteCache", params);
}

void StorageTracker::deleteCacheEntry(const QString& cacheId, const QString& requestUrl)
{
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["request"] = QJsonObject{{"url", requestUrl}};
    m_cdp->sendCommand("CacheStorage.deleteEntry", params);
}

void StorageTracker::requestCachedResponse(const QString& cacheId, const QString& requestUrl,
                                          std::function<void(const QByteArray&)> callback)
{
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["requestUrl"] = requestUrl;
    m_cdp->sendCommand("CacheStorage.requestCachedResponse", params, [callback](const QJsonObject& result) {
        const QString body = result.value("response").toObject().value("body").toString();
        if (callback) callback(QByteArray::fromBase64(body.toUtf8()));
    });
}

// === Storage tracking ===

void StorageTracker::trackCookies(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.trackCookies", params);
}

void StorageTracker::untrackCookies(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.untrackCookies", params);
}

void StorageTracker::trackIndexedDB(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.trackIndexedDBForOrigin", params);
}

void StorageTracker::untrackIndexedDB(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.untrackIndexedDBForOrigin", params);
}

void StorageTracker::trackCacheStorage(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.trackCacheStorageForOrigin", params);
}

void StorageTracker::untrackCacheStorage(const QString& origin)
{
    QJsonObject params;
    params["origin"] = origin;
    m_cdp->sendCommand("Storage.untrackCacheStorageForOrigin", params);
}

void StorageTracker::clearDataForOrigin(const QString& origin, const QStringList& storageTypes)
{
    QJsonObject params;
    params["origin"] = origin;
    params["storageTypes"] = storageTypes.isEmpty() ? QString("all") : storageTypes.join(",");
    m_cdp->sendCommand("Storage.clearDataForOrigin", params);
}

// === Private ===

void StorageTracker::handleEvent(const QString& method, const QJsonObject& params, const QString&)
{
    // DOM Storage events
    if (method == "DOMStorage.domStorageItemAdded") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemAdded(sid.value("securityOrigin").toString(),
                                 sid.value("isLocalStorage").toBool(),
                                 params.value("key").toString(),
                                 params.value("newValue").toString());
    }
    else if (method == "DOMStorage.domStorageItemRemoved") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemRemoved(sid.value("securityOrigin").toString(),
                                   sid.value("isLocalStorage").toBool(),
                                   params.value("key").toString());
    }
    else if (method == "DOMStorage.domStorageItemUpdated") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemUpdated(sid.value("securityOrigin").toString(),
                                   sid.value("isLocalStorage").toBool(),
                                   params.value("key").toString(),
                                   params.value("oldValue").toString(),
                                   params.value("newValue").toString());
    }
    else if (method == "DOMStorage.domStorageItemsCleared") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemsCleared(sid.value("securityOrigin").toString(),
                                    sid.value("isLocalStorage").toBool());
    }
    // Cookie change events
    else if (method == "Storage.cookieChanged") {
        const bool deleted = params.value("deleted").toBool();
        const QJsonObject cookieObj = params.value("cookie").toObject();
        const QString cause = params.value("cause").toString();
        emit cookieChanged(parseCookie(cookieObj), deleted, cause);
    }
    // IndexedDB change events
    else if (method == "Storage.indexedDBListUpdated") {
        emit indexedDBListUpdated(params.value("origin").toString());
    }
    else if (method == "Storage.indexedDBContentUpdated") {
        emit indexedDBContentUpdated(params.value("origin").toString(),
                                     params.value("databaseName").toString());
    }
    // CacheStorage change events
    else if (method == "Storage.cacheStorageListUpdated") {
        emit cacheStorageListUpdated(params.value("origin").toString());
    }
    else if (method == "Storage.cacheStorageContentUpdated") {
        emit cacheStorageContentUpdated(params.value("origin").toString(),
                                        params.value("cacheName").toString());
    }
}

QList<CookieInfo> StorageTracker::parseCookies(const QJsonArray& arr)
{
    QList<CookieInfo> result;
    for (const QJsonValue& v : arr) {
        result.append(parseCookie(v.toObject()));
    }
    return result;
}

CookieInfo StorageTracker::parseCookie(const QJsonObject& obj)
{
    CookieInfo c;
    c.name = obj.value("name").toString();
    c.value = obj.value("value").toString();
    c.domain = obj.value("domain").toString();
    c.path = obj.value("path").toString();
    c.expires = obj.value("expires").toDouble(-1);
    c.size = obj.value("size").toInt(0);
    c.httpOnly = obj.value("httpOnly").toBool(false);
    c.secure = obj.value("secure").toBool(false);
    c.session = obj.value("session").toBool(false);
    c.sameSite = obj.value("sameSite").toString();
    c.priority = obj.value("priority").toString();
    c.sourceScheme = obj.value("sourceScheme").toString();
    c.sourcePort = obj.value("sourcePort").toInt(0);
    if (obj.contains("partitionKey")) {
        c.partitionKey = obj.value("partitionKey").toString();
        c.partitionKeyOpaque = obj.value("partitionKeyOpaque").toBool(false);
    }
    return c;
}

QList<StorageItem> StorageTracker::parseStorageItems(const QJsonArray& arr)
{
    QList<StorageItem> items;
    for (const QJsonValue& v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() >= 2) {
            StorageItem item;
            item.key = pair[0].toString();
            item.value = pair[1].toString();
            items.append(item);
        }
    }
    return items;
}

QList<CacheEntry> StorageTracker::parseCacheEntries(const QJsonArray& arr)
{
    QList<CacheEntry> entries;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        CacheEntry e;
        e.requestURL = o.value("requestURL").toString();
        e.requestMethod = o.value("requestMethod").toString();
        e.responseTime = o.value("responseTime").toDouble();
        e.responseStatus = o.value("responseStatus").toInt();
        e.responseStatusText = o.value("responseStatusText").toString();
        e.responseType = o.value("responseType").toString();

        const QJsonArray reqHeaders = o.value("requestHeaders").toArray();
        for (const QJsonValue& h : reqHeaders) {
            const QJsonObject ho = h.toObject();
            HttpHeader header;
            header.name = ho.value("name").toString();
            header.value = ho.value("value").toString();
            e.requestHeaders.append(header);
        }
        const QJsonArray respHeaders = o.value("responseHeaders").toArray();
        for (const QJsonValue& h : respHeaders) {
            const QJsonObject ho = h.toObject();
            HttpHeader header;
            header.name = ho.value("name").toString();
            header.value = ho.value("value").toString();
            e.responseHeaders.append(header);
        }
        entries.append(e);
    }
    return entries;
}

} // namespace devtools
} // namespace nothing
