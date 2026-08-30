'use strict';

// Mirrors piggy's existing IPC call pattern (see client.js/site.js) —
// sends a command over the named pipe to the C++ SearchEngineManager,
// does not touch the browser via any CDP-style protocol.
class SearchEngine {
  constructor(client, contextId = 'default') {
    this._client = client;       // the underlying piggy IPC client
    this._contextId = contextId;
  }

  // searchengine('google') or searchengine({ id: 'google' })
  async use(engineIdOrConfig) {
    const engineId = typeof engineIdOrConfig === 'string'
      ? engineIdOrConfig
      : engineIdOrConfig.id;

    const result = await this._client.send('search.setEngine', {
      contextId: this._contextId,
      engineId,
    });

    if (!result.ok) {
      throw new Error(`Unknown search engine: ${engineId}`);
    }
    return this;
  }

  async current() {
    return this._client.send('search.getEngine', { contextId: this._contextId });
  }

  async list() {
    return this._client.send('search.listEngines', {});
  }

  // registers a custom SearXNG-style instance, then optionally switches to it
  async registerCustom(id, displayName, baseUrl, { switchTo = true } = {}) {
    await this._client.send('search.registerCustom', { id, displayName, baseUrl });
    if (switchTo) await this.use(id);
    return this;
  }

  async query(text) {
    const engine = await this.current();
    return this._client.send('search.buildQueryUrl', {
      contextId: this._contextId,
      query: text,
    });
  }
}

function createSearchEngine(client, contextId) {
  return new SearchEngine(client, contextId);
}

module.exports = { SearchEngine, createSearchEngine };