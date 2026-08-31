/**
 * nothing/devtools — DevTools API for Nothing Browser
 *
 * This module provides a JavaScript/TypeScript API for the DevTools
 * functionality exposed by the C++ packages/devtools module.
 *
 * It communicates with the C++ side via piggy's named-pipe IPC,
 * same as all other Nothing Browser packages.
 *
 * Usage:
 *   import { devtools } from "nothing-browser";
 *
 *   await devtools.enable();
 *   const requests = await devtools.network.getAllRequests();
 *   const title = await devtools.runtime.evaluate("document.title");
 *   const screenshot = await devtools.page.screenshot();
 */

// ============================================================================
// Types
// ============================================================================

export interface DevToolsConfig {
  /** Remote debugging port (default: 9222) */
  port?: number;
  /** Auto-enable all domain trackers on connect */
  autoEnable?: boolean;
  /** Auto-attach to new targets (pages, workers) */
  autoAttach?: boolean;
}

export interface NetworkRequest {
  requestId: string;
  url: string;
  method: string;
  status: number;
  statusText: string;
  mimeType: string;
  protocol: string;
  resourceType: string;
  headers: Record<string, string>;
  postData?: string;
  remoteIPAddress: string;
  remotePort: number;
  fromDiskCache: boolean;
  fromServiceWorker: boolean;
  encodedDataLength: number;
  dataLength: number;
  finished: boolean;
  failed: boolean;
  errorText: string;
  isWebSocket: boolean;
  isRedirect: boolean;
  timestamp: number;
  wallTime: number;
  initiator: { type: string; url: string; lineNumber: number };
}

export interface WebSocketFrame {
  direction: "sent" | "received";
  opcode: number;
  masked: boolean;
  payloadData: string;
  timestamp: number;
}

export interface ConsoleMessage {
  type: string;
  args: any[];
  executionContextId: number;
  timestamp: number;
  stackTrace?: { callFrames: any[] };
}

export interface EvaluateResult {
  value: any;
  type: string;
  subtype?: string;
  description?: string;
  objectId?: string;
  exceptionDetails?: {
    text: string;
    lineNumber: number;
    columnNumber: number;
    url: string;
    stackTrace?: any;
  };
}

export interface CookieInfo {
  name: string;
  value: string;
  domain: string;
  path: string;
  expires: number;
  secure: boolean;
  httpOnly: boolean;
  sameSite: string;
  session: boolean;
}

export interface ScreenshotOptions {
  format?: "png" | "jpeg" | "webp";
  quality?: number;
  clip?: { x: number; y: number; width: number; height: number };
  captureBeyondViewport?: boolean;
}

export interface DomNode {
  nodeId: number;
  nodeType: number;
  nodeName: string;
  localName: string;
  nodeValue: string;
  attributes: string[];
  children: DomNode[];
  childNodeCount: number;
  frameId?: string;
  shadowRootType?: string;
}

// ============================================================================
// DevTools API
// ============================================================================

export class DevTools {
  private config: DevToolsConfig;
  private connected: boolean = false;

  constructor(config: DevToolsConfig = {}) {
    this.config = {
      port: 9222,
      autoEnable: true,
      autoAttach: true,
      ...config,
    };
  }

  /** Enable DevTools — connects to the C++ DevToolsServer via IPC */
  async enable(): Promise<void> {
    // IPC call: devtools.enable { port, autoEnable, autoAttach }
    await piggy.call("devtools.enable", {
      port: this.config.port,
      autoEnable: this.config.autoEnable,
      autoAttach: this.config.autoAttach,
    });
    this.connected = true;
  }

  /** Disable DevTools */
  async disable(): Promise<void> {
    await piggy.call("devtools.disable");
    this.connected = false;
  }

  /** Check if connected */
  isEnabled(): boolean {
    return this.connected;
  }

  // === Network API ===

  network = {
    /** Get all captured network requests */
    getAllRequests: async (): Promise<NetworkRequest[]> => {
      return piggy.call("devtools.network.getAllRequests");
    },

    /** Get a specific request by ID */
    getRequest: async (requestId: string): Promise<NetworkRequest | null> => {
      return piggy.call("devtools.network.getRequest", { requestId });
    },

    /** Get the response body for a request */
    getResponseBody: async (requestId: string): Promise<{ body: string; base64Encoded: boolean }> => {
      return piggy.call("devtools.network.getResponseBody", { requestId });
    },

    /** Get the POST body for a request */
    getRequestPostData: async (requestId: string): Promise<{ body: string; base64Encoded: boolean }> => {
      return piggy.call("devtools.network.getRequestPostData", { requestId });
    },

    /** Set extra HTTP headers for all requests */
    setExtraHeaders: async (headers: Record<string, string>): Promise<void> => {
      return piggy.call("devtools.network.setExtraHeaders", { headers });
    },

    /** Block URLs matching patterns */
    blockUrls: async (patterns: string[]): Promise<void> => {
      return piggy.call("devtools.network.blockUrls", { patterns });
    },

    /** Unblock a URL pattern */
    unblockUrl: async (pattern: string): Promise<void> => {
      return piggy.call("devtools.network.unblockUrl", { pattern });
    },

    /** Emulate network conditions */
    emulateConditions: async (opts: {
      offline?: boolean;
      latencyMs?: number;
      downloadThroughput?: number;
      uploadThroughput?: number;
    }): Promise<void> => {
      return piggy.call("devtools.network.emulateConditions", opts);
    },

    /** Disable HTTP cache */
    setCacheDisabled: async (disabled: boolean): Promise<void> => {
      return piggy.call("devtools.network.setCacheDisabled", { disabled });
    },

    /** Subscribe to network events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.network.${event}`, callback);
    },

    /** Unsubscribe from network events */
    off: (event: string, callback: (data: any) => void): void => {
      piggy.off(`devtools.network.${event}`, callback);
    },
  };

  // === Runtime API ===

  runtime = {
    /** Evaluate a JavaScript expression */
    evaluate: async (
      expression: string,
      options?: {
        returnByValue?: boolean;
        awaitPromise?: boolean;
        executionContextId?: number;
        includeCommandLineAPI?: boolean;
        timeout?: number;
      }
    ): Promise<EvaluateResult> => {
      return piggy.call("devtools.runtime.evaluate", {
        expression,
        ...options,
      });
    },

    /** Add a binding (JS function that calls back to DevTools) */
    addBinding: async (name: string): Promise<void> => {
      return piggy.call("devtools.runtime.addBinding", { name });
    },

    /** Inject a script that runs before page scripts on every navigation */
    injectScript: async (
      source: string,
      options?: { worldName?: string; grantUniversalAccess?: boolean }
    ): Promise<string> => {
      return piggy.call("devtools.runtime.injectScript", { source, ...options });
    },

    /** Create an isolated world */
    createIsolatedWorld: async (
      frameId: string,
      worldName: string,
      grantUniversalAccess?: boolean
    ): Promise<number> => {
      return piggy.call("devtools.runtime.createIsolatedWorld", {
        frameId,
        worldName,
        grantUniversalAccess,
      });
    },

    /** Subscribe to runtime events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.runtime.${event}`, callback);
    },
  };

  // === DOM API ===

  dom = {
    /** Get the document tree */
    getDocument: async (depth?: number, pierce?: boolean): Promise<DomNode> => {
      return piggy.call("devtools.dom.getDocument", { depth: depth ?? -1, pierce: pierce ?? true });
    },

    /** Query a selector */
    querySelector: async (selector: string, nodeId?: number): Promise<number> => {
      return piggy.call("devtools.dom.querySelector", { selector, nodeId: nodeId ?? 1 });
    },

    /** Query all matches */
    querySelectorAll: async (selector: string, nodeId?: number): Promise<number[]> => {
      return piggy.call("devtools.dom.querySelectorAll", { selector, nodeId: nodeId ?? 1 });
    },

    /** Get outer HTML of a node */
    getOuterHTML: async (nodeId: number): Promise<string> => {
      return piggy.call("devtools.dom.getOuterHTML", { nodeId });
    },

    /** Set an attribute */
    setAttribute: async (nodeId: number, name: string, value: string): Promise<void> => {
      return piggy.call("devtools.dom.setAttribute", { nodeId, name, value });
    },

    /** Remove a node */
    removeNode: async (nodeId: number): Promise<void> => {
      return piggy.call("devtools.dom.removeNode", { nodeId });
    },

    /** Focus an element */
    focus: async (nodeId: number): Promise<void> => {
      return piggy.call("devtools.dom.focus", { nodeId });
    },

    /** Get computed style */
    getComputedStyle: async (nodeId: number): Promise<Record<string, string>> => {
      return piggy.call("devtools.dom.getComputedStyle", { nodeId });
    },

    /** Get box model */
    getBoxModel: async (nodeId: number): Promise<any> => {
      return piggy.call("devtools.dom.getBoxModel", { nodeId });
    },

    /** Upload files to an <input type="file"> */
    setFileInputFiles: async (nodeId: number, files: string[]): Promise<void> => {
      return piggy.call("devtools.dom.setFileInputFiles", { nodeId, files });
    },

    /** Subscribe to DOM events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.dom.${event}`, callback);
    },
  };

  // === Page API ===

  page = {
    /** Navigate to a URL */
    navigate: async (url: string, referrer?: string): Promise<{ frameId: string; loaderId: string }> => {
      return piggy.call("devtools.page.navigate", { url, referrer });
    },

    /** Reload the page */
    reload: async (ignoreCache?: boolean): Promise<void> => {
      return piggy.call("devtools.page.reload", { ignoreCache });
    },

    /** Capture a screenshot */
    screenshot: async (options?: ScreenshotOptions): Promise<string> => {
      // Returns base64-encoded image data
      return piggy.call("devtools.page.screenshot", options ?? {});
    },

    /** Capture a full-page screenshot */
    screenshotFullPage: async (format?: string): Promise<string> => {
      return piggy.call("devtools.page.screenshotFullPage", { format: format ?? "png" });
    },

    /** Generate a PDF */
    printToPdf: async (options?: any): Promise<string> => {
      return piggy.call("devtools.page.printToPdf", options ?? {});
    },

    /** Find loaded files by name */
    findFiles: async (filename: string): Promise<Array<{ url: string; type: string; size: number; frameId: string }>> => {
      return piggy.call("devtools.page.findFiles", { filename });
    },

    /** Download a resource to disk */
    downloadResource: async (frameId: string, url: string, filepath: string): Promise<boolean> => {
      return piggy.call("devtools.page.downloadResource", { frameId, url, filepath });
    },

    /** Wait for page load */
    waitForLoad: async (timeoutMs?: number): Promise<void> => {
      return piggy.call("devtools.page.waitForLoad", { timeoutMs: timeoutMs ?? 30000 });
    },

    /** Wait for network idle */
    waitForNetworkIdle: async (timeoutMs?: number): Promise<void> => {
      return piggy.call("devtools.page.waitForNetworkIdle", { timeoutMs: timeoutMs ?? 30000 });
    },

    /** Get current URL */
    getCurrentUrl: async (): Promise<string> => {
      return piggy.call("devtools.page.getCurrentUrl");
    },

    /** Handle a JavaScript dialog */
    handleDialog: async (accept: boolean, promptText?: string): Promise<void> => {
      return piggy.call("devtools.page.handleDialog", { accept, promptText });
    },

    /** Subscribe to page events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.page.${event}`, callback);
    },
  };

  // === Storage API ===

  storage = {
    /** Get all cookies */
    getAllCookies: async (): Promise<CookieInfo[]> => {
      return piggy.call("devtools.storage.getAllCookies");
    },

    /** Get cookies for URLs */
    getCookiesForUrls: async (urls: string[]): Promise<CookieInfo[]> => {
      return piggy.call("devtools.storage.getCookiesForUrls", { urls });
    },

    /** Set a cookie */
    setCookie: async (cookie: Partial<CookieInfo> & { name: string; value: string; domain: string }): Promise<boolean> => {
      return piggy.call("devtools.storage.setCookie", { cookie });
    },

    /** Delete a cookie */
    deleteCookie: async (name: string, domain: string, path?: string): Promise<void> => {
      return piggy.call("devtools.storage.deleteCookie", { name, domain, path });
    },

    /** Delete all cookies for a domain */
    deleteCookiesForDomain: async (domain: string): Promise<number> => {
      return piggy.call("devtools.storage.deleteCookiesForDomain", { domain });
    },

    /** Clear all cookies */
    clearAllCookies: async (): Promise<void> => {
      return piggy.call("devtools.storage.clearAllCookies");
    },

    /** Export cookies to JSON */
    exportCookies: async (): Promise<string> => {
      return piggy.call("devtools.storage.exportCookies");
    },

    /** Import cookies from JSON */
    importCookies: async (json: string): Promise<number> => {
      return piggy.call("devtools.storage.importCookies", { json });
    },

    /** Get localStorage items */
    getLocalStorage: async (origin: string): Promise<Array<{ key: string; value: string }>> => {
      return piggy.call("devtools.storage.getLocalStorage", { origin });
    },

    /** Get sessionStorage items */
    getSessionStorage: async (origin: string): Promise<Array<{ key: string; value: string }>> => {
      return piggy.call("devtools.storage.getSessionStorage", { origin });
    },

    /** Set a localStorage item */
    setLocalStorageItem: async (origin: string, key: string, value: string): Promise<void> => {
      return piggy.call("devtools.storage.setLocalStorageItem", { origin, key, value });
    },

    /** List IndexedDB databases */
    listDatabases: async (origin: string): Promise<string[]> => {
      return piggy.call("devtools.storage.listDatabases", { origin });
    },

    /** Read IndexedDB data */
    readObjectStore: async (
      origin: string,
      database: string,
      objectStore: string,
      skipCount?: number,
      pageSize?: number
    ): Promise<{ entries: any[]; hasMore: boolean }> => {
      return piggy.call("devtools.storage.readObjectStore", {
        origin, database, objectStore,
        skipCount: skipCount ?? 0,
        pageSize: pageSize ?? 100,
      });
    },

    /** List CacheStorage caches */
    listCaches: async (origin: string): Promise<any[]> => {
      return piggy.call("devtools.storage.listCaches", { origin });
    },

    /** List CacheStorage entries */
    listCacheEntries: async (cacheId: string, skipCount?: number, pageSize?: number): Promise<any[]> => {
      return piggy.call("devtools.storage.listCacheEntries", {
        cacheId,
        skipCount: skipCount ?? 0,
        pageSize: pageSize ?? 100,
      });
    },

    /** Clear all storage for an origin */
    clearDataForOrigin: async (origin: string, types?: string[]): Promise<void> => {
      return piggy.call("devtools.storage.clearDataForOrigin", { origin, types });
    },

    /** Subscribe to storage events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.storage.${event}`, callback);
    },
  };

  // === Target API ===

  targets = {
    /** List all targets */
    list: async (): Promise<any[]> => {
      return piggy.call("devtools.targets.list");
    },

    /** Create a new tab */
    create: async (url: string, options?: { newWindow?: boolean; forTab?: boolean }): Promise<string> => {
      return piggy.call("devtools.targets.create", { url, ...options });
    },

    /** Close a target */
    close: async (targetId: string): Promise<void> => {
      return piggy.call("devtools.targets.close", { targetId });
    },

    /** Activate (focus) a target */
    activate: async (targetId: string): Promise<void> => {
      return piggy.call("devtools.targets.activate", { targetId });
    },

    /** Create an isolated browser context */
    createContext: async (): Promise<string> => {
      return piggy.call("devtools.targets.createContext");
    },

    /** Subscribe to target events */
    on: (event: string, callback: (data: any) => void): void => {
      piggy.on(`devtools.targets.${event}`, callback);
    },
  };

  // === Emulation API ===

  emulation = {
    /** Override the user agent */
    setUserAgent: async (userAgent: string, options?: {
      acceptLanguage?: string;
      platform?: string;
      userAgentMetadata?: any;
    }): Promise<void> => {
      return piggy.call("devtools.emulation.setUserAgent", { userAgent, ...options });
    },

    /** Set device metrics (viewport emulation) */
    setDeviceMetrics: async (opts: {
      width: number;
      height: number;
      deviceScaleFactor: number;
      mobile: boolean;
    }): Promise<void> => {
      return piggy.call("devtools.emulation.setDeviceMetrics", opts);
    },

    /** Override locale */
    setLocale: async (locale: string): Promise<void> => {
      return piggy.call("devtools.emulation.setLocale", { locale });
    },

    /** Override timezone */
    setTimezone: async (timezone: string): Promise<void> => {
      return piggy.call("devtools.emulation.setTimezone", { timezone });
    },

    /** Override geolocation */
    setGeolocation: async (lat: number, lon: number, accuracy: number): Promise<void> => {
      return piggy.call("devtools.emulation.setGeolocation", { lat, lon, accuracy });
    },

    /** Hide automation signals (navigator.webdriver) */
    hideAutomation: async (): Promise<void> => {
      return piggy.call("devtools.emulation.hideAutomation");
    },

    /** Throttle CPU */
    throttleCpu: async (rate: number): Promise<void> => {
      return piggy.call("devtools.emulation.throttleCpu", { rate });
    },

    /** Ignore certificate errors */
    ignoreCertErrors: async (ignore: boolean): Promise<void> => {
      return piggy.call("devtools.emulation.ignoreCertErrors", { ignore });
    },
  };
}

// ============================================================================
// Singleton export
// ============================================================================

export const devtools = new DevTools();
