#!/bin/sh
# Rebuilds src/js/xml.js (DOMParser + XPath for the Construct 2 XML plugin)
set -e
cd "$(dirname "$0")"
TMP=$(mktemp -d)
cp xml_entry.js "$TMP/entry.js"
cd "$TMP" && npm init -y >/dev/null && npm i @xmldom/xmldom xpath esbuild >/dev/null
npx esbuild entry.js --bundle --minify --format=iife --target=es2017 --outfile="$OLDPWD/../src/js/xml.js"
