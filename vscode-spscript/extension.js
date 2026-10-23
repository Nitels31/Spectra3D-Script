// Spectra3D Script - VS Code extension
//
// Adds .sps language support: the syntax highlighting, the diagnostics of the
// compiler (the Problems panel) and the run / compile commands.

const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { execFile } = require('child_process');

const TERMINAL_NAME = 'SPScript';

// The problems of the scripts.
const diagnostics = vscode.languages.createDiagnosticCollection('spscript');

// The words of the language for the completion.
const KEYWORDS = [
    'let', 'var', 'def', 'class', 'struct', 'if', 'elif', 'else', 'while', 'for',
    'in', 'break', 'continue', 'return', 'try', 'catch', 'import', 'as',
    'true', 'false', 'nil', 'and', 'or', 'not'
];

const TYPES = ['int', 'float', 'double', 'str', 'bool', 'list', 'dict'];

const BUILTINS = [
    'print', 'len', 'str', 'int', 'float', 'double', 'bool', 'type', 'input',
    'range', 'abs', 'min', 'max', 'sum', 'ord', 'chr',
    'read_file', 'write_file', 'append_file', 'file_exists', 'delete_file',
    'file_size', 'list_dir', 'make_dir',
    'time', 'now', 'sleep',
    'json_parse', 'json_stringify', 'json_pretty',
    'spl_parse', 'spl_stringify', 'spl_pretty', 'spl_load', 'spl_save',
    'sqrt', 'sin', 'cos', 'tan', 'atan2', 'pow', 'floor', 'ceil', 'round',
    'random', 'random_range',
    'map', 'filter', 'reduce',
    // the api of the engine
    'downKey', 'justPressed', 'justReleased', 'mouseX', 'mouseY', 'mouseDown',
    'gamepadDown', 'gamepadAxis', 'deltaTime', 'frame', 'wait',
    'playSound', 'stopSound', 'playMusic', 'stopMusic', 'soundPlaying', 'setVolume',
    'loadModel', 'loadTexture', 'loadMaterial', 'loadScene', 'loadFont',
    'vec3', 'color', 'vec3.add', 'vec3.sub', 'vec3.mul', 'vec3.dot', 'vec3.cross',
    'vec3.length', 'vec3.normalize', 'vec3.lerp', 'distance', 'clamp',
    'showMessage', 'log', 'quit',
    // the events, the timers and the physics
    'on', 'off', 'emit', 'after', 'every', 'cancelTimer',
    'raycast', 'applyImpulse', 'setGravity', 'getGravity',
    // the entities, the screen and the system
    'createEntity', 'clone', 'setParent', 'findByTag', 'findAllByTag',
    'screenToWorld', 'worldToScreen', 'setCursorVisible',
    'playAnimation', 'stopAnimation', 'emitParticles', 'setTimeScale',
    'pauseGame', 'resumeGame', 'getFPS', 'frame', 'platform',
    'logWarning', 'logError', 'callFunction',
    // the utilities
    'lerp', 'moveTowards', 'wrap', 'sign', 'repeat', 'shuffle', 'randomPoint',
    // the result
    'resultOk', 'resultError', 'isOk', 'isError', 'unwrap', 'unwrapOr',
    // the math of the quaternions and the matrices
    'quatFromEuler', 'quatMultiply', 'quatRotate', 'quatSlerp',
    'mat4Translation', 'mat4Scaling', 'mat4Rotation', 'mat4Perspective',
    'mat4Multiply', 'mat4Transform'
];

// the classes of the engine
const ENGINE_CLASSES = [
    'Entity', 'Scene', 'Camera', 'Light', 'Rigidbody', 'Collider', 'Trigger',
    'AudioSource', 'Label', 'Button', 'Vector3', 'Color', 'Shader', 'Material',
    'Panel', 'Image', 'Quaternion', 'Matrix4'
];

function isWindows() {
    return process.platform === 'win32';
}

function config() {
    return vscode.workspace.getConfiguration('spscript');
}

// Finds the spsc compiler: the configured path first, then bin/spsc(.exe)
// inside the workspace folders, then "spsc" from PATH.
function resolveCompiler() {
    const configured = (config().get('compilerPath') || '').trim();
    if (configured) {
        return configured;
    }

    const folders = vscode.workspace.workspaceFolders || [];
    for (const folder of folders) {
        const root = folder.uri.fsPath;
        const candidates = isWindows()
            ? [path.join(root, 'bin', 'spsc.exe'), path.join(root, 'spsc.exe')]
            : [path.join(root, 'bin', 'spsc'), path.join(root, 'spsc')];
        for (const candidate of candidates) {
            if (fs.existsSync(candidate)) {
                return candidate;
            }
        }
    }
    return isWindows() ? 'spsc.exe' : 'spsc';
}

function quote(value) {
    if (isWindows()) {
        return '"' + value + '"';
    }
    return "'" + value.replace(/'/g, "'\\''") + "'";
}

function workspaceRoot() {
    const folders = vscode.workspace.workspaceFolders;
    return folders && folders.length ? folders[0].uri.fsPath : undefined;
}

function getTerminal(cwd) {
    const existing = vscode.window.terminals.find((t) => t.name === TERMINAL_NAME);
    if (existing) {
        existing.show(true);
        return existing;
    }
    return vscode.window.createTerminal({ name: TERMINAL_NAME, cwd });
}

// Detects the shell of VS Code.
function shellKind() {
    const shell = ((vscode.env && vscode.env.shell) || '').toLowerCase();
    if (shell.indexOf('pwsh') !== -1 || shell.indexOf('powershell') !== -1) {
        return 'powershell';
    }
    if (shell.indexOf('cmd') !== -1) {
        return 'cmd';
    }
    return isWindows() ? 'powershell' : 'posix';
}

// Builds the command of the shell. PowerShell needs the call operator for a
// quoted path.
function buildCommand(compiler, args) {
    const parts = [quote(compiler)].concat(args.map(quote)).join(' ');
    return shellKind() === 'powershell' ? '& ' + parts : parts;
}

// The command that prints the exit code of the previous command.
function exitCodeCommand() {
    switch (shellKind()) {
        case 'cmd':
            return 'echo exit code: %errorlevel%';
        case 'powershell':
            return 'echo "exit code: $LASTEXITCODE"';
        default:
            return 'echo "exit code: $?"';
    }
}

// Runs the compiler and returns its output.
function runCompiler(compiler, args) {
    return new Promise((resolve) => {
        execFile(compiler, args, { cwd: workspaceRoot() }, (error, stdout, stderr) => {
            const output = (stdout || '') + (stderr || '');
            resolve({ code: error ? (error.code || 1) : 0, output });
        });
    });
}

// Reads the diagnostics of the compiler: "file:line:column: error: message"
function parseDiagnostics(output) {
    const found = [];
    const pattern = /^(.*?):(\d+):(\d+):\s*(error|warning):\s*(.*)$/;
    for (const line of output.split(/\r?\n/)) {
        const match = pattern.exec(line.trim());
        if (!match) continue;
        const row = Math.max(0, Number(match[2]) - 1);
        const column = Math.max(0, Number(match[3]) - 1);
        const severity = match[4] === 'warning'
            ? vscode.DiagnosticSeverity.Warning
            : vscode.DiagnosticSeverity.Error;
        const range = new vscode.Range(row, column, row, column + 1);
        found.push(new vscode.Diagnostic(range, match[5], severity));
    }
    return found;
}

// Checks the file and shows its problems in the Problems panel.
async function checkDocument(document) {
    if (!document || document.languageId !== 'spscript' && !document.fileName.endsWith('.sps')) {
        return;
    }

    const compiler = resolveCompiler();
    const result = await runCompiler(compiler, ['--check', document.uri.fsPath]);
    const found = parseDiagnostics(result.output);

    if (found.length) {
        diagnostics.set(document.uri, found);
    } else {
        diagnostics.delete(document.uri);
    }
}

async function checkFile(editor) {
    if (!editor) return;
    await checkDocument(editor.document);
    const found = diagnostics.get(editor.document.uri) || [];
    if (found.length) {
        const first = found[0];
        const message = `SPScript: ${found.length} problem(s), the first one is on line ${first.range.start.line + 1}`;
        vscode.window.showWarningMessage(message);
    }
}

async function saveIfNeeded(document) {
    if (document.isUntitled) {
        vscode.window.showErrorMessage('SPScript: save the file before running it');
        return false;
    }
    if (!document.isDirty) {
        return true;
    }
    return await document.save();
}

async function runFile(editor, extraArgs) {
    if (!editor) return;
    const document = editor.document;
    if (!(await saveIfNeeded(document))) return;

    await checkDocument(document);

    const file = document.uri.fsPath;
    const compiler = resolveCompiler();
    const root = workspaceRoot();
    const cwd = root || path.dirname(file);
    const extra = (config().get('runArguments') || '').trim();

    const args = [file].concat(extra ? extra.split(/\s+/) : []).concat(extraArgs || []);
    const terminal = getTerminal(cwd);
    terminal.show(true);
    terminal.sendText(buildCommand(compiler, args), true);
    terminal.sendText(exitCodeCommand(), true);
}

async function compileFile(editor) {
    if (!editor) return;
    const document = editor.document;
    if (!(await saveIfNeeded(document))) return;

    await checkDocument(document);

    const file = document.uri.fsPath;
    const compiler = resolveCompiler();
    const root = workspaceRoot();
    const cwd = root || path.dirname(file);

    const directory = (config().get('outputDirectory') || 'out').trim();
    const target = path.join(directory, path.basename(file, path.extname(file)) + '.spc');
    const output = root ? path.join(root, target) : target;

    const terminal = getTerminal(cwd);
    terminal.show(true);
    terminal.sendText(buildCommand(compiler, [file, '-o', output]), true);
    terminal.sendText(exitCodeCommand(), true);
}

async function runWithArgs(editor) {
    if (!editor) return;
    const args = await vscode.window.showInputBox({
        title: 'SPScript: arguments',
        prompt: 'Command line arguments passed to the script',
        placeHolder: '--run'
    });
    if (args === undefined) return;
    await runFile(editor, args.trim() ? args.trim().split(/\s+/) : []);
}

function activate(context) {
    context.subscriptions.push(
        diagnostics,
        vscode.commands.registerCommand('spscript.runFile', () => runFile(vscode.window.activeTextEditor, [])),
        vscode.commands.registerCommand('spscript.runWithArgs', () => runWithArgs(vscode.window.activeTextEditor)),
        vscode.commands.registerCommand('spscript.compileFile', () => compileFile(vscode.window.activeTextEditor)),
        vscode.commands.registerCommand('spscript.checkFile', () => checkFile(vscode.window.activeTextEditor)),
        vscode.languages.registerCompletionItemProvider('spscript', {
            provideCompletionItems() {
                const items = [];
                for (const word of KEYWORDS) {
                    items.push(new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
                }
                for (const word of TYPES) {
                    items.push(new vscode.CompletionItem(word, vscode.CompletionItemKind.Struct));
                }
                for (const word of BUILTINS) {
                    items.push(new vscode.CompletionItem(word, vscode.CompletionItemKind.Function));
                }
                for (const word of ENGINE_CLASSES) {
                    items.push(new vscode.CompletionItem(word, vscode.CompletionItemKind.Class));
                }
                return items;
            }
        }),
        vscode.workspace.onDidSaveTextDocument((document) => {
            if (document.fileName.endsWith('.sps')) {
                checkDocument(document);
            }
        })
    );
}

function deactivate() {
    diagnostics.clear();
}

module.exports = { activate, deactivate };
