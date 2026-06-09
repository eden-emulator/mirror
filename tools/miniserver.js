#!/usr/local/env node
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

import { createServer } from 'http';
import { readFile } from 'fs';
import { join } from 'path';

// DO NOT RUN THIS IN ANY PRODUCTION ENVIRONMENT EVER
const server = createServer((req, res) => {
    console.log(`reuqest? ${req.url}`);
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
<script>
var Module = {}; // MUST be global otherwise wasm does wawawawa
function finished_body() {
    Module = { //do not prepend var
        print: (e) => {
            e = e.replace('[1;31m', '<span style="color:red;font-weight:bold;">');
            e = e.replace('[0;37m', '<span style="color:white;font-weight:bold;">');
            e = e.replace('[1;35m', '<span style="color:pink;font-weight:bold;">');
            e = e.replace('[1;33m', '<span style="color:yellow;font-weight:bold;">');
            e = e.replace('[0m', '</span>');
            tty_stdout.innerHTML += e + '</br>';
        },
        printErr: (e) => {
            tty_stdout.innerHTML += '<span style="color:red">' + e + '</span></br>';
        },
        // not a wasm func but idc
        printInternal: (e) => {
            tty_stdout.innerHTML += '<span style="color:pink">Internal WASM: ' + e + '</span></br>';
        },
        onRuntimeInitialized: () => { Module.printInternal("runtime ok"); },
        setStatus: (e) => { Module.printInternal(e); },
        monitorRunDependencies: (e) => { Module.printInternal("monitor deps: " + e); },
        __wasm_call_ctors: () => { Module.printInternal("ctors beep"); },
    };

    var tty_stdout = document.createElement('div');
    document.body.appendChild(tty_stdout);
    Module.printInternal("loading from ${build_dir}/eden-cli.js");

    Module.arguments = ['game.nro'];

    var script = document.createElement('script');
    script.src = '/eden-cli.js';
    script.onload = () => {
        Module.printInternal("Atomics: " + window.Atomics);
        Module.printInternal("SharedArrayBuffer: " + window.SharedArrayBuffer);
        Module.printInternal("trying to load script (if it hangs here check console)");

        Module.FS.writeFile('/game.nro', [1,2,3]);
    };
    document.head.appendChild(script);
}
</script>
</head>
<body style="margin:0;padding:0;background-color:black;font-family:Monospace,Tahoma,Arial;" onload="finished_body();">
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
            res.end(content, 'utf-8');
        });
    }
});

const build_dir = process.argv[2];
if (build_dir === undefined) {
    console.log(`Usage: ${process.argv[0]} ${process.argv[1]} [build directory]`);
} else {
    server.listen(2210, () => {
        console.log(`${process.argv[0]} ${process.argv[1]} http://localhost:2210`);
        console.log(`build dir = ${build_dir}`);
    });
}
