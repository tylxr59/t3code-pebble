'use strict';

var fs = require('fs');
var path = require('path');

var root = path.resolve(__dirname, '..');
var changelog = fs.readFileSync(path.join(root, 'CHANGELOG.md'), 'utf8');
var version = process.argv[2] || require(path.join(root, 'package.json')).version;
var escaped = version.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
var header = new RegExp('^##\\s+' + escaped + '\\s*$', 'm');
var match = header.exec(changelog);

if (!match) {
  console.error('No CHANGELOG.md entry found for ' + version);
  process.exit(1);
}

var remainder = changelog.slice(match.index + match[0].length);
var nextHeader = /^##\s+/m.exec(remainder);
var body = (nextHeader ? remainder.slice(0, nextHeader.index) : remainder).trim();
if (!body) {
  console.error('CHANGELOG.md entry for ' + version + ' has no release notes');
  process.exit(1);
}

process.stdout.write(body + '\n');
