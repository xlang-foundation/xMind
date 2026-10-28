'use strict';
// Shared transport policy data; no HTTP server is implemented here.
const rules=require('./xmind_web_routes.json');
const compiled=Object.fromEntries(Object.entries(rules).map(([method,paths])=>[method,paths.map(path=>new RegExp('^(?:'+path+')$'))]));
function allowedApiRoute(path,method){return typeof path==='string'&&(compiled[method]||[]).some(pattern=>pattern.test(path));}
module.exports={allowedApiRoute};
