#!/usr/bin/env node
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

import { createServer } from 'http';
import { readFile } from 'fs';
import { join } from 'path';
console.log(`dont forget to run: "npm --global install @jdmichaud/dwarf-2-sourcemap" for better debugging!`);
const server = createServer((req, res) => {
    console.log(`get ${req.url}`);
    if (req.url === '/') {
        // https://developer.mozilla.org/en-US/docs/WebAssembly/Guides/Loading_and_running
        // If your browser doesn't support fetch... HAHA GET FUCKED
        res.writeHead(200, {
            'Content-Type': 'text/html',
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp'
        });
        res.end(`<!DOCTYPE html>
<html>
<head>
<title>eden-cli</title>
</head>
<body style="margin:0;padding:0;background-color:black;color:white;font-family:Monospace,Tahoma,Arial;">
<div style="display:grid;grid-template-columns:1fr 1fr;gap:2px;width:100%;height:100vh;">
    <canvas id="canvas" oncontextmenu="event.preventDefault()" style="width:100%;height:100%;background-color:gray;"></canvas>
    <div id="tty-stdout"></div>
</div>
<script>
var Module = { //do not prepend var
    mainScriptUrlOrBlob: 'eden-cli.js',
    arguments: ['--null-render', '--singlecore', '--filter', '*:Trace', 'game.nro'],
    canvas: document.getElementById('canvas'),
    print: (e) => {
        e = e.replace('[1;31m', '<span style="color:red;font-weight:bold;">');
        e = e.replace('[0;37m', '<span style="color:white;font-weight:bold;">');
        e = e.replace('[1;35m', '<span style="color:pink;font-weight:bold;">');
        e = e.replace('[1;33m', '<span style="color:yellow;font-weight:bold;">');
        e = e.replace('[0;36m', '<span style="color:white;font-weight:bold;">');
        e = e.replace('[0m', '</span>');
        document.getElementById('tty-stdout').innerHTML += \`\${e}</br>\`;
    },
    printErr: (e) => {
        document.getElementById('tty-stdout').innerHTML += \`<span style="color:red">\${e}</span></br>\`;
    },
    // not a wasm func but idc
    printInternal: (e) => {
        document.getElementById('tty-stdout').innerHTML += \`<span style="color:white">Internal WASM: \${e}</span></br>\`;
    },
    onRuntimeInitialized: () => { Module.printInternal("runtime ok"); },
    setStatus: (e) => { Module.printInternal(e); },
    monitorRunDependencies: (e) => { Module.printInternal("monitor deps: " + e); },
    __wasm_call_ctors: () => { Module.printInternal("ctors beep"); },
};
var gameNroFileBuffer = {};
Module.printInternal(\`Atomics: \${window.Atomics}, SharedArrayBuffer: \${window.SharedArrayBuffer}\`);
Module.printInternal("trying to load script (if it hangs here check console)");
fetch('game.nro').then((resp) => {
    if (!resp.ok)
        throw Error(\`\${resp.status}\`);
    return resp.arrayBuffer();
}).then((buffer) => {
    gameNroFileBuffer = buffer;
    Module.printInternal(\`buffer: \${gameNroFileBuffer}\`);
    // load the thingy AFTER loading the nro
    Module.printInternal(\`loading from ${build_dir}/\${Module.mainScriptUrlOrBlob}\`);
    var script = document.createElement('script');
    script.src = '/eden-cli.js';
    script.onload = () => {
        // copy just most relevant :)
        console.log(Module);
        Module.FS.writeFile('/game.nro', new DataView(gameNroFileBuffer));
    };
    document.head.appendChild(script);
}).catch(Module.printErr);
</script>
</body>
</html>`);
    } else if (req.url === '/eden-cli.js') {
        readFile(join(build_dir, 'eden-cli.js'), (err, content) => {
            res.writeHead(200, {
                'Content-Type': 'application/javascript',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp'
            });
            res.end(content, 'utf-8');
        });
    } else if (req.url === '/eden-cli.wasm') {
        readFile(join(build_dir, 'eden-cli.wasm'), (err, content) => {
            res.writeHead(200, {
                'Content-Type': 'application/wasm',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp'
            });
            res.end(content);
        });
    } else if (req.url === '/game.nro') {
        readFile(nro_file, (err, content) => {
            res.writeHead(200, {
                'Content-Type': 'application/octet-stream',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp'
            });
            res.end(content);
        });
    } else {
        res.writeHead(404, {});
        res.end('', 'utf-8');
    }
});

const build_dir = process.argv[2];
const nro_file = process.argv[3];
if (typeof build_dir == "undefined" || typeof nro_file == "undefined") {
    console.log(`Usage: ${process.argv[0]} ${process.argv[1]} [build directory] [NRO file]`);
} else {
    server.listen(2210, () => {
        console.log(`${process.argv[0]} ${process.argv[1]} http://localhost:2210`);
        console.log(`build dir = ${build_dir}`);
    });
}
