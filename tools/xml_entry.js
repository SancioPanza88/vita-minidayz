var xmldom = require('@xmldom/xmldom');
var xpath = require('xpath');
var DP = xmldom.DOMParser;
var proto = Object.getPrototypeOf(new DP().parseFromString('<a/>', 'text/xml'));
proto.evaluate = function (expr, ctx, ns, type, res) { return xpath.evaluate(expr, ctx || this, ns || null, type || 0, res || null); };
proto.createNSResolver = function () { return null; };
globalThis.DOMParser = function () {};
globalThis.DOMParser.prototype.parseFromString = function (s, mime) {
  return new DP({ onError: function () {} }).parseFromString(s, mime || 'text/xml');
};
globalThis.XMLSerializer = xmldom.XMLSerializer;
globalThis.XPathResult = xpath.XPathResult;
