
// This is a flaky webserver that introduces random weirdness and errors.
// It randomly pauses and resumes.
// It also writes out data in random-sized bursts with random delays.


var clientIdCounter = 0;


// Generate a 128k random data buffer
var buf = new Buffer(128 * 1024),
    from = 'abcdefghijklmnopqrstuvwxyz ';
for (var i = 0; i < buf.length; i++) {
  buf[i] = from.charCodeAt(Math.floor(Math.random() * from.length));
}


// Create the server
require('net').createServer(handleConnection).listen(8000);

function handleConnection(socket) {
  var clientId = ++clientIdCounter,
      sendTimeout = null,
      receiveTimeout = null,
      errorTimeout = null,
      disconnectTimeout = null,
      paused = true,
      received = 0,
      sentMin = 0,
      sentMax = 0;

  console.log("#%d: New connection from %s:%d.",
              clientId,
              socket.remoteAddress,
              socket.remotePort);

  // Start in paused state
  socket.pause();

  // Attach listeners.
  socket.on('data', function(buf) {
    received += buf.length;
  });

  socket.on('error', function(err) {
    console.log('#%d: %s.', clientId, err);
    cancelReceive();
    cancelSend();
    cancelDisconnect();
  });

  socket.on('end', function() {
    console.log('#%d: Received FIN packet.', clientId);
    cancelReceive();
  });

  socket.on('close', function() {
    if (sentMin != sentMax) {
      console.log('#%d: Connection closed. Received: %d bytes. Sent: between %d and %d bytes.',
                  clientId,
                  received,
                  sentMin,
                  sentMax);
    } else {
      console.log('#%d: Connection closed. Received: %d bytes. Sent: %d bytes.',
                    clientId,
                  received,
                  sentMax);
    }

    cancelReceive();
    cancelSend();
    cancelDisconnect();
  });

  // Schedule flakyness
  scheduleSend();
  scheduleReceive();
  scheduleDisconnect();

  function scheduleSend() {
    var delay = Math.pow(2000, Math.random()) - 1;
    sendTimeout = setTimeout(sendBurst, delay);
  }

  function scheduleReceive() {
    var delay = Math.pow(2000, Math.random()) - 1;
    receiveTimeout = setTimeout(flipPausedState, delay);
  }

  function scheduleDisconnect() {
    var delay = 60000; // Math.round(Math.pow(5 * 60 * 1000, Math.random())) - 1;
    console.log("#%d: Will disconnect after %d seconds.", clientId, delay / 1000);
    disconnectTimeout = setTimeout(disconnect, delay);
  }

  function cancelReceive() {
    if (receiveTimeout) {
      clearTimeout(receiveTimeout);
      receiveTimeout = null;
    }
  }

  function cancelSend() {
    if (sendTimeout) {
      clearTimeout(sendTimeout);
      sendTimeout = null;
    }
  }

  function cancelDisconnect() {
    if (disconnectTimeout) {
      clearTimeout(disconnectTimeout);
      disconnectTimeout = null;
    }
  }

  function flipPausedState() {
    if (paused) {
      paused = false;
      socket.resume();
    } else {
      paused = true;
      socket.pause();
    }

    scheduleReceive();
  }

  function sendBurst() {
    var size = Math.floor(Math.pow(buf.length, Math.random())) - 1,
        offset = Math.floor(Math.random() * (buf.length - size)),
        burst = buf.slice(offset, offset + size);

    socket.write(burst, function(err) {
      if (err) {
        return;
      }

      sentMin += size;

      if (socket.writable)
        scheduleSend();
    });
    sentMax += size;

    sendTimeout = null;
  }

  function disconnect() {
    var graceful = socket.writable && (Math.random() >= .5);
    if (graceful) {
      console.log('#%d: Sending FIN packet.', clientId);
      socket.end();
      cancelSend();
    } else {
      console.log('#%d: Initiating hard close.', clientId);
      socket.destroy();
      cancelSend();
      cancelReceive();
    }

    disconnectTimeout = null;
  }
}
