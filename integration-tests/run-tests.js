const fs = require('fs');
const Path = require('path');
const spawn = require('child_process').spawn;
const crypto = require('crypto');
const util = require('util');

(async () => {
    try {
        let jdiff = 'jdiff';
        if (process.argv.indexOf('--jdiff-path') > -1) {
            jdiff = process.argv[process.argv.indexOf('--jdiff-path') + 1];
        }

        const files = await fs.promises.readdir(Path.join(__dirname, 'source'));
        if (!await pathExists(Path.join(__dirname, 'target'))) {
            await fs.promises.mkdir(Path.join(__dirname, 'target'), { recursive: true });
        }

        const target_files = [];
        for (let s = 0; s < files.length; s++) {
            for (let t = 0; t < files.length; t++) {
                if (t === s) continue;

                target_files.push([
                    Path.join(__dirname, 'source', files[s]),
                    Path.join(__dirname, 'source', files[t]),
                    Path.join(__dirname, 'target',
                        Path.basename(files[s], Path.extname(files[s])) +
                        '_' +
                        Path.basename(files[t], Path.extname(files[t])) + '.diff'),
                    crypto.createHash('sha256').update(await fs.promises.readFile(Path.join(__dirname, 'source', files[t]))).digest('hex')
                ]);
            }
        }

        console.log('Generating diffs...');

        for (let f of target_files) {
            let [ sourceFile, targetFile, diffFile ] = f;
            await spawnHelper(jdiff, [
                sourceFile,
                targetFile,
                diffFile
            ], {
                acceptableExitCodes: [ 0, 1, 2 ], // all of these can be returned by jdiff
            });

            console.log('Generated', diffFile);
        }

        console.log('\nPatching...\n');

        let total_time = 0;
        let total_ok = 0;
        let total_failed = 0;

        for (let f of target_files) {
            let [ sourceFile, targetFile, diffFile, hash ] = f;
            let patched = diffFile.replace(/\.diff$/, '.patched');

            let start = Date.now();
            await spawnHelper(Path.join(__dirname, '..', 'janpatch-cli'), [
                sourceFile,
                diffFile,
                patched,
            ]);
            let elapsed = '(' + ((Date.now() - start)) + ' ms.)';

            total_time += (Date.now() - start);

            let newHash = crypto.createHash('sha256').update(await fs.promises.readFile(patched)).digest('hex');

            let name = Path.basename(patched, '.patched');

            if (newHash !== hash) {
                console.log('    \u2717', name, 'Hash mismatch (expected: ' + hash + ', but was ' + newHash + ')', elapsed);
                total_failed++;
            }
            else {
                console.log('    \u2714', name, 'OK', elapsed);
                total_ok++;
            }
        }

        console.log(`\nSummary: ${total_ok}/${total_ok+total_failed} succeeded (total time: ${total_time} ms.)`);
    }
    catch (ex) {
        console.error(`Failed to run integration tests`, ex);
        process.exit(1);
    }
})();

function spawnHelper(command, args, opts) {
    opts = opts || { };
    if (typeof opts.ignoreErrors !== 'boolean') {
        opts.ignoreErrors = false;
    }
    if (typeof opts.acceptableExitCodes === 'undefined') {
        opts.acceptableExitCodes = [ 0 ];
    }

    return new Promise((resolve, reject) => {
        // console.log(`spawning ${command} ${args.join(' ')}`);
        const p = spawn(command, args, { env: process.env, cwd: opts.cwd });

        const allData = [];

        p.stdout.on('data', (data) => {
            allData.push(data);
            // process.stdout.write(data);
        });

        p.stderr.on('data', (data) => {
            allData.push(data);
            // process.stderr.write(data);
        });

        p.on('error', reject);

        p.on('close', (code) => {
            if (opts.acceptableExitCodes.indexOf(code) > -1 || opts.ignoreErrors === true) {
                resolve(Buffer.concat(allData).toString('utf-8'));
            }
            else {
                let errorCodeStr = opts.acceptableExitCodes.length === 1 ?
                    `Error code was not ${opts.acceptableExitCodes[0]}` :
                    `Error code was not in ${JSON.stringify(opts.acceptableExitCodes)}`;
                reject(`${errorCodeStr} (but ${code}): ` + Buffer.concat(allData).toString('utf-8'));
            }
        });
    });
}

async function pathExists(path) {
    let exists = false;
    try {
        await util.promisify(fs.stat)(path);
        exists = true;
    }
    catch (ex) {
        /* noop */
    }
    return exists;
}
