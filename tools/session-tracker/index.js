const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');

// Load config
const configPath = path.join(__dirname, 'config.json');
const sessionsPath = path.join(__dirname, 'sessions.json');
const ptsSessionsPath = path.join(os.homedir(), '.claude', 'pts-active-sessions.json');

// Context window sizes per model (in tokens)
const CONTEXT_WINDOWS = {
  'Opus 4.5': 200000,
  'Sonnet 4': 200000,
  'Haiku 3.5': 200000,
  'default': 200000
};
let config = {
  esp32_ip: '192.168.1.100',
  port: 5544,
  max_sessions: 5
};

if (fs.existsSync(configPath)) {
  try {
    config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  } catch (e) {
    console.error('Error loading config:', e.message);
  }
}

const app = express();
app.use(express.json());

// Enable CORS for web UI
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  next();
});

// Session to row mapping
// Key: session_id, Value: { row: number, lastState: string, lastUpdate: timestamp, cwd: string, name: string }
const sessions = new Map();

// Track which rows are in use
let rowsInUse = new Array(config.max_sessions).fill(null);

// Tracking enabled state - when disabled, we don't process events or send updates
let trackingEnabled = true;

// Extract project name from cwd path
function getProjectName(cwd) {
  if (!cwd) return 'Unknown';
  // Get the last folder name from the path
  const parts = cwd.replace(/\\/g, '/').split('/').filter(p => p);
  return parts[parts.length - 1] || 'Unknown';
}

// Save sessions to disk
function saveSessions() {
  const data = {
    sessions: Array.from(sessions.entries()).map(([id, session]) => ({
      sessionId: id,
      ...session
    })),
    rowsInUse: rowsInUse
  };
  try {
    fs.writeFileSync(sessionsPath, JSON.stringify(data, null, 2));
  } catch (e) {
    console.error('Error saving sessions:', e.message);
  }
}

// Load sessions from disk
function loadSessions() {
  if (!fs.existsSync(sessionsPath)) return;
  try {
    const data = JSON.parse(fs.readFileSync(sessionsPath, 'utf8'));
    if (data.sessions) {
      data.sessions.forEach(s => {
        sessions.set(s.sessionId, {
          row: s.row,
          lastState: s.lastState,
          lastUpdate: s.lastUpdate,
          cwd: s.cwd,
          name: s.name,
          contextPercent: s.contextPercent || 0
        });
      });
    }
    if (data.rowsInUse) {
      rowsInUse = data.rowsInUse;
    }
    console.log(`Loaded ${sessions.size} sessions from disk`);
  } catch (e) {
    console.error('Error loading sessions:', e.message);
  }
}

// Load persisted sessions on startup
loadSessions();

// Find next available row
function getNextFreeRow() {
  for (let i = 0; i < config.max_sessions; i++) {
    if (rowsInUse[i] === null) {
      return i;
    }
  }
  return -1; // No rows available
}

// Assign row to session
function assignRow(sessionId, cwd) {
  // Check if session already has a row
  if (sessions.has(sessionId)) {
    const session = sessions.get(sessionId);
    // Update cwd if provided and not already set
    if (cwd && !session.cwd) {
      session.cwd = cwd;
      session.name = getProjectName(cwd);
    }
    return session.row;
  }

  const row = getNextFreeRow();
  if (row === -1) {
    console.log(`No free rows for session ${sessionId}`);
    return -1;
  }

  const name = getProjectName(cwd);
  rowsInUse[row] = sessionId;
  sessions.set(sessionId, {
    row,
    lastState: 'idle',
    lastUpdate: Date.now(),
    cwd: cwd || '',
    name: name,
    contextPercent: 0
  });

  console.log(`Assigned row ${row} to session ${sessionId} (${name})`);
  saveSessions();
  return row;
}

// Free row from session
function freeRow(sessionId) {
  if (!sessions.has(sessionId)) {
    return;
  }

  const session = sessions.get(sessionId);
  const freedRow = session.row;
  rowsInUse[freedRow] = null;
  sessions.delete(sessionId);
  console.log(`Freed row ${freedRow} from session ${sessionId}`);
  saveSessions();
  return freedRow;
}

// Compact rows to eliminate gaps - returns array of row changes to send to ESP32
async function compactRows() {
  const changes = [];

  // Find all active sessions sorted by their current row
  const activeSessions = Array.from(sessions.entries())
    .sort((a, b) => a[1].row - b[1].row);

  // Reset rowsInUse
  for (let i = 0; i < config.max_sessions; i++) {
    rowsInUse[i] = null;
  }

  // Reassign rows starting from 0
  let nextRow = 0;
  for (const [sessionId, session] of activeSessions) {
    const oldRow = session.row;
    if (oldRow !== nextRow) {
      console.log(`Compacting: moving session ${sessionId} from row ${oldRow} to row ${nextRow}`);
      // Clear old row on ESP32
      changes.push({ row: oldRow, state: 'offline', contextPercent: 0 });
      // Set new row on ESP32
      changes.push({ row: nextRow, state: session.lastState, contextPercent: session.contextPercent || 0 });
    }
    session.row = nextRow;
    rowsInUse[nextRow] = sessionId;
    nextRow++;
  }

  saveSessions();

  // Send all changes to ESP32
  for (const change of changes) {
    try {
      await updateEsp32WithContext(change.row, change.state, change.contextPercent);
    } catch (err) {
      console.error(`Failed to update ESP32 for row ${change.row}:`, err.message);
    }
  }

  return changes;
}

// Send state update to ESP32
async function updateEsp32(row, state) {
  return new Promise((resolve, reject) => {
    const url = `http://${config.esp32_ip}/claude/row?row=${row}&state=${state}`;
    console.log(`Sending to ESP32: ${url}`);

    http.get(url, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        console.log(`ESP32 response: ${data}`);
        resolve(data);
      });
    }).on('error', (err) => {
      console.error(`ESP32 error: ${err.message}`);
      reject(err);
    });
  });
}

// Clear a row on ESP32
async function clearEsp32Row(row) {
  return updateEsp32(row, 'offline');
}

// Send state update to ESP32 with context percentage
async function updateEsp32WithContext(row, state, contextPercent) {
  return new Promise((resolve, reject) => {
    const percent = Math.round(contextPercent || 0);
    const url = `http://${config.esp32_ip}/claude/row?row=${row}&state=${state}&contextPercent=${percent}`;
    console.log(`Sending to ESP32: ${url}`);

    http.get(url, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        console.log(`ESP32 response: ${data}`);
        resolve(data);
      });
    }).on('error', (err) => {
      console.error(`ESP32 error: ${err.message}`);
      reject(err);
    });
  });
}

// Map hook events to LED states
function hookEventToState(hookEvent, toolName, eventData) {
  switch (hookEvent) {
    case 'SessionStart':
      return 'idle';

    case 'PreToolUse':
      // AskUserQuestion means Claude is waiting for user response
      if (toolName === 'AskUserQuestion') {
        return 'waiting';
      }
      // Task tool spawns background agents - keep as tool while running
      return 'tool';

    case 'PostToolUse':
      // Check if this was a background task launch (Task tool with run_in_background)
      if (toolName === 'Task' && eventData.tool_input && eventData.tool_input.run_in_background) {
        // Background task launched - return to idle since main session is waiting
        return 'idle';
      }
      // AskUserQuestion just completed - user answered, go to idle
      if (toolName === 'AskUserQuestion') {
        return 'idle';
      }
      // After tool completes, briefly thinking then usually goes idle
      // Return 'thinking' but we'll also set a timeout to go idle
      return 'thinking';

    case 'Notification':
      // Check notification type
      if (eventData && eventData.notification_type === 'idle_prompt') {
        // "Claude is waiting for your input" = idle state
        return 'idle';
      }
      // Background task completion notification
      if (eventData && eventData.notification_type === 'task_notification') {
        // A background task finished - main session might still be idle
        return null; // Don't change state, let the main session's state persist
      }
      // Other notifications are informational, don't change state
      return null;

    case 'Stop':
      // Claude is done, waiting for user input
      return 'idle';

    case 'SessionEnd':
      return 'offline';

    default:
      return null;
  }
}

// Timeout to auto-set idle if no activity (ms)
const IDLE_TIMEOUT = 10000; // 10 seconds

// Timeout to switch from 'tool' to 'waiting' if no PostToolUse received (ms)
// This catches permission prompts where user needs to approve tool execution
const PERMISSION_WAIT_TIMEOUT = 2500; // 2.5 seconds

// Track pending tool timers per session
const pendingToolTimers = new Map();

// Check for stale sessions and set them to idle
function checkIdleTimeouts() {
  if (!trackingEnabled) return;
  const now = Date.now();
  sessions.forEach((session, sessionId) => {
    // If session is in thinking state and no update for IDLE_TIMEOUT, assume idle
    if (session.lastState === 'thinking' && (now - session.lastUpdate) > IDLE_TIMEOUT) {
      console.log(`Session ${sessionId} timed out from thinking -> idle`);
      session.lastState = 'idle';
      session.lastUpdate = now;
      saveSessions();
      updateEsp32WithContext(session.row, 'idle', session.contextPercent || 0).catch(err => {
        console.error('Failed to update ESP32 on timeout:', err.message);
      });
    }
  });
}

// Run idle timeout check every 5 seconds
setInterval(checkIdleTimeouts, 5000);

// Poll PTS sessions file for context window percentages
function pollContextPercentages() {
  if (!trackingEnabled) return;
  if (!fs.existsSync(ptsSessionsPath)) {
    return;
  }

  try {
    const ptsData = JSON.parse(fs.readFileSync(ptsSessionsPath, 'utf8'));
    const ptsSessions = ptsData.sessions || {};

    // Update context percentages for active sessions
    sessions.forEach((session, sessionId) => {
      const ptsSession = ptsSessions[sessionId];
      if (ptsSession && ptsSession.totalTokens) {
        const model = ptsSession.model || 'default';
        const contextWindow = CONTEXT_WINDOWS[model] || CONTEXT_WINDOWS['default'];
        const newPercent = Math.min(100, Math.round((ptsSession.totalTokens / contextWindow) * 100));

        // Only update if changed
        if (newPercent !== session.contextPercent) {
          console.log(`Session ${sessionId} context: ${session.contextPercent}% -> ${newPercent}% (${ptsSession.totalTokens}/${contextWindow} tokens)`);
          session.contextPercent = newPercent;
          saveSessions();

          // Send update to ESP32
          updateEsp32WithContext(session.row, session.lastState, newPercent).catch(err => {
            console.error('Failed to update ESP32 with context:', err.message);
          });
        }
      }
    });
  } catch (err) {
    // Silently ignore errors - file might be being written
  }
}

// Poll context percentages every 3 seconds
setInterval(pollContextPercentages, 3000);

// POST /event - receive hook events from Claude Code
app.post('/event', async (req, res) => {
  // Skip processing if tracking is disabled
  if (!trackingEnabled) {
    return res.json({ success: true, message: 'Tracking disabled, event ignored' });
  }

  try {
    const event = req.body;
    console.log('Received event:', JSON.stringify(event, null, 2));

    const sessionId = event.session_id;
    const hookEvent = event.hook_event_name;
    const cwd = event.cwd;

    if (!sessionId) {
      return res.status(400).json({ error: 'Missing session_id' });
    }

    let row;
    let state;

    if (hookEvent === 'SessionStart') {
      // Assign a row to this new session
      row = assignRow(sessionId, cwd);
      if (row === -1) {
        return res.status(503).json({ error: 'No free rows available' });
      }
      state = 'idle';
    } else if (hookEvent === 'SessionEnd') {
      // Free the row
      row = freeRow(sessionId);
      if (row !== undefined) {
        await clearEsp32Row(row);
        // Compact remaining rows to eliminate gaps
        await compactRows();
      }
      return res.json({ success: true, message: 'Session ended' });
    } else if (hookEvent === 'Status') {
      // Status events contain context window usage information
      // Extract context percentage and update ESP32
      if (!sessions.has(sessionId)) {
        // Session not registered, auto-register it
        row = assignRow(sessionId, cwd);
        if (row === -1) {
          return res.status(503).json({ error: 'No free rows available' });
        }
      }

      const session = sessions.get(sessionId);
      row = session.row;

      // Extract context window percentage
      if (event.context_window && typeof event.context_window.used_percentage === 'number') {
        session.contextPercent = event.context_window.used_percentage;
        session.lastUpdate = Date.now();
        saveSessions();

        // Send context update to ESP32 with current state
        try {
          await updateEsp32WithContext(row, session.lastState, session.contextPercent);
        } catch (err) {
          console.error('Failed to update ESP32 with context:', err.message);
        }
      }

      return res.json({ success: true, row, contextPercent: session.contextPercent });
    } else {
      // Get existing session
      if (!sessions.has(sessionId)) {
        // Session not registered, auto-register it
        row = assignRow(sessionId, cwd);
        if (row === -1) {
          return res.status(503).json({ error: 'No free rows available' });
        }
      } else {
        row = sessions.get(sessionId).row;
        // Update cwd if we have it now
        const session = sessions.get(sessionId);
        if (cwd && !session.cwd) {
          session.cwd = cwd;
          session.name = getProjectName(cwd);
        }
      }
      state = hookEventToState(hookEvent, event.tool_name, event);

      // Handle permission wait timeout for tool use
      if (hookEvent === 'PreToolUse' && state === 'tool') {
        // Clear any existing timer for this session
        if (pendingToolTimers.has(sessionId)) {
          clearTimeout(pendingToolTimers.get(sessionId));
        }
        // Start timer - if PostToolUse doesn't come soon, switch to 'waiting'
        const timerId = setTimeout(async () => {
          pendingToolTimers.delete(sessionId);
          const sess = sessions.get(sessionId);
          if (sess && sess.lastState === 'tool') {
            console.log(`Session ${sessionId} tool timeout -> waiting (permission prompt?)`);
            sess.lastState = 'waiting';
            sess.lastUpdate = Date.now();
            saveSessions();
            try {
              await updateEsp32WithContext(sess.row, 'waiting', sess.contextPercent || 0);
            } catch (err) {
              console.error('Failed to update ESP32 on permission timeout:', err.message);
            }
          }
        }, PERMISSION_WAIT_TIMEOUT);
        pendingToolTimers.set(sessionId, timerId);
      } else if (hookEvent === 'PostToolUse') {
        // Tool completed - clear any pending timer
        if (pendingToolTimers.has(sessionId)) {
          clearTimeout(pendingToolTimers.get(sessionId));
          pendingToolTimers.delete(sessionId);
        }
      }
    }

    if (state && row !== undefined && row >= 0) {
      // Update session state
      const session = sessions.get(sessionId);
      if (session) {
        session.lastState = state;
        session.lastUpdate = Date.now();
        saveSessions();
      }

      // Send to ESP32 with context percentage
      try {
        const contextPercent = session ? session.contextPercent : 0;
        await updateEsp32WithContext(row, state, contextPercent);
      } catch (err) {
        // ESP32 might be offline, but we still track state
        console.error('Failed to update ESP32:', err.message);
      }
    }

    res.json({ success: true, row, state });
  } catch (err) {
    console.error('Error handling event:', err);
    res.status(500).json({ error: err.message });
  }
});

// GET /status - debug endpoint to view current state
app.get('/status', (req, res) => {
  const status = {
    enabled: trackingEnabled,
    sessions: [],
    rows: rowsInUse.map((sessionId, index) => {
      const session = sessionId && sessions.has(sessionId) ? sessions.get(sessionId) : null;
      return {
        row: index,
        sessionId: sessionId || null,
        state: session ? session.lastState : 'offline',
        name: session ? session.name : null,
        contextPercent: session ? session.contextPercent : 0
      };
    })
  };

  sessions.forEach((value, key) => {
    status.sessions.push({
      sessionId: key,
      row: value.row,
      lastState: value.lastState,
      lastUpdate: new Date(value.lastUpdate).toISOString(),
      cwd: value.cwd,
      name: value.name,
      contextPercent: value.contextPercent || 0
    });
  });

  res.json(status);
});

// GET /config - get current config
app.get('/config', (req, res) => {
  res.json(config);
});

// POST /config - update ESP32 IP
app.post('/config', (req, res) => {
  if (req.body.esp32_ip) {
    config.esp32_ip = req.body.esp32_ip;
    // Save to config file
    try {
      fs.writeFileSync(configPath, JSON.stringify(config, null, 2));
      res.json({ success: true, config });
    } catch (err) {
      res.status(500).json({ error: 'Failed to save config' });
    }
  } else {
    res.status(400).json({ error: 'Missing esp32_ip' });
  }
});

// GET /resync - re-send current state for all active sessions to ESP32
app.get('/resync', async (req, res) => {
  console.log('Resyncing all sessions to ESP32...');

  // Compact rows first to eliminate gaps
  await compactRows();

  const results = [];
  for (const [sessionId, session] of sessions) {
    try {
      await updateEsp32WithContext(session.row, session.lastState, session.contextPercent || 0);
      results.push({ row: session.row, state: session.lastState, contextPercent: session.contextPercent || 0, success: true });
    } catch (err) {
      results.push({ row: session.row, state: session.lastState, contextPercent: session.contextPercent || 0, success: false, error: err.message });
    }
  }

  res.json({ success: true, synced: results.length, results });
});

// GET /compact - manually compact rows to eliminate gaps
app.get('/compact', async (req, res) => {
  console.log('Compacting rows...');
  const changes = await compactRows();
  res.json({ success: true, changes });
});

// GET /clear - clear all sessions and ESP32 rows
app.get('/clear', async (req, res) => {
  // Clear all sessions
  sessions.clear();
  for (let i = 0; i < config.max_sessions; i++) {
    rowsInUse[i] = null;
  }
  saveSessions();

  // Clear ESP32
  try {
    await new Promise((resolve, reject) => {
      http.get(`http://${config.esp32_ip}/claude/clear`, (res) => {
        resolve();
      }).on('error', reject);
    });
  } catch (err) {
    console.error('Failed to clear ESP32:', err.message);
  }

  res.json({ success: true, message: 'All sessions cleared' });
});

// GET /enable - enable tracking
app.get('/enable', async (req, res) => {
  trackingEnabled = true;
  console.log('Tracking ENABLED');

  // Resync all sessions to ESP32
  for (const [sessionId, session] of sessions) {
    try {
      await updateEsp32WithContext(session.row, session.lastState, session.contextPercent || 0);
    } catch (err) {
      console.error(`Failed to sync session ${sessionId}:`, err.message);
    }
  }

  res.json({ success: true, enabled: true });
});

// GET /disable - disable tracking and clear ESP32
app.get('/disable', async (req, res) => {
  trackingEnabled = false;
  console.log('Tracking DISABLED');

  // Clear all rows on ESP32
  try {
    await new Promise((resolve, reject) => {
      http.get(`http://${config.esp32_ip}/claude/clear`, (httpRes) => {
        resolve();
      }).on('error', reject);
    });
  } catch (err) {
    console.error('Failed to clear ESP32:', err.message);
  }

  res.json({ success: true, enabled: false });
});

// GET /toggle - toggle tracking on/off
app.get('/toggle', async (req, res) => {
  if (trackingEnabled) {
    // Disable
    trackingEnabled = false;
    console.log('Tracking DISABLED (toggled)');

    try {
      await new Promise((resolve, reject) => {
        http.get(`http://${config.esp32_ip}/claude/clear`, (httpRes) => {
          resolve();
        }).on('error', reject);
      });
    } catch (err) {
      console.error('Failed to clear ESP32:', err.message);
    }
  } else {
    // Enable
    trackingEnabled = true;
    console.log('Tracking ENABLED (toggled)');

    // Resync all sessions
    for (const [sessionId, session] of sessions) {
      try {
        await updateEsp32WithContext(session.row, session.lastState, session.contextPercent || 0);
      } catch (err) {
        console.error(`Failed to sync session ${sessionId}:`, err.message);
      }
    }
  }

  res.json({ success: true, enabled: trackingEnabled });
});

// Start server
app.listen(config.port, () => {
  console.log(`Claude Session Tracker running on http://localhost:${config.port}`);
  console.log(`ESP32 IP: ${config.esp32_ip}`);
  console.log('');
  console.log('Endpoints:');
  console.log('  POST /event    - Receive hook events');
  console.log('  GET  /status   - View session-to-row mapping');
  console.log('  GET  /config   - Get current config');
  console.log('  POST /config   - Update ESP32 IP');
  console.log('  GET  /resync   - Re-send state for all active sessions');
  console.log('  GET  /clear    - Clear all sessions');
});
