var messageKeys = require("message_keys");

var CMD_CONFIG = 1;
var CMD_PROJECTS = 2;
var CMD_THREADS = 3;
var CMD_MESSAGES = 4;
var CMD_SEND = 5;
var CMD_ITEM = 20;
var CMD_DONE = 21;
var CMD_STATUS = 22;
var CMD_ERROR = 23;

var REQUEST_TIMEOUT_MS = 20000;
var SOCKET_TIMEOUT_MS = 30000;
var MAX_PROJECTS = 30;
var MAX_THREADS = 40;
var MAX_MESSAGES = 40;
var MAX_TEXT = 520;

var cachedSocketToken = null;
var nextRequestId = 1;
var appMessageQueue = [];
var appMessageBusy = false;

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
    "function postJson(url,body,cb){var r=new XMLHttpRequest();var done=false;var t=setTimeout(function(){if(done)return;done=true;try{r.abort()}catch(e){}cb(new Error('HTTP timeout'))},20000);function finish(e,d){if(done)return;done=true;clearTimeout(t);cb(e,d)}r.open('POST',url,true);r.setRequestHeader('content-type','application/json');r.onload=function(){var d=null;try{d=r.responseText?JSON.parse(r.responseText):null}catch(e){finish(new Error('Bad JSON'));return}if(r.status<200||r.status>=300){finish(new Error(d&&d.error?d.error:'HTTP '+r.status));return}finish(null,d)};r.onerror=function(){finish(new Error('Network error'))};r.send(JSON.stringify(body||{}))}" +
    "document.getElementById('auth').onclick=function(){statusEl.textContent='';bearerToken='';setReady();var base=norm(document.getElementById('baseUrl').value);var code=String(document.getElementById('pairingCode').value||'').trim();if(!base){log('Enter server URL');return}if(!code){log('Enter pairing code');return}document.getElementById('baseUrl').value=base;log('Connecting to '+base);log('Exchanging pairing code');postJson(base.replace(/\\/+$/,'')+'/api/auth/bootstrap/bearer',{credential:code},function(err,data){if(err){log('Auth failed: '+err.message);return}if(!data||!data.sessionToken){log('Auth failed: no bearer token');return}bearerToken=data.sessionToken;bearerExpiresAt=data.expiresAt||'';log('Authenticated');if(bearerExpiresAt)log('Expires '+bearerExpiresAt);setReady()})};" +
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

function requestJson(url, method, body, bearerToken, callback) {
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
  req.setRequestHeader("content-type", "application/json");
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
      var message = data && data.error ? data.error : "HTTP " + req.status;
      var httpError = new Error(message);
      httpError.status = req.status;
      done(httpError);
      return;
    }
    done(null, data);
  };
  req.onerror = function() { done(new Error("NETWORK")); };
  req.send(body === undefined ? null : JSON.stringify(body));
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
  requestJson(endpoint("/api/auth/bootstrap/bearer"), "POST", {
    credential: settings.pairingCode
  }, null, function(err, result) {
    if (err) {
      callback(err);
      return;
    }
    var token = result && result.sessionToken;
    if (!token) {
      callback(new Error("No session token"));
      return;
    }
    settings.bearerToken = token;
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
    requestJson(endpoint("/api/auth/ws-token"), "POST", {}, bearerToken, function(wsErr, result) {
      if (wsErr) {
        if (wsErr.status === 401) {
          var settings = readSettings();
          settings.bearerToken = "";
          writeSettings(settings);
        }
        callback(wsErr);
        return;
      }
      cachedSocketToken = result && result.token;
      if (!cachedSocketToken) {
        callback(new Error("No WS token"));
        return;
      }
      callback(null, withSocketToken(cachedSocketToken));
    });
  });
}

function withSocketToken(token) {
  return wsBaseUrl() + "?wsToken=" + encodeURIComponent(token);
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

function loadProjects(seq) {
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

function loadThreads(seq, projectId) {
  sendStatus("Loading threads");
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
    for (var j = 0; j < count; j++) {
      send({
        Command: CMD_ITEM,
        Seq: seq,
        Index: j,
        Total: count,
        ThreadId: filtered[j].id,
        Title: truncate(filtered[j].title || "Thread", 64),
        Status: filtered[j].session ? filtered[j].session.status : ""
      });
    }
    send({ Command: CMD_DONE, Seq: seq, Total: count });
  });
}

function loadMessages(seq, threadId) {
  sendStatus("Loading messages");
  rpc("orchestration.subscribeThread", { threadId: threadId }, function(item) {
    if (!item || item.kind !== "snapshot" || !item.snapshot.thread) return;
    var messages = item.snapshot.thread.messages || [];
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
    send({ Command: CMD_DONE, Seq: seq, Total: count });
    return true;
  }, function(err) {
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
    send({ Command: CMD_DONE, Seq: seq, ThreadId: actualThreadId, Status: "Sent" });
    loadMessages(seq + 1, actualThreadId);
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
    loadMessages(seq, value(payload, "ThreadId", ""));
  } else if (command === CMD_SEND) {
    sendTurn(seq, value(payload, "ThreadId", ""), value(payload, "ProjectId", ""), value(payload, "Text", ""));
  }
});
