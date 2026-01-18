const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

// Load config
const configPath = path.join(__dirname, 'config.json');
const sessionsPath = path.join(__dirname, 'sessions.json');
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
          name: s.name
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
    name: name
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
  rowsInUse[session.row] = null;
  sessions.delete(sessionId);
  console.log(`Freed row ${session.row} from session ${sessionId}`);
  saveSessions();
  return session.row;
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
      return 'tool';

    case 'PostToolUse':
      // After tool completes, Claude is thinking about the result
      return 'thinking';

    case 'Notification':
      // Check notification type
      if (eventData && eventData.notification_type === 'idle_prompt') {
        // "Claude is waiting for your input" = idle state
        return 'idle';
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

// POST /event - receive hook events from Claude Code
app.post('/event', async (req, res) => {
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
      }
      return res.json({ success: true, message: 'Session ended' });
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
    }

    if (state && row !== undefined && row >= 0) {
      // Update session state
      const session = sessions.get(sessionId);
      if (session) {
        session.lastState = state;
        session.lastUpdate = Date.now();
        saveSessions();
      }

      // Send to ESP32
      try {
        await updateEsp32(row, state);
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
    sessions: [],
    rows: rowsInUse.map((sessionId, index) => {
      const session = sessionId && sessions.has(sessionId) ? sessions.get(sessionId) : null;
      return {
        row: index,
        sessionId: sessionId || null,
        state: session ? session.lastState : 'offline',
        name: session ? session.name : null
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
      name: value.name
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
  const results = [];

  for (const [sessionId, session] of sessions) {
    try {
      await updateEsp32(session.row, session.lastState);
      results.push({ row: session.row, state: session.lastState, success: true });
    } catch (err) {
      results.push({ row: session.row, state: session.lastState, success: false, error: err.message });
    }
  }

  res.json({ success: true, synced: results.length, results });
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
