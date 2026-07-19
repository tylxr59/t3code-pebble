var messageKeys = require("message_keys");

var CMD_CONFIG = 1;
var CMD_PROJECTS = 2;
var CMD_THREADS = 3;
var CMD_MESSAGES = 4;
var CMD_SEND = 5;
var CMD_CANCEL = 6;
var CMD_ITEM = 20;
var CMD_DONE = 21;
var CMD_STATUS = 22;
var CMD_ERROR = 23;

var REQUEST_TIMEOUT_MS = 20000;
var SOCKET_TIMEOUT_MS = 30000;
var POLL_INTERVAL_MS = 10000;
var THREAD_POLL_INTERVAL_MS = 15000;
var MAX_PROJECTS = 30;
var MAX_THREADS = 40;
var MAX_MESSAGES = 40;
var MAX_TEXT = 520;

var OAUTH_TOKEN_EXCHANGE_GRANT = "urn:ietf:params:oauth:grant-type:token-exchange";
var OAUTH_ACCESS_TOKEN_TYPE = "urn:ietf:params:oauth:token-type:access_token";
var T3_BOOTSTRAP_TOKEN_TYPE = "urn:t3:params:oauth:token-type:environment-bootstrap";

var cachedSocketToken = null;
var nextRequestId = 1;
var appMessageQueue = [];
var appMessageBusy = false;
var messageViewToken = 0;
var activeMessagePoll = null;
var activeThreadListPoll = null;
var threadActivityById = {};

function value(payload, name, fallback) {
  if (payload[name] !== undefined) return payload[name];
  var key = messageKeys[name];
  if (key !== undefined && payload[key] !== undefined) return payload[key];
  if (key !== undefined && payload[String(key)] !== undefined) return payload[String(key)];
  return fallback;
}

function readSettings() {
  var raw = localStorage.getItem("t3code-settings");
  if (!raw) return {};
  try {
    return JSON.parse(raw) || {};
  } catch (e) {
    return {};
  }
}

function writeSettings(settings) {
  localStorage.setItem("t3code-settings", JSON.stringify(settings));
}

function normalizeBaseUrl(raw) {
  var text = String(raw || "").trim();
  if (!text) return "";
  if (!/^https?:\/\//i.test(text)) text = "http://" + text;
  if (text.charAt(text.length - 1) !== "/") text += "/";
  return text;
}

function htmlEscape(text) {
  return String(text || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function jsEscape(text) {
  return String(text || "")
    .replace(/\\/g, "\\\\")
    .replace(/'/g, "\\'")
    .replace(/\r/g, "\\r")
    .replace(/\n/g, "\\n");
}

function buildConfigUrl() {
  var settings = readSettings();
  var baseUrl = normalizeBaseUrl(settings.baseUrl || "");
  var bearerToken = settings.bearerToken || "";
  var html =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>T3 Code</title><style>" +
    "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:0;padding:18px;background:#f7f7f7;color:#202124}" +
    "h1{font-size:22px;margin:0 0 16px}label{display:block;font-size:13px;font-weight:600;margin:14px 0 6px}" +
    "input{box-sizing:border-box;width:100%;font-size:16px;padding:11px;border:1px solid #c7c7c7;border-radius:6px;background:white}" +
    "button{width:100%;font-size:16px;font-weight:700;margin-top:16px;padding:12px;border:0;border-radius:6px;background:#111;color:white}" +
    "button:disabled{background:#b8b8b8;color:#f4f4f4}.secondary{background:#3b5bdb}" +
    "#status{white-space:pre-wrap;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;line-height:1.45;margin-top:16px;padding:12px;background:white;border:1px solid #ddd;border-radius:6px;min-height:72px}" +
    ".hint{font-size:12px;color:#666;margin-top:6px;line-height:1.35}" +
    "</style></head><body>" +
    "<h1>T3 Code</h1>" +
    "<label for='baseUrl'>Server URL</label>" +
    "<input id='baseUrl' autocapitalize='none' autocomplete='off' value='" + htmlEscape(baseUrl) + "' placeholder='http://192.168.1.2:3773/'>" +
    "<div class='hint'>Use the IP:PORT reachable from this phone.</div>" +
    "<label for='pairingCode'>Pairing code</label>" +
    "<input id='pairingCode' autocapitalize='characters' autocomplete='off' placeholder='One-time pairing code'>" +
    "<button id='auth' class='secondary'>Authenticate</button>" +
    "<button id='save' disabled>Save</button>" +
    "<div id='status'></div>" +
    "<script>" +
    "var bearerToken='" + jsEscape(bearerToken) + "';" +
    "var bearerExpiresAt='';" +
    "var statusEl=document.getElementById('status');" +
    "var saveEl=document.getElementById('save');" +
    "function norm(v){v=String(v||'').trim();if(!v)return '';if(!/^https?:\\/\\//i.test(v))v='http://'+v;if(v.charAt(v.length-1)!=='/')v+='/';return v}" +
    "function log(v){statusEl.textContent+=(statusEl.textContent?'\\n':'')+v}" +
    "function setReady(){saveEl.disabled=!bearerToken}" +
    "function post(url,type,body,cb){var r=new XMLHttpRequest();var done=false;var t=setTimeout(function(){if(done)return;done=true;try{r.abort()}catch(e){}cb(new Error('HTTP timeout'))},20000);function finish(e,d){if(done)return;done=true;clearTimeout(t);cb(e,d)}r.open('POST',url,true);r.setRequestHeader('content-type',type);r.onload=function(){var d=null;try{d=r.responseText?JSON.parse(r.responseText):null}catch(e){finish(new Error('Bad JSON'));return}if(r.status<200||r.status>=300){var er=new Error(d&&(d.error||d.reason||d.message)?d.error||d.reason||d.message:'HTTP '+r.status);er.status=r.status;finish(er);return}finish(null,d)};r.onerror=function(){finish(new Error('Network error'))};r.send(body)}" +
    "function exchange(base,code,cb){var form='grant_type='+encodeURIComponent('urn:ietf:params:oauth:grant-type:token-exchange')+'&subject_token='+encodeURIComponent(code)+'&subject_token_type='+encodeURIComponent('urn:t3:params:oauth:token-type:environment-bootstrap')+'&requested_token_type='+encodeURIComponent('urn:ietf:params:oauth:token-type:access_token')+'&client_label='+encodeURIComponent('T3 Code for Pebble')+'&client_device_type=mobile';post(base+'/oauth/token','application/x-www-form-urlencoded',form,function(err,data){if(err&&err.status===404){post(base+'/api/auth/bootstrap/bearer','application/json',JSON.stringify({credential:code}),cb);return}cb(err,data)})}" +
    "document.getElementById('auth').onclick=function(){statusEl.textContent='';bearerToken='';setReady();var base=norm(document.getElementById('baseUrl').value);var code=String(document.getElementById('pairingCode').value||'').trim();if(!base){log('Enter server URL');return}if(!code){log('Enter pairing code');return}document.getElementById('baseUrl').value=base;log('Connecting to '+base);log('Exchanging pairing code');exchange(base.replace(/\\/+$/,''),code,function(err,data){if(err){log('Auth failed: '+err.message);return}bearerToken=data&&(data.access_token||data.sessionToken);if(!bearerToken){log('Auth failed: no bearer token');return}bearerExpiresAt=data.expiresAt||'';if(!bearerExpiresAt&&data.expires_in){bearerExpiresAt=new Date(Date.now()+data.expires_in*1000).toISOString()}log('Authenticated');if(bearerExpiresAt)log('Expires '+bearerExpiresAt);setReady()})};" +
    "saveEl.onclick=function(){var base=norm(document.getElementById('baseUrl').value);if(!base){log('Enter server URL');return}if(!bearerToken){log('Authenticate before saving');return}var payload={baseUrl:base,bearerToken:bearerToken,bearerExpiresAt:bearerExpiresAt};document.location='pebblejs://close#'+encodeURIComponent(JSON.stringify(payload))};" +
    "if(bearerToken){log('Existing bearer token loaded');setReady()}else{log('Enter URL and pairing code, then authenticate')}" +
    "</script></body></html>";
  return "data:text/html;charset=utf-8," + encodeURIComponent(html);
}

function endpoint(pathname) {
  var settings = readSettings();
  var baseUrl = normalizeBaseUrl(settings.baseUrl);
  if (!baseUrl) throw new Error("Set server URL");
  return baseUrl.replace(/\/+$/, "") + pathname;
}

function wsBaseUrl() {
  var settings = readSettings();
  var baseUrl = normalizeBaseUrl(settings.baseUrl);
  if (!baseUrl) throw new Error("Set server URL");
  var wsUrl = baseUrl.replace(/^https:/i, "wss:").replace(/^http:/i, "ws:");
  return wsUrl.replace(/\/+$/, "") + "/ws";
}

function send(dict) {
  appMessageQueue.push({ dict: dict, attempts: 0 });
  pumpAppMessageQueue();
}

function pumpAppMessageQueue() {
  if (appMessageBusy || appMessageQueue.length === 0) return;
  appMessageBusy = true;
  var entry = appMessageQueue[0];
  Pebble.sendAppMessage(entry.dict, function() {
    appMessageQueue.shift();
    appMessageBusy = false;
    pumpAppMessageQueue();
  }, function(e) {
    entry.attempts++;
    appMessageBusy = false;
    if (entry.attempts >= 3) {
      console.log("sendAppMessage failed: " + JSON.stringify(e));
      appMessageQueue.shift();
    }
    setTimeout(pumpAppMessageQueue, 250);
  });
}

function sendStatus(text) {
  send({ Command: CMD_STATUS, Status: truncate(text, 80) });
}

function sendError(seq, text) {
  send({ Command: CMD_ERROR, Seq: seq || 0, Error: truncate(text, 120) });
}

function truncate(text, max) {
  text = String(text || "");
  return text.length > max ? text.slice(0, Math.max(0, max - 3)) + "..." : text;
}

function basename(path) {
  var text = String(path || "").replace(/\\/g, "/");
  var parts = text.split("/");
  for (var i = parts.length - 1; i >= 0; i--) {
    if (parts[i]) return parts[i];
  }
  return "";
}

function request(url, method, body, bearerToken, contentType, callback) {
  var req = new XMLHttpRequest();
  var finished = false;
  var timer = setTimeout(function() {
    if (finished) return;
    finished = true;
    try { req.abort(); } catch (e) {}
    callback(new Error("HTTP TIMEOUT"));
  }, REQUEST_TIMEOUT_MS);

  function done(err, data) {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    callback(err, data);
  }

  req.open(method || "GET", url, true);
  if (contentType) req.setRequestHeader("content-type", contentType);
  if (bearerToken) req.setRequestHeader("authorization", "Bearer " + bearerToken);
  req.onload = function() {
    var data = null;
    try {
      data = req.responseText ? JSON.parse(req.responseText) : null;
    } catch (e) {
      done(new Error("Bad JSON"));
      return;
    }
    if (req.status < 200 || req.status >= 300) {
      var message = data && (data.error || data.reason || data.message) ?
        data.error || data.reason || data.message : "HTTP " + req.status;
      var httpError = new Error(message);
      httpError.status = req.status;
      done(httpError);
      return;
    }
    done(null, data);
  };
  req.onerror = function() { done(new Error("NETWORK")); };
  req.send(body === undefined ? null : body);
}

function requestJson(url, method, body, bearerToken, callback) {
  request(url, method, body === undefined ? undefined : JSON.stringify(body), bearerToken,
    "application/json", callback);
}

function requestForm(url, fields, callback) {
  var parts = [];
  for (var name in fields) {
    if (Object.prototype.hasOwnProperty.call(fields, name)) {
      parts.push(encodeURIComponent(name) + "=" + encodeURIComponent(fields[name]));
    }
  }
  request(url, "POST", parts.join("&"), null, "application/x-www-form-urlencoded", callback);
}

function bearerExpiry(result) {
  if (result && result.expiresAt) return result.expiresAt;
  if (result && result.expires_in) {
    return new Date(Date.now() + result.expires_in * 1000).toISOString();
  }
  return "";
}

function exchangePairingCode(pairingCode, callback) {
  requestForm(endpoint("/oauth/token"), {
    grant_type: OAUTH_TOKEN_EXCHANGE_GRANT,
    subject_token: pairingCode,
    subject_token_type: T3_BOOTSTRAP_TOKEN_TYPE,
    requested_token_type: OAUTH_ACCESS_TOKEN_TYPE,
    client_label: "T3 Code for Pebble",
    client_device_type: "mobile"
  }, function(err, result) {
    if (err && err.status === 404) {
      requestJson(endpoint("/api/auth/bootstrap/bearer"), "POST", {
        credential: pairingCode
      }, null, callback);
      return;
    }
    callback(err, result);
  });
}

function ensureBearerToken(callback) {
  var settings = readSettings();
  if (settings.bearerToken) {
    callback(null, settings.bearerToken);
    return;
  }
  if (!settings.pairingCode) {
    callback(new Error("Set pairing code"));
    return;
  }
  exchangePairingCode(settings.pairingCode, function(err, result) {
    if (err) {
      callback(err);
      return;
    }
    var token = result && (result.access_token || result.sessionToken);
    if (!token) {
      callback(new Error("No session token"));
      return;
    }
    settings.bearerToken = token;
    settings.bearerExpiresAt = bearerExpiry(result);
    settings.pairingCode = "";
    writeSettings(settings);
    callback(null, token);
  });
}

function issueSocketUrl(callback) {
  ensureBearerToken(function(err, bearerToken) {
    if (err) {
      callback(err);
      return;
    }
    requestJson(endpoint("/api/auth/websocket-ticket"), "POST", {}, bearerToken, function(wsErr, result) {
      if (wsErr && wsErr.status === 404) {
        requestJson(endpoint("/api/auth/ws-token"), "POST", {}, bearerToken,
          function(legacyErr, legacyResult) {
            finishSocketUrl(legacyErr, legacyResult, true, callback);
          });
        return;
      }
      finishSocketUrl(wsErr, result, false, callback);
    });
  });
}

function finishSocketUrl(wsErr, result, legacy, callback) {
  if (wsErr) {
    if (wsErr.status === 401) {
      var settings = readSettings();
      settings.bearerToken = "";
      writeSettings(settings);
    }
    callback(wsErr);
    return;
  }
  cachedSocketToken = result && (legacy ? result.token : result.ticket);
  if (!cachedSocketToken) {
    callback(new Error(legacy ? "No WS token" : "No WS ticket"));
    return;
  }
  callback(null, withSocketToken(cachedSocketToken, legacy));
}

function withSocketToken(token, legacy) {
  return wsBaseUrl() + (legacy ? "?wsToken=" : "?wsTicket=") + encodeURIComponent(token);
}

function rpc(method, params, onChunk, onDone) {
  issueSocketUrl(function(err, url) {
    if (err) {
      onDone(err);
      return;
    }
    var ws;
    var id = nextRequestId++;
    var settled = false;
    var timer = setTimeout(function() {
      finish(new Error("WS TIMEOUT"));
    }, SOCKET_TIMEOUT_MS);

    function finish(doneErr, result) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try { if (ws) ws.close(); } catch (e) {}
      onDone(doneErr, result);
    }

    try {
      ws = new WebSocket(url);
    } catch (e) {
      finish(e);
      return;
    }

    ws.onopen = function() {
      ws.send(JSON.stringify({
        _tag: "Request",
        id: String(id),
        tag: method,
        payload: params || {},
        headers: []
      }));
    };
    ws.onerror = function() {
      cachedSocketToken = null;
      finish(new Error("WS ERROR"));
    };
    ws.onclose = function() {
      if (!settled) finish(new Error("WS CLOSED"));
    };
    ws.onmessage = function(event) {
      var message;
      try {
        message = JSON.parse(event.data);
      } catch (e) {
        finish(new Error("Bad WS JSON"));
        return;
      }
      if (message._tag === "Pong") return;
      if (message._tag === "Chunk" && String(message.requestId) === String(id)) {
        var items = message.values || [];
        var shouldFinish = false;
        for (var i = 0; i < items.length; i++) {
          if (onChunk(items[i]) === true) shouldFinish = true;
        }
        ws.send(JSON.stringify({
          _tag: "Ack",
          requestId: String(id)
        }));
        if (shouldFinish) finish(null, null);
        return;
      }
      if (message._tag === "Exit" && String(message.requestId) === String(id)) {
        if (message.exit && message.exit._tag === "Failure") {
          finish(new Error(effectFailureMessage(message.exit)));
          return;
        }
        finish(null, message.exit ? message.exit.value : null);
        return;
      }
      if (message._tag === "Defect") {
        finish(new Error(String(message.defect || "WS DEFECT")));
        return;
      }
    };
  });
}

function effectFailureMessage(exit) {
  var cause = exit && exit.cause;
  if (!cause || !cause.length) return "RPC failed";
  for (var i = 0; i < cause.length; i++) {
    var item = cause[i];
    if (item && item._tag === "Fail" && item.error) {
      if (item.error.message) return item.error.message;
      return JSON.stringify(item.error);
    }
    if (item && item._tag === "Die") return String(item.defect || "RPC defect");
  }
  return "RPC failed";
}

function loadShellSnapshot(seq, callback) {
  var completed = false;
  rpc("orchestration.subscribeShell", {}, function(item) {
    if (!item || item.kind !== "snapshot" || !item.snapshot) return;
    completed = true;
    callback(null, item.snapshot);
    return true;
  }, function(err) {
    if (err && !completed) {
      rpc("orchestration.getArchivedShellSnapshot", {}, function() {}, callback);
    }
  });
}

function cancelMessagePoll() {
  messageViewToken++;
  if (activeMessagePoll && activeMessagePoll.timer) {
    clearTimeout(activeMessagePoll.timer);
  }
  activeMessagePoll = null;
}

function cancelThreadListPoll() {
  if (activeThreadListPoll && activeThreadListPoll.timer) {
    clearTimeout(activeThreadListPoll.timer);
  }
  activeThreadListPoll = null;
}

function clearMessagePollTimer() {
  if (activeMessagePoll && activeMessagePoll.timer) {
    clearTimeout(activeMessagePoll.timer);
    activeMessagePoll.timer = null;
  }
}

function normalizedSessionStatus(status) {
  return String(status || "").toLowerCase();
}

function isThreadWorking(thread) {
  if (!thread) return false;
  var messages = thread.messages || [];
  for (var i = 0; i < messages.length; i++) {
    if (messages[i] && messages[i].streaming) return true;
  }
  var status = thread.session ? normalizedSessionStatus(thread.session.status) : "";
  return status === "running" || status === "connecting";
}

function hasUnseenCompletion(thread) {
  if (!thread || !thread.latestTurn || !thread.latestTurn.completedAt) return false;
  var completedAt = Date.parse(thread.latestTurn.completedAt);
  if (isNaN(completedAt)) return false;
  if (!thread.lastVisitedAt) return true;
  var lastVisitedAt = Date.parse(thread.lastVisitedAt);
  if (isNaN(lastVisitedAt)) return true;
  return completedAt > lastVisitedAt;
}

function threadStatusText(thread, working) {
  return working ? "Working" : "Ready";
}

function activityForThread(thread, working) {
  var id = thread && thread.id;
  if (!id) return { working: working, unseenDone: false };
  var previous = threadActivityById[id] || {};
  var unseenDone = !!previous.unseenDone || hasUnseenCompletion(thread);
  if (previous.working && !working) unseenDone = true;
  threadActivityById[id] = {
    working: working,
    unseenDone: unseenDone
  };
  return threadActivityById[id];
}

function clearThreadDone(threadId) {
  if (!threadId) return;
  var activity = threadActivityById[threadId] || {};
  activity.unseenDone = false;
  threadActivityById[threadId] = activity;
}

function scheduleMessagePoll(seq, threadId, token) {
  clearMessagePollTimer();
  if (token !== messageViewToken) return;
  activeMessagePoll = {
    threadId: threadId,
    token: token,
    timer: setTimeout(function() {
      if (!activeMessagePoll || activeMessagePoll.token !== token || activeMessagePoll.threadId !== threadId) return;
      loadMessages(seq, threadId, {
        status: "Working...",
        trackWorking: true,
        viewToken: token
      });
    }, POLL_INTERVAL_MS)
  };
}

function scheduleThreadListPoll(seq, projectId) {
  cancelThreadListPoll();
  activeThreadListPoll = {
    projectId: projectId,
    seq: seq,
    timer: setTimeout(function() {
      if (!activeThreadListPoll || activeThreadListPoll.projectId !== projectId || activeThreadListPoll.seq !== seq) return;
      loadThreads(seq, projectId, { poll: true });
    }, THREAD_POLL_INTERVAL_MS)
  };
}

function loadProjects(seq) {
  cancelMessagePoll();
  cancelThreadListPoll();
  sendStatus("Loading projects");
  loadShellSnapshot(seq, function(err, snapshot) {
    if (err) {
      sendError(seq, err.message);
      return;
    }
    var projects = snapshot && snapshot.projects ? snapshot.projects : [];
    projects.sort(function(a, b) {
      return String(b.updatedAt || "").localeCompare(String(a.updatedAt || ""));
    });
    var count = Math.min(projects.length, MAX_PROJECTS);
    for (var i = 0; i < count; i++) {
      send({
        Command: CMD_ITEM,
        Seq: seq,
        Index: i,
        Total: count,
        ProjectId: projects[i].id,
        Title: truncate(projects[i].title || basename(projects[i].workspaceRoot) || "Project", 64),
        Status: truncate(basename(projects[i].workspaceRoot) || projects[i].workspaceRoot || "Project workspace", 28)
      });
    }
    send({ Command: CMD_DONE, Seq: seq, Total: count });
  });
}

function loadThreads(seq, projectId, options) {
  options = options || {};
  cancelMessagePoll();
  if (!options.poll) sendStatus("Loading threads");
  loadShellSnapshot(seq, function(err, snapshot) {
    if (err) {
      sendError(seq, err.message);
      return;
    }
    var threads = snapshot && snapshot.threads ? snapshot.threads : [];
    var filtered = [];
    for (var i = 0; i < threads.length; i++) {
      if (threads[i].projectId === projectId && !threads[i].archivedAt) filtered.push(threads[i]);
    }
    filtered.sort(function(a, b) {
      return String(b.updatedAt || "").localeCompare(String(a.updatedAt || ""));
    });
    var count = Math.min(filtered.length, MAX_THREADS);
    var anyWorking = false;
    for (var j = 0; j < count; j++) {
      var working = isThreadWorking(filtered[j]);
      var activity = activityForThread(filtered[j], working);
      if (working) anyWorking = true;
      send({
        Command: CMD_ITEM,
        Seq: seq,
        Index: j,
        Total: count,
        ThreadId: filtered[j].id,
        Title: truncate(filtered[j].title || "Thread", 64),
        Status: threadStatusText(filtered[j], working),
        Working: working ? 1 : 0,
        UnseenDone: activity.unseenDone ? 1 : 0
      });
    }
    send({ Command: CMD_DONE, Seq: seq, Total: count });
    if (anyWorking) {
      scheduleThreadListPoll(seq, projectId);
    } else {
      cancelThreadListPoll();
    }
  });
}

function loadMessages(seq, threadId, options) {
  options = options || {};
  var viewToken = options.viewToken;
  if (viewToken !== undefined && viewToken !== messageViewToken) return;
  sendStatus(options.status || "Loading messages");
  rpc("orchestration.subscribeThread", { threadId: threadId }, function(item) {
    if (viewToken !== undefined && viewToken !== messageViewToken) return true;
    if (!item || item.kind !== "snapshot" || !item.snapshot.thread) return;
    var thread = item.snapshot.thread;
    var messages = thread.messages || [];
    var count = Math.min(messages.length, MAX_MESSAGES);
    var start = Math.max(0, messages.length - count);
    for (var i = 0; i < count; i++) {
      var msg = messages[start + i];
      send({
        Command: CMD_ITEM,
        Seq: seq,
        Index: i,
        Total: count,
        MessageId: msg.id,
        Role: msg.role,
        Text: truncate(msg.text || "", MAX_TEXT),
        Status: msg.streaming ? "streaming" : ""
      });
    }
    var working = isThreadWorking(thread);
    send({ Command: CMD_DONE, Seq: seq, Total: count, Status: working ? "Working..." : "" });
    if (options.trackWorking) {
      if (working) {
        scheduleMessagePoll(seq, threadId, viewToken);
      } else if (activeMessagePoll && activeMessagePoll.threadId === threadId) {
        clearMessagePollTimer();
        activeMessagePoll = null;
      }
    }
    return true;
  }, function(err) {
    if (viewToken !== undefined && viewToken !== messageViewToken) return;
    if (err) sendError(seq, err.message);
  });
}

function uuid() {
  var chars = "0123456789abcdef";
  var out = "";
  for (var i = 0; i < 36; i++) {
    if (i === 8 || i === 13 || i === 18 || i === 23) {
      out += "-";
    } else if (i === 14) {
      out += "4";
    } else if (i === 19) {
      out += chars.charAt((Math.floor(Math.random() * 16) & 3) | 8);
    } else {
      out += chars.charAt(Math.floor(Math.random() * 16));
    }
  }
  return out;
}

function isLocalThreadId(threadId) {
  return String(threadId || "").indexOf("pebble-thread:") === 0;
}

function firstLine(text) {
  var line = String(text || "").split(/\r?\n/)[0];
  return truncate(line || "Pebble thread", 48);
}

function modelSelectionForProject(snapshot, projectId) {
  var threads = snapshot && snapshot.threads ? snapshot.threads : [];
  var best = null;
  for (var i = 0; i < threads.length; i++) {
    if (threads[i].projectId === projectId && threads[i].modelSelection) {
      if (!best || String(threads[i].updatedAt || "").localeCompare(String(best.updatedAt || "")) > 0) {
        best = threads[i];
      }
    }
  }
  return best && best.modelSelection ? best.modelSelection : { provider: "codex", model: "gpt-5" };
}

function createThread(seq, projectId, text, callback) {
  if (!projectId) {
    callback(new Error("Missing project"));
    return;
  }
  var serverThreadId = uuid();
  loadShellSnapshot(seq, function(snapshotErr, snapshot) {
    if (snapshotErr) {
      callback(snapshotErr);
      return;
    }
    rpc("orchestration.dispatchCommand", {
      type: "thread.create",
      commandId: uuid(),
      threadId: serverThreadId,
      projectId: projectId,
      title: firstLine(text),
      modelSelection: modelSelectionForProject(snapshot, projectId),
      runtimeMode: "full-access",
      interactionMode: "default",
      branch: null,
      worktreePath: null,
      createdAt: new Date().toISOString()
    }, function() {}, function(err) {
      callback(err, serverThreadId);
    });
  });
}

function dispatchTurn(threadId, text, callback) {
  var now = new Date().toISOString();
  rpc("orchestration.dispatchCommand", {
    type: "thread.turn.start",
    commandId: uuid(),
    threadId: threadId,
    message: {
      messageId: uuid(),
      role: "user",
      text: text || "",
      attachments: []
    },
    runtimeMode: "full-access",
    interactionMode: "default",
    createdAt: now
  }, function() {}, callback);
}

function sendTurn(seq, threadId, projectId, text) {
  sendStatus("Sending");
  function finish(actualThreadId, err) {
    if (err) {
      sendError(seq, err.message);
      return;
    }
    if (actualThreadId) {
      threadActivityById[actualThreadId] = {
        working: true,
        unseenDone: false
      };
    }
    send({ Command: CMD_DONE, Seq: seq, ThreadId: actualThreadId, Status: "Sent" });
  }
  if (isLocalThreadId(threadId)) {
    createThread(seq, projectId, text, function(err, serverThreadId) {
      if (err) {
        finish(threadId, err);
        return;
      }
      dispatchTurn(serverThreadId, text, function(turnErr) {
        finish(serverThreadId, turnErr);
      });
    });
  } else {
    dispatchTurn(threadId, text, function(err) {
      finish(threadId, err);
    });
  }
}

Pebble.addEventListener("ready", function() {});

Pebble.addEventListener("showConfiguration", function() {
  Pebble.openURL(buildConfigUrl());
});

Pebble.addEventListener("webviewclosed", function(e) {
  if (!e || !e.response) return;
  var response = decodeURIComponent(e.response);
  var saved;
  try {
    saved = JSON.parse(response);
  } catch (err) {
    sendStatus("Settings cancelled");
    return;
  }
  var settings = readSettings();
  settings.baseUrl = normalizeBaseUrl(saved.baseUrl || settings.baseUrl);
  settings.bearerToken = saved.bearerToken || "";
  settings.bearerExpiresAt = saved.bearerExpiresAt || "";
  settings.pairingCode = "";
  cachedSocketToken = null;
  writeSettings(settings);
  sendStatus("Settings saved");
});

Pebble.addEventListener("appmessage", function(e) {
  var payload = e.payload || {};
  var command = value(payload, "Command", 0);
  var seq = value(payload, "Seq", 0);
  if (command === CMD_CONFIG) {
    var settings = readSettings();
    send({
      Command: CMD_STATUS,
      Seq: seq,
      Status: settings.baseUrl ? "Configured" : "Open settings"
    });
  } else if (command === CMD_PROJECTS) {
    loadProjects(seq);
  } else if (command === CMD_THREADS) {
    loadThreads(seq, value(payload, "ProjectId", ""));
  } else if (command === CMD_MESSAGES) {
    cancelMessagePoll();
    cancelThreadListPoll();
    var token = messageViewToken;
    var threadId = value(payload, "ThreadId", "");
    clearThreadDone(threadId);
    loadMessages(seq, threadId, {
      status: "Loading messages",
      trackWorking: true,
      viewToken: token
    });
  } else if (command === CMD_SEND) {
    cancelMessagePoll();
    cancelThreadListPoll();
    sendTurn(seq, value(payload, "ThreadId", ""), value(payload, "ProjectId", ""), value(payload, "Text", ""));
  } else if (command === CMD_CANCEL) {
    cancelMessagePoll();
    cancelThreadListPoll();
  }
});
