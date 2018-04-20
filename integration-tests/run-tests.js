const fs = require('fs');
const Path = require('path');
const execSync = require('child_process').execSync;
const crypto = require('crypto');

let jdiff = 'jdiff';
if (process.argv.indexOf('--jdiff-path') > -1) {
    jdiff = process.argv[process.argv.indexOf('--jdiff-path') + 1];
}

const files = fs.readdirSync(Path.join(__dirname, 'source'));
if (!fs.existsSync(Path.join(__dirname, 'target'))) {
    fs.mkdirSync(Path.join(__dirname, 'target'));
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
            crypto.createHash('sha256').update(fs.readFileSync(Path.join(__dirname, 'source', files[t]))).digest('hex')
        ]);
    }
}

console.log('Generating diffs...');

for (let f of target_files) {
    let [ sourceFile, targetFile, diffFile ] = f;
    execSync(`${jdiff} "${sourceFile}" "${targetFile}" "${diffFile}"`);

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
    execSync(`${Path.join(__dirname, '..', 'janpatch-cli')} "${sourceFile}" "${diffFile}" "${patched}"`);
    let elapsed = '(' + ((Date.now() - start)) + ' ms.)';

    total_time += (Date.now() - start);

    let newHash = crypto.createHash('sha256').update(fs.readFileSync(patched)).digest('hex');

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
