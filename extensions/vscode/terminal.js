'use strict';

const { StringDecoder } = require('node:string_decoder');

function boundedSize(dimensions) {
  return {
    rows: Math.max(5, Math.min(300, Number.isSafeInteger(dimensions?.rows) ? dimensions.rows : 24)),
    columns: Math.max(20, Math.min(500, Number.isSafeInteger(dimensions?.columns) ? dimensions.columns : 80))
  };
}

class XMindPseudoterminal {
  constructor(vscode, client, session, { title = 'xMind Terminal', current = () => true, onDispose = () => {} } = {}) {
    this.client = client;
    this.session = session;
    this.title = title;
    this.current = current;
    this.onDispose = onDispose;
    this.writeEmitter = new vscode.EventEmitter();
    this.closeEmitter = new vscode.EventEmitter();
    this.onDidWrite = this.writeEmitter.event;
    this.onDidClose = this.closeEmitter.event;
    this.decoder = new StringDecoder('utf8');
    this.sequence = 0;
    this.dimensions = { rows: 24, columns: 80 };
    this.inputQueue = Promise.resolve();
    this.inputBytes = 0;
    this.resizeQueue = Promise.resolve();
    this.closed = false;
    this.started = false;
    this.id = undefined;
    this.timer = undefined;
    this.failureDelay = 100;
  }

  open(dimensions) {
    if (this.started || this.closed) return;
    this.started = true;
    this.dimensions = boundedSize(dimensions);
    this.client.createTerminal(this.session, this.title, this.dimensions.rows, this.dimensions.columns).then(async metadata => {
      if (this.closed || !this.current()) {
        await this.client.closeTerminal(metadata.id).catch(() => {});
        this.finish();
        return;
      }
      this.id = metadata.id;
      this.sequence = metadata.output_sequence;
      this.writeEmitter.fire(`xMind workspace terminal · ${metadata.title}\r\n`);
      this.pump();
    }).catch(error => {
      if (!this.closed) {
        this.writeEmitter.fire(`\r\nxMind terminal could not start: ${safeMessage(error)}\r\n`);
        this.finish();
      }
    });
  }

  handleInput(data) {
    if (this.closed || !this.id || !this.current() || typeof data !== 'string' || !data) return;
    const bytes = Buffer.byteLength(data, 'utf8');
    if (bytes > 65536 || this.inputBytes + bytes > 1024 * 1024) {
      this.writeEmitter.fire('\r\nxMind terminal input queue is full; input was discarded.\r\n');
      return;
    }
    this.inputBytes += bytes;
    const id = this.id;
    this.inputQueue = this.inputQueue.then(async () => {
      if (!this.closed && this.id === id && this.current()) await this.client.terminalInput(id, data);
    }).catch(error => {
      if (!this.closed && this.current()) this.writeEmitter.fire(`\r\nxMind terminal input failed: ${safeMessage(error)}\r\n`);
    }).finally(() => { this.inputBytes = Math.max(0, this.inputBytes - bytes); });
  }

  setDimensions(dimensions) {
    if (this.closed) return;
    const next = boundedSize(dimensions);
    if (next.rows === this.dimensions.rows && next.columns === this.dimensions.columns) return;
    this.dimensions = next;
    if (!this.id || !this.current()) return;
    const id = this.id, size = next;
    this.resizeQueue = this.resizeQueue.then(async () => {
      if (!this.closed && this.id === id && this.current()) await this.client.resizeTerminal(id, size.rows, size.columns);
    }).catch(error => {
      if (!this.closed && this.current()) this.writeEmitter.fire(`\r\nxMind terminal resize failed: ${safeMessage(error)}\r\n`);
    });
  }

  async pump() {
    if (this.closed || !this.id) return;
    if (!this.current()) { this.close(); return; }
    const id = this.id;
    try {
      const output = await this.client.terminalOutput(id, this.sequence);
      if (this.closed || this.id !== id || !this.current()) return;
      this.sequence = output.next_sequence;
      if (output.truncated) this.writeEmitter.fire('\r\n[xMind terminal output was truncated before it could be displayed]\r\n');
      const text = this.decoder.write(Buffer.from(output.data, 'hex'));
      if (text) this.writeEmitter.fire(text);
      this.failureDelay = 100;
      if (!output.running) {
        const tail = this.decoder.end();
        if (tail) this.writeEmitter.fire(tail);
        this.finish(output.exit_code);
        return;
      }
    } catch (error) {
      if (this.closed || this.id !== id) return;
      if (!this.current()) { this.close(); return; }
      this.writeEmitter.fire(`\r\nxMind terminal observation paused: ${safeMessage(error)}\r\n`);
      this.failureDelay = Math.min(2000, this.failureDelay * 2);
    }
    this.timer = setTimeout(() => { this.timer = undefined; this.pump(); }, this.failureDelay);
  }

  close() {
    if (this.closed) return;
    this.closed = true;
    clearTimeout(this.timer);
    const id = this.id;
    this.id = undefined;
    if (id) this.client.closeTerminal(id).catch(() => {});
    this.finish();
  }

  dispose() { this.close(); }

  finish(code) {
    if (this.closed && this.finished) return;
    this.closed = true;
    this.finished = true;
    clearTimeout(this.timer);
    const tail = this.decoder.end();
    if (tail) this.writeEmitter.fire(tail);
    this.closeEmitter.fire(code === undefined ? undefined : { code });
    this.onDispose(this);
  }
}

function safeMessage(error) {
  const value = typeof error?.message === 'string' ? error.message : 'backend unavailable';
  return value.replace(/[\x00-\x1f\x7f]/g, ' ').slice(0, 240);
}

module.exports = { XMindPseudoterminal, boundedSize };
