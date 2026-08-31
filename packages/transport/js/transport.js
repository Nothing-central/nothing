'use strict';

class Transport {
  constructor(client, contextId = 'default') {
    this._client = client;
    this._contextId = contextId;
  }

  async useTor(enabled = true) {
    const result = await this._client.send('transport.setTorEnabled', {
      contextId: this._contextId,
      enabled,
    });
    if (!result.ok) throw new Error(result.error || 'Failed to toggle Tor');
    return this;
  }

  async newTorIdentity() {
    const result = await this._client.send('transport.newTorIdentity', {});
    if (!result.ok) throw new Error(result.error || 'Failed to rotate Tor circuit');
    return this;
  }

  // source: a local path "./myproxies.txt", a remote list URL
  // "https://.../proxies.txt", or a single proxy string "host:port" /
  // "socks5://user:pass@host:port"
  async useProxy(source, { rotation = 'sequential' } = {}) {
    const result = await this._client.send('transport.setProxy', {
      contextId: this._contextId,
      source,
      rotation,
    });
    if (!result.ok) throw new Error(result.error || 'Failed to load proxy source');
    return this;
  }

  async clearProxy() {
    return this._client.send('transport.clearProxy', { contextId: this._contextId });
  }

  async status() {
    return this._client.send('transport.status', { contextId: this._contextId });
  }
}

function createTransport(client, contextId) {
  return new Transport(client, contextId);
}

module.exports = { Transport, createTransport };