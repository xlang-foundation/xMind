'use strict';

class GitChangesTreeProvider {
  constructor(vscode) {
    this.vscode = vscode;
    this.changed = new vscode.EventEmitter();
    this.onDidChangeTreeData = this.changed.event;
    this.folders = [];
    this.message = 'Refresh Git Changes to load the opened workspace.';
  }

  getTreeItem(item) { return item; }

  getChildren(item) {
    if (item?.kind === 'folder') {
      if (!item.folder.changes.length) return [this.info('No changed files')];
      return item.folder.changes.map(change => this.file(item.folder, change));
    }
    if (item) return [];
    if (!this.folders.length) return [this.info(this.message)];
    return this.folders.map(folder => this.folder(folder));
  }

  clear(message = 'Connect xMind and refresh to load Git changes.') {
    this.folders = [];
    this.message = message;
    this.changed.fire(undefined);
  }

  setFolders(folders) {
    if (!Array.isArray(folders) || folders.length > 256) throw new Error('Invalid Git folder snapshot.');
    this.folders = folders.map(folder => {
      if (!folder || typeof folder.name !== 'string' || !folder.name || typeof folder.root !== 'string' ||
          !Array.isArray(folder.changes) || folder.changes.length > 4096) throw new Error('Invalid Git folder snapshot.');
      return { ...folder, changes: folder.changes.map(change => {
        if (!change || typeof change.file !== 'string' || !change.file ||
            !['added', 'deleted', 'modified'].includes(change.status) ||
            !Number.isSafeInteger(change.additions) || change.additions < 0 ||
            !Number.isSafeInteger(change.deletions) || change.deletions < 0) throw new Error('Invalid Git file snapshot.');
        return { ...change };
      }) };
    });
    this.message = '';
    this.changed.fire(undefined);
  }

  info(label) {
    const item = new this.vscode.TreeItem(label, this.vscode.TreeItemCollapsibleState.None);
    item.contextValue = 'xmind.gitInfo';
    return item;
  }

  folder(folder) {
    const item = new this.vscode.TreeItem(folder.name, this.vscode.TreeItemCollapsibleState.Expanded);
    item.kind = 'folder';
    item.folder = folder;
    item.description = folder.root;
    item.tooltip = folder.root;
    item.contextValue = 'xmind.gitFolder';
    item.iconPath = new this.vscode.ThemeIcon('repo');
    return item;
  }

  file(folder, change) {
    const icon = { added: 'diff-added', deleted: 'diff-removed', modified: 'diff-modified' }[change.status];
    const item = new this.vscode.TreeItem(change.file, this.vscode.TreeItemCollapsibleState.None);
    item.description = `+${change.additions}  −${change.deletions}`;
    item.tooltip = `${change.status} · ${folder.root} · +${change.additions} −${change.deletions}`;
    item.contextValue = 'xmind.gitFile';
    item.iconPath = new this.vscode.ThemeIcon(icon);
    return item;
  }
}

module.exports = { GitChangesTreeProvider };
