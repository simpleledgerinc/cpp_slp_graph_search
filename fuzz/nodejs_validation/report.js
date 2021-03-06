/*
 * We use slp-validate's internal methods to get a JSON
 * Then we format this for consumption & comparison by differential fuzzer.
 */

const fs = require('fs');
const turbo = require('turbo-http')
const validate = require('slp-validate');

if (process.argv.length < 3) {
    console.log('missing input file');
    process.exit(1);
}

const bin = fs.readFileSync(process.argv[2])


try {
    y = validate.Slp.parseSlpOutputScript(bin);

    if (y.transactionType == "GENESIS") {
        y.symbol         = Buffer.from(y.symbol, 'binary').toString('hex').toUpperCase();
        y.name           = Buffer.from(y.name, 'binary').toString('hex').toUpperCase();
        y.documentUri    = Buffer.from(y.documentUri, 'binary').toString('hex').toUpperCase();
        if (y.documentSha256 === null) {
            y.documentSha256 = "";
        }
        y.documentSha256 = Buffer.from(y.documentSha256, 'binary').toString('hex').toUpperCase();
    }

    if (y.hasOwnProperty("genesisOrMintQuantity")) {
        y.genesisOrMintQuantity = y.genesisOrMintQuantity.toString()
    }

    if (y.hasOwnProperty("sendOutputs")) {
        y.sendOutputs = y.sendOutputs.map((v) => v.toString());
    }
} catch(e) {
    console.log(e.message);
    process.exit(1);
}

console.log(y);
