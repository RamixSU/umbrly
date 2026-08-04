// Расширение VS Code для Umbrly: автодополнение, hover-подсказки, подсказки
// параметров и кнопка запуска файла (как "Run Python File"). Подсветка
// синтаксиса и сниппеты объявлены декларативно в package.json/syntaxes/snippets
// и отдельного кода не требуют.
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { KEYWORDS, BUILTINS } = require("./builtins-data");

// Один переиспользуемый терминал для запуска — как делает расширение Python.
let umbrlyTerminal;

function getOrCreateTerminal() {
  if (!umbrlyTerminal || umbrlyTerminal.exitStatus !== undefined) {
    umbrlyTerminal = vscode.window.createTerminal({ name: "Umbrly" });
  }
  return umbrlyTerminal;
}

function quoteArg(value) {
  return `"${String(value).replace(/"/g, '\\"')}"`;
}

// PowerShell не запускает `"путь\к.exe" "аргумент"` напрямую — кавычка в начале строки
// делает её строковым выражением, а не командой, и парсер падает на следующем токене.
// Нужен оператор вызова `&` перед путём. cmd.exe и POSIX-подобные оболочки (git-bash)
// прекрасно понимают и без `&`, поэтому добавляем его только для PowerShell/pwsh.
function detectShellKind() {
  const raw = String(vscode.env.shell || "").toLowerCase();
  if (raw.includes("powershell") || raw.includes("pwsh")) return "powershell";
  if (raw.includes("cmd.exe")) return "cmd";
  if (raw.includes("bash") || raw.includes("zsh") || /(^|[\\/])sh$/.test(raw)) return "posix";
  // Не удалось определить оболочку по vscode.env.shell — на Windows профиль по
  // умолчанию почти всегда PowerShell, поэтому это самое безопасное предположение.
  return process.platform === "win32" ? "powershell" : "posix";
}

function buildRunCommand(interpreterPath, filePath) {
  const prefix = detectShellKind() === "powershell" ? "& " : "";
  return `${prefix}${quoteArg(interpreterPath)} ${quoteArg(filePath)}`;
}

// Разрешает umbrly.interpreterPath: абсолютный путь — как есть; относительный —
// от корня открытой папки (если файл существует там); иначе оставляем как есть,
// чтобы терминал сам поискал команду в PATH (значение по умолчанию — "umbrly.exe").
function resolveInterpreterPath(fileUri) {
  const configured = vscode.workspace.getConfiguration("umbrly").get("interpreterPath", "umbrly.exe");
  if (!configured || path.isAbsolute(configured)) return configured;

  const folder = vscode.workspace.getWorkspaceFolder(fileUri);
  if (folder) {
    const candidate = path.join(folder.uri.fsPath, configured);
    if (fs.existsSync(candidate)) return candidate;
  }
  return configured;
}

async function runFile(uri) {
  const editor = vscode.window.activeTextEditor;
  const targetUri = uri || (editor && editor.document.uri);
  if (!targetUri) {
    vscode.window.showErrorMessage("Нет открытого файла Umbrly для запуска.");
    return;
  }

  const document = await vscode.workspace.openTextDocument(targetUri);
  if (document.isDirty) await document.save();

  const interpreterPath = resolveInterpreterPath(targetUri);
  const terminal = getOrCreateTerminal();
  terminal.show();
  terminal.sendText(buildRunCommand(interpreterPath, document.uri.fsPath));
}

function activate(context) {
  context.subscriptions.push(vscode.commands.registerCommand("umbrly.runFile", runFile));
  context.subscriptions.push(
    vscode.window.onDidCloseTerminal((t) => {
      if (t === umbrlyTerminal) umbrlyTerminal = undefined;
    })
  );

  const all = [
    ...KEYWORDS.map((k) => ({ ...k, kind: "keyword" })),
    ...BUILTINS.map((b) => ({ ...b, kind: "function" })),
  ];
  const byName = new Map(all.map((item) => [item.name, item]));
  const wordPattern = /[A-Za-z_][A-Za-z0-9_]*/;

  // ---------- Автодополнение ----------
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider("umbrly", {
      provideCompletionItems() {
        return all.map((item) => {
          const kind =
            item.kind === "keyword"
              ? vscode.CompletionItemKind.Keyword
              : vscode.CompletionItemKind.Function;
          const ci = new vscode.CompletionItem(item.name, kind);
          ci.detail = item.category ? `${item.signature}  —  ${item.category}` : item.signature;
          ci.documentation = new vscode.MarkdownString(item.doc);
          return ci;
        });
      },
    })
  );

  // ---------- Hover ----------
  context.subscriptions.push(
    vscode.languages.registerHoverProvider("umbrly", {
      provideHover(document, position) {
        const range = document.getWordRangeAtPosition(position, wordPattern);
        if (!range) return;
        const item = byName.get(document.getText(range));
        if (!item) return;
        const md = new vscode.MarkdownString();
        md.appendCodeblock(item.signature, "umbrly");
        md.appendMarkdown(item.doc);
        if (item.category) md.appendMarkdown(`\n\n*${item.category}*`);
        return new vscode.Hover(md, range);
      },
    })
  );

  // ---------- Подсказки параметров (signature help) ----------
  context.subscriptions.push(
    vscode.languages.registerSignatureHelpProvider(
      "umbrly",
      {
        provideSignatureHelp(document, position) {
          const textBefore = document.lineAt(position.line).text.slice(0, position.character);
          const match = /([A-Za-z_][A-Za-z0-9_]*)\(([^()]*)$/.exec(textBefore);
          if (!match) return;
          const item = byName.get(match[1]);
          if (!item || !item.signature.includes("(")) return;

          const paramsText = item.signature.slice(
            item.signature.indexOf("(") + 1,
            item.signature.lastIndexOf(")")
          );
          const params = paramsText.trim().length
            ? paramsText.split(",").map((p) => p.trim())
            : [];

          const info = new vscode.SignatureInformation(item.signature, new vscode.MarkdownString(item.doc));
          info.parameters = params.map((p) => new vscode.ParameterInformation(p));

          const help = new vscode.SignatureHelp();
          help.signatures = [info];
          help.activeSignature = 0;
          const commaCount = (match[2].match(/,/g) || []).length;
          help.activeParameter = params.length ? Math.min(commaCount, params.length - 1) : 0;
          return help;
        },
      },
      "(",
      ","
    )
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
