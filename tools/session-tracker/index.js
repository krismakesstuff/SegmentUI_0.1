const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

// Load config
const configPath = path.join(__dirname, 'config.json');
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

// Session to row mapping
// Key: session_id, Value: { row: number, lastState: string, lastUpdate: timestamp }
const sessions = new Map();

// Track which rows are in use
const rowsInUse = new Array(config.max_sessions).fill(null);

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
function assignRow(sessionId) {
  // Check if session already has a row
  if (sessions.has(sessionId)) {
    return sessions.get(sessionId).row;
  }

  const row = getNextFreeRow();
  if (row === -1) {
    console.log(`No free rows for session ${sessionId}`);
    return -1;
  }

  rowsInUse[row] = sessionId;
  sessions.set(sessionId, {
    row,
    lastState: 'idle',
    lastUpdate: Date.now()
  });

  console.log(`Assigned row ${row} to session ${sessionId}`);
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
function hookEventToState(hookEvent, toolName) {
  switch (hookEvent) {
    case 'SessionStart':
      return 'idle';

    case 'PreToolUse':
      return 'tool';

    case 'PostToolUse':
      return 'idle';

    case 'Notification':
      // Notifications usually mean waiting for user input
      return 'waiting';

    case 'Stop':
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

    if (!sessionId) {
      return res.status(400).json({ error: 'Missing session_id' });
    }

    let row;
    let state;

    if (hookEvent === 'SessionStart') {
      // Assign a row to this new session
      row = assignRow(sessionId);
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
        row = assignRow(sessionId);
        if (row === -1) {
          return res.status(503).json({ error: 'No free rows available' });
        }
      } else {
        row = sessions.get(sessionId).row;
      }
      state = hookEventToState(hookEvent, event.tool_name);
    }

    if (state && row !== undefined && row >= 0) {
      // Update session state
      const session = sessions.get(sessionId);
      if (session) {
        session.lastState = state;
        session.lastUpdate = Date.now();
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
    rows: rowsInUse.map((sessionId, index) => ({
      row: index,
      sessionId: sessionId || null,
      state: sessionId && sessions.has(sessionId) ? sessions.get(sessionId).lastState : 'offline'
    }))
  };

  sessions.forEach((value, key) => {
    status.sessions.push({
      sessionId: key,
      row: value.row,
      lastState: value.lastState,
      lastUpdate: new Date(value.lastUpdate).toISOString()
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

// GET /clear - clear all sessions and ESP32 rows
app.get('/clear', async (req, res) => {
  // Clear all sessions
  sessions.clear();
  for (let i = 0; i < config.max_sessions; i++) {
    rowsInUse[i] = null;
  }

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
  console.log('  GET  /clear    - Clear all sessions');
});
