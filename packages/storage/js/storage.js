/**
 * nothing/storage — Storage API for Nothing Browser
 *
 * Two storage modes:
 * - "persistent" (default): Data is written to JSON files on disk. Survives restart.
 *   Used by Sabre Browser for normal browsing.
 *
 * - "volatile": Data lives in memory only. Cleared when the incognito/private
 *   session ends. Used by Nothing Browser and Nothing Private Browser.
 *
 * The JS API selects the mode per-call via the `storage` option:
 *   storage.set("tabs", "tab-1", data, { storage: "volatile" })
 *   storage.set("settings", "homepage", "https://...")  // defaults to persistent
 *
 * Incognito contexts default to volatile. Normal contexts default to persistent.
 *
 * Usage:
 *   import { storage } from "nothing-browser";
 *   await storage.set("settings", "homepage", "https://example.com");
 *   const homepage = await storage.get("settings", "homepage");
 */

// ============================================================================
// Types
// ============================================================================

export type StorageMode = "persistent" | "volatile";

export interface StorageOptions {
  /** "persistent" (default) or "volatile" */
  storage?: StorageMode;
  /** Context ID — "default" for normal, UUID for incognito. Auto-selected if omitted. */
  contextId?: string;
}

export interface StorageEntry {
  key: string;
  value: any;
  storage: StorageMode;
  namespace: string;
  contextId: string;
  sizeBytes: number;
}

export interface StorageStats {
  entryCount: number;
  totalBytes: number;
  persistentEntries: number;
  volatileEntries: number;
  persistentBytes: number;
  volatileBytes: number;
}

// ============================================================================
// Storage API
// ============================================================================

export class Storage {
  private currentContextId: string = "default";

  /** Set the active context (called by piggy when entering/leaving incognito) */
  setContext(contextId: string): void {
    this.currentContextId = contextId;
  }

  /** Get the active context ID */
  getContext(): string {
    return this.currentContextId;
  }

  /**
   * Store a value.
   * @param namespace_  Logical group: "tabs", "settings", "downloads", etc.
   * @param key         The key within the namespace
   * @param value       The value (any JSON-serializable type)
   * @param options     Storage options (mode, contextId)
   */
  async set(
    namespace_: string,
    key: string,
    value: any,
    options?: StorageOptions
  ): Promise<boolean> {
    return piggy.call("storage.set", {
      namespace: namespace_,
      key,
      value,
      storage: options?.storage ?? "persistent",
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /**
   * Retrieve a value.
   * Tries the specified mode first, then falls back to the other.
   */
  async get(
    namespace_: string,
    key: string,
    options?: StorageOptions
  ): Promise<any> {
    const result = await piggy.call("storage.get", {
      namespace: namespace_,
      key,
      storage: options?.storage ?? "",
      contextId: options?.contextId ?? this.currentContextId,
    });
    return result;
  }

  /** Check if a key exists. */
  async has(
    namespace_: string,
    key: string,
    options?: StorageOptions
  ): Promise<boolean> {
    return piggy.call("storage.has", {
      namespace: namespace_,
      key,
      storage: options?.storage ?? "",
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /** Remove a key. Returns true if the key existed. */
  async remove(
    namespace_: string,
    key: string,
    options?: StorageOptions
  ): Promise<boolean> {
    return piggy.call("storage.remove", {
      namespace: namespace_,
      key,
      storage: options?.storage ?? "",
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /** List all entries matching the filter. */
  async list(
    namespace_?: string,
    keyPrefix?: string,
    options?: StorageOptions
  ): Promise<StorageEntry[]> {
    return piggy.call("storage.list", {
      namespace: namespace_ ?? "",
      keyPrefix: keyPrefix ?? "",
      storage: options?.storage ?? "",
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /**
   * Clear entries for a context.
   * @param namespace_  Empty = clear ALL namespaces
   * @param options     mode: "persistent" | "volatile" | "" (both)
   */
  async clear(
    namespace_?: string,
    options?: StorageOptions
  ): Promise<number> {
    return piggy.call("storage.clear", {
      namespace: namespace_ ?? "",
      storage: options?.storage ?? "",
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /** Get storage statistics for the current context. */
  async stats(options?: { contextId?: string }): Promise<StorageStats> {
    return piggy.call("storage.stats", {
      contextId: options?.contextId ?? this.currentContextId,
    });
  }

  /** Flush persistent storage to disk. */
  async flush(): Promise<void> {
    return piggy.call("storage.flush");
  }

  /**
   * Create a new incognito storage context.
   * Returns a contextId that defaults to volatile storage.
   * Call destroyContext() when the incognito session ends.
   */
  async createContext(
    contextId?: string,
    defaultMode?: StorageMode
  ): Promise<string> {
    const id = await piggy.call("storage.createContext", {
      contextId: contextId ?? "",
      defaultMode: defaultMode ?? "volatile",
    });
    return id;
  }

  /**
   * Destroy a storage context.
   * Clears all volatile data. For non-default contexts, also clears persistent.
   */
  async destroyContext(contextId: string): Promise<number> {
    return piggy.call("storage.destroyContext", { contextId });
  }

  /** List all active contexts. */
  async contexts(): Promise<string[]> {
    return piggy.call("storage.contexts");
  }

  // === Convenience: volatile shortcut ===

  /**
   * Store a value in volatile storage (convenience method).
   * Equivalent to set(ns, key, value, { storage: "volatile" }).
   */
  async setVolatile(namespace_: string, key: string, value: any): Promise<boolean> {
    return this.set(namespace_, key, value, { storage: "volatile" });
  }

  /**
   * Get a value from volatile storage.
   * Falls back to persistent if not found in volatile.
   */
  async getVolatile(namespace_: string, key: string): Promise<any> {
    return this.get(namespace_, key, { storage: "volatile" });
  }
}

// ============================================================================
// Singleton export
// ============================================================================

export const storage = new Storage();
